#ifndef MELONPRIME_THREAD_BRIDGE_H
#define MELONPRIME_THREAD_BRIDGE_H

#include <atomic>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace MelonPrime {

struct MelonPrimeUiSnapshot {
    bool focused = false;
    bool captureWanted = false;
    bool cursorMode = true;
    bool stylusMode = false;
    bool inGame = false;
    bool romDetected = false;
    bool fastForward = false;
    bool rawAimActive = false;
    int screenSyncMode = 0;
    int centerX = 0;
    int centerY = 0;
};

// EmuThread -> GUI persistence request. Only the latest hotkey value matters;
// generation makes replacement/order explicit and supports stale-request checks.
struct MelonPrimePersistRequest {
    enum class Type : uint8_t {
        None = 0,
        AimSensitivity = 1,
    };

    Type type = Type::None;
    int value = 0;
    uint64_t generation = 0;
};

// Explicit GUI/EmuThread boundary for MelonPrime-only state. GUI producers
// write command/input mailboxes; EmuThread publishes the runtime snapshot and
// GUI request bits. No QWidget or platform GUI object crosses this boundary.
class MelonPrimeThreadBridge {
public:
    enum GuiRequest : uint32_t {
        GuiRequestNone = 0,
        GuiRequestRecenter = 1u << 0,
        GuiRequestRefreshCapture = 1u << 1,
        // Kept for source compatibility with in-flight Phase 4 work. Cursor
        // presentation is reconciled from m_cursorVisibleDesired instead of
        // deciding precedence between these edge bits.
        GuiRequestShowCursor = 1u << 2,
        GuiRequestHideCursor = 1u << 3,
        GuiRequestReconcileCursor = 1u << 4,
    };

    void SetFocusedFromGui(bool value) noexcept
    {
        SetGuiInputPolicyBit(GuiPolicyFocused, value);
    }
    void SetCaptureWantedFromGui(bool value) noexcept
    {
        SetGuiInputPolicyBit(GuiPolicyCaptureWanted, value);
    }
    void SetPanelAvailableFromGui(bool value) noexcept
    {
        SetGuiInputPolicyBit(GuiPolicyPanelAvailable, value);
    }
    void PublishCenterFromGui(int x, int y) noexcept
    {
        const uint64_t packed = PackInt32Pair(x, y);
        if (m_center.load(std::memory_order_relaxed) == packed)
            return;
        m_center.store(packed, std::memory_order_release);
    }
    void PublishWindowHandleFromGui(uintptr_t handle) noexcept
    {
        if (m_windowHandle.load(std::memory_order_relaxed) == handle)
            return;
        m_windowHandle.store(handle, std::memory_order_release);
    }
    void NotifyLayoutChangeFromGui() noexcept
    {
        m_layoutGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    void AddWheelFromGui(int steps) noexcept
    {
        if (!steps)
            return;

        // The wheel mailbox is a single-producer/single-consumer accumulator,
        // but the generation tag must be published atomically with its value.
        // A Qt pulse from an old capture registration is therefore discarded
        // instead of being carried into the next Raw Input owner epoch.
        const uint64_t generationValue =
            m_inputGeneration.load(std::memory_order_acquire);
        const uint32_t generation = static_cast<uint32_t>(generationValue);
        uint64_t current = m_wheelMailbox.load(std::memory_order_relaxed);
        for (;;) {
            const uint32_t currentGeneration =
                static_cast<uint32_t>(current >> 32);
            const int32_t currentSteps = static_cast<int32_t>(
                static_cast<uint32_t>(current));
            int64_t nextSteps = (currentGeneration == generation ? currentSteps : 0);
            nextSteps += steps;
            nextSteps = std::clamp<int64_t>(
                nextSteps,
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::max());
            const uint64_t desired = PackWheelMailbox(
                generation, static_cast<int32_t>(nextSteps));
            if (m_wheelMailbox.compare_exchange_weak(
                    current, desired,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                // A generation publication may race this GUI event. If the
                // emulation thread advanced the source after our first load,
                // retry with the current generation instead of resurrecting an
                // old pulse after the boundary.
                if (m_inputGeneration.load(std::memory_order_acquire)
                    == generationValue)
                    return;
                current = m_wheelMailbox.load(std::memory_order_relaxed);
            }
        }
    }
    void SetInputGenerationFromEmu(uint64_t generation) noexcept
    {
        const uint64_t normalizedGeneration = generation ? generation : 1;
        if (m_inputGeneration.load(std::memory_order_acquire)
            == normalizedGeneration)
            return;
        const uint64_t previous = m_inputGeneration.exchange(
            normalizedGeneration, std::memory_order_acq_rel);
        if (previous != normalizedGeneration) {
            // Registration change is a hard mailbox boundary. Events already
            // queued by Qt are intentionally not carried into the new source.
            // Use CAS so a new-generation GUI pulse published concurrently
            // with this boundary is retained rather than overwritten.
            const uint32_t generation =
                static_cast<uint32_t>(normalizedGeneration);
            uint64_t current = m_wheelMailbox.load(std::memory_order_relaxed);
            for (;;) {
                if (static_cast<uint32_t>(current >> 32) == generation)
                    break;
                const uint64_t desired = PackWheelMailbox(generation, 0);
                if (m_wheelMailbox.compare_exchange_weak(
                        current, desired,
                        std::memory_order_release,
                        std::memory_order_relaxed))
                    break;
            }
        }
    }
    void RequestCursorModeFromGui(bool enabled) noexcept
    {
        m_cursorModeCommand.store(enabled ? 1 : 0, std::memory_order_release);
    }
    void AddPanelAimDeltaFromGui(int32_t dx, int32_t dy) noexcept
    {
        if ((dx | dy) == 0)
            return;

        // Qt dispatch serializes every ScreenPanel mouse producer on the GUI
        // thread. Publish one packed cumulative total with load/store; reset
        // requests never write this producer-owned value.
        const uint64_t current =
            m_panelAimTotal.load(std::memory_order_relaxed);
        const uint32_t nextX = PairX(current) + static_cast<uint32_t>(dx);
        const uint32_t nextY = PairY(current) + static_cast<uint32_t>(dy);
        m_panelAimTotal.store(
            PackUint32Pair(nextX, nextY), std::memory_order_release);
    }
    void ResetPanelAimDeltaFromGui() noexcept
    {
        // GUI is the sole reset-boundary writer. Publish the cumulative total
        // first, then a monotonically changing generation; the EmuThread
        // consumer owns its cursor and never writes either GUI field.
        m_panelAimGuiResetBoundary.store(
            m_panelAimTotal.load(std::memory_order_acquire),
            std::memory_order_release);
        uint32_t generation = ++m_panelAimGuiResetGenerationShadow;
        if (generation == 0)
            generation = ++m_panelAimGuiResetGenerationShadow;
        m_panelAimGuiResetGeneration.store(
            generation, std::memory_order_release);
    }
    void PublishStylusPointerFromGui(int x, int y, bool valid) noexcept
    {
        uint32_t packed = 0;
        if (valid) {
            packed = (1u << 31)
                | (static_cast<uint32_t>(y) & 0xFFu) << 8
                | (static_cast<uint32_t>(x) & 0xFFu);
        }
        if (m_stylusPointer.load(std::memory_order_relaxed) == packed)
            return;
        m_stylusPointer.store(packed, std::memory_order_release);
    }

    // MELONPRIME_PHASE5_CONFIG_USAGE_V1
    // Packed publication keeps value and generation in one atomic operation.
    // Rapid hotkey presses intentionally coalesce to the latest value.
    void RequestAimSensitivityPersistFromEmu(int value) noexcept
    {
        uint32_t generation =
            m_persistGeneration.fetch_add(1, std::memory_order_relaxed) + 1u;
        if (generation == 0) {
            generation =
                m_persistGeneration.fetch_add(1, std::memory_order_relaxed) + 1u;
        }

        const uint64_t packed =
            (static_cast<uint64_t>(generation) << 32)
            | static_cast<uint32_t>(value);
        m_aimSensitivityPersist.store(packed, std::memory_order_release);
    }

    bool TakePersistRequestForGui(MelonPrimePersistRequest& out) noexcept
    {
        if (m_aimSensitivityPersist.load(std::memory_order_relaxed) == 0)
            return false;
        const uint64_t packed =
            m_aimSensitivityPersist.exchange(0, std::memory_order_acq_rel);
        if (packed == 0)
            return false;

        out.type = MelonPrimePersistRequest::Type::AimSensitivity;
        out.value = static_cast<int32_t>(static_cast<uint32_t>(packed));
        out.generation = static_cast<uint32_t>(packed >> 32);
        return true;
    }

    void DiscardPersistRequestsFromGui() noexcept
    {
        if (m_aimSensitivityPersist.load(std::memory_order_relaxed) != 0)
            (void)m_aimSensitivityPersist.exchange(
                0, std::memory_order_acq_rel);
    }

    [[nodiscard]] bool FocusedForEmu() const noexcept
    {
        return (m_guiInputPolicy.load(std::memory_order_acquire)
            & GuiPolicyFocused) != 0;
    }
    [[nodiscard]] bool CaptureWantedForEmu() const noexcept
    {
        return (m_guiInputPolicy.load(std::memory_order_acquire)
            & GuiPolicyCaptureWanted) != 0;
    }
    [[nodiscard]] bool PanelAvailableForEmu() const noexcept
    {
        return (m_guiInputPolicy.load(std::memory_order_acquire)
            & GuiPolicyPanelAvailable) != 0;
    }
    [[nodiscard]] uint64_t LayoutGenerationForEmu() const noexcept
    {
        return m_layoutGeneration.load(std::memory_order_acquire);
    }
    [[nodiscard]] uintptr_t WindowHandleForEmu() const noexcept
    {
        return m_windowHandle.load(std::memory_order_acquire);
    }
    int ConsumeWheelForEmu(uint64_t expectedGeneration = 0) noexcept
    {
        // Zero is the only empty encoding. A generation-only publication is
        // nonzero and must still be claimed so an old owner epoch cannot leak.
        // A producer racing a zero observation is consumed next frame, which is
        // the mailbox's existing coalescing contract.
        const uint64_t observed = m_wheelMailbox.load(std::memory_order_relaxed);
        if (observed == 0)
            return 0;
        const uint64_t packed = m_wheelMailbox.exchange(0, std::memory_order_acq_rel);
        if (!packed)
            return 0;
        if (expectedGeneration != 0
            && static_cast<uint32_t>(packed >> 32)
                != static_cast<uint32_t>(expectedGeneration))
            return 0;
        return static_cast<int32_t>(static_cast<uint32_t>(packed));
    }
    int ConsumeCursorModeForEmu() noexcept
    {
        // GUI commands are level-replacement requests. A command published
        // after the empty load remains pending and is consumed next frame.
        const int observed = m_cursorModeCommand.load(std::memory_order_relaxed);
        if (observed == -1)
            return -1;
        return m_cursorModeCommand.exchange(-1, std::memory_order_acq_rel);
    }
    void getAimMouseDelta(int32_t& dx, int32_t& dy) noexcept
    {
        uint64_t current = 0;
        for (;;) {
            const uint32_t generationBefore =
                m_panelAimGuiResetGeneration.load(std::memory_order_acquire);
            if (generationBefore != m_panelAimGuiResetSeen) {
                m_panelAimCursor =
                    m_panelAimGuiResetBoundary.load(std::memory_order_acquire);
                m_panelAimGuiResetSeen = generationBefore;
            }
            current = m_panelAimTotal.load(std::memory_order_acquire);
            const uint32_t generationAfter =
                m_panelAimGuiResetGeneration.load(std::memory_order_acquire);
            if (generationBefore == generationAfter)
                break;
            // A reset raced the snapshot. Retry without advancing the cursor;
            // the new boundary decides which motion belongs after the reset.
        }
        dx = static_cast<int32_t>(
            PairX(current) - PairX(m_panelAimCursor));
        dy = static_cast<int32_t>(
            PairY(current) - PairY(m_panelAimCursor));
        m_panelAimCursor = current;
    }
    void resetAimMouseDelta() noexcept
    {
        ResetPanelAimDeltaFromEmu();
    }
    void ResetPanelAimDeltaFromEmu() noexcept
    {
        for (;;) {
            const uint32_t generationBefore =
                m_panelAimGuiResetGeneration.load(std::memory_order_acquire);
            const uint64_t current =
                m_panelAimTotal.load(std::memory_order_acquire);
            const uint32_t generationAfter =
                m_panelAimGuiResetGeneration.load(std::memory_order_acquire);
            if (generationBefore != generationAfter)
                continue;
            m_panelAimCursor = current;
            m_panelAimGuiResetSeen = generationBefore;
            break;
        }
    }
    void ReadCenterForEmu(int& x, int& y) const noexcept
    {
        const uint64_t center = m_center.load(std::memory_order_acquire);
        x = static_cast<int32_t>(PairX(center));
        y = static_cast<int32_t>(PairY(center));
    }
    [[nodiscard]] bool ReadStylusPointerForEmu(int& x, int& y) const noexcept
    {
        const uint32_t packed = m_stylusPointer.load(std::memory_order_acquire);
        if ((packed & (1u << 31)) == 0)
            return false;
        x = static_cast<int>(packed & 0xFFu);
        y = static_cast<int>((packed >> 8) & 0xFFu);
        return true;
    }

    void PublishRuntimeFromEmu(bool cursorMode, bool stylusMode,
                               bool inGame, bool romDetected,
                               bool fastForward, bool rawAimActive,
                               int screenSyncMode) noexcept
    {
        uint32_t bits = 0;
        bits |= cursorMode ? 1u << 0 : 0;
        bits |= stylusMode ? 1u << 1 : 0;
        bits |= inGame ? 1u << 2 : 0;
        bits |= romDetected ? 1u << 3 : 0;
        bits |= fastForward ? 1u << 4 : 0;
        bits |= rawAimActive ? 1u << 5 : 0;
        bits |= static_cast<uint32_t>(screenSyncMode & 0x3) << 6;
        if (m_runtimeBits.load(std::memory_order_relaxed) == bits)
            return;
        m_runtimeBits.store(bits, std::memory_order_release);
        BumpGuiWorkRevisionFromEmu();
    }
    [[nodiscard]] MelonPrimeUiSnapshot ReadForGui() const noexcept
    {
        MelonPrimeUiSnapshot out;
        const uint32_t bits = m_runtimeBits.load(std::memory_order_acquire);
        out.cursorMode = (bits & (1u << 0)) != 0;
        out.stylusMode = (bits & (1u << 1)) != 0;
        out.inGame = (bits & (1u << 2)) != 0;
        out.romDetected = (bits & (1u << 3)) != 0;
        out.fastForward = (bits & (1u << 4)) != 0;
        out.rawAimActive = (bits & (1u << 5)) != 0;
        out.screenSyncMode = static_cast<int>((bits >> 6) & 0x3);
        const uint32_t guiPolicy =
            m_guiInputPolicy.load(std::memory_order_acquire);
        out.focused = (guiPolicy & GuiPolicyFocused) != 0;
        out.captureWanted = (guiPolicy & GuiPolicyCaptureWanted) != 0;
        const uint64_t center = m_center.load(std::memory_order_acquire);
        out.centerX = static_cast<int32_t>(PairX(center));
        out.centerY = static_cast<int32_t>(PairY(center));
        return out;
    }

    void RequestGuiFromEmu(uint32_t requests) noexcept
    {
        bool publishedNewWork = false;
        if (requests & GuiRequestRecenter) {
            if (!m_recenterPending.load(std::memory_order_relaxed)) {
                m_recenterPending.store(true, std::memory_order_release);
                publishedNewWork = true;
            }
        }
        const uint32_t remaining = requests & ~GuiRequestRecenter;
        if (remaining) {
            const uint32_t previous =
                m_guiRequests.fetch_or(remaining, std::memory_order_acq_rel);
            if ((previous & remaining) != remaining)
                publishedNewWork = true;
        }
        if (publishedNewWork)
            BumpGuiWorkRevisionFromEmu();
    }

    // MELONPRIME_CURSOR_AUTHORITATIVE_STATE_V1
    // Cursor visibility is a level-triggered desired state, not two competing
    // show/hide edges. Publishing the desired value before the request bit
    // gives the GUI consumer one authoritative answer even when transitions
    // coalesce between two paint/draw passes.
    void RequestCursorVisibilityFromEmu(
        bool visible,
        uint32_t additionalRequests = GuiRequestNone) noexcept
    {
        m_cursorVisibleDesired.store(visible, std::memory_order_release);
        RequestGuiFromEmu(
            GuiRequestReconcileCursor | additionalRequests);
    }

    // A new ROM/session supersedes every cursor request from the old one.
    // Replace, rather than OR, pending requests so a stale hide/recenter cannot
    // be replayed after the new session has returned to menu cursor mode.
    void ResetCursorPresentationFromEmu() noexcept
    {
        m_cursorVisibleDesired.store(true, std::memory_order_release);
        m_recenterPending.store(false, std::memory_order_release);
        (void)m_guiRequests.exchange(
            GuiRequestReconcileCursor, std::memory_order_acq_rel);
        BumpGuiWorkRevisionFromEmu();
    }

    [[nodiscard]] bool CursorVisibleDesiredForGui() const noexcept
    {
        return m_cursorVisibleDesired.load(std::memory_order_acquire);
    }

    uint32_t TakeGuiRequestsFromGui() noexcept
    {
        uint32_t requests = 0;
        if (m_guiRequests.load(std::memory_order_relaxed) != 0)
            requests = m_guiRequests.exchange(0, std::memory_order_acq_rel);
        if (m_recenterPending.load(std::memory_order_relaxed)
            && m_recenterPending.exchange(false, std::memory_order_acq_rel))
            requests |= GuiRequestRecenter;
        return requests;
    }

    [[nodiscard]] uint64_t GuiWorkRevisionForGui() const noexcept
    {
        return m_guiWorkRevision.load(std::memory_order_acquire);
    }

private:
    enum GuiPolicyBit : uint32_t {
        GuiPolicyFocused = 1u << 0,
        GuiPolicyCaptureWanted = 1u << 1,
        GuiPolicyPanelAvailable = 1u << 2,
    };

    void SetGuiInputPolicyBit(uint32_t bit, bool value) noexcept
    {
        const uint32_t next = value
            ? (m_guiInputPolicyShadow | bit)
            : (m_guiInputPolicyShadow & ~bit);
        if (next == m_guiInputPolicyShadow)
            return;
        m_guiInputPolicyShadow = next;
        m_guiInputPolicy.store(next, std::memory_order_release);
    }

    void BumpGuiWorkRevisionFromEmu() noexcept
    {
        uint64_t next = ++m_guiWorkRevisionShadow;
        if (next == 0)
            next = ++m_guiWorkRevisionShadow;
        m_guiWorkRevision.store(next, std::memory_order_release);
    }

    static uint64_t PackUint32Pair(uint32_t x, uint32_t y) noexcept
    {
        return static_cast<uint64_t>(x)
            | (static_cast<uint64_t>(y) << 32);
    }
    static uint64_t PackInt32Pair(int32_t x, int32_t y) noexcept
    {
        return PackUint32Pair(
            static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }
    static uint32_t PairX(uint64_t packed) noexcept
    {
        return static_cast<uint32_t>(packed);
    }
    static uint32_t PairY(uint64_t packed) noexcept
    {
        return static_cast<uint32_t>(packed >> 32);
    }

    static uint64_t PackWheelMailbox(uint32_t generation, int32_t steps) noexcept
    {
        return (static_cast<uint64_t>(generation) << 32)
            | static_cast<uint32_t>(steps);
    }

    std::atomic<uint32_t> m_guiInputPolicy{0};
    // GUI-thread-only coherent policy source.
    uint32_t m_guiInputPolicyShadow = 0;
    std::atomic<uint64_t> m_center{0};
    std::atomic<uint64_t> m_layoutGeneration{1};
    std::atomic<uintptr_t> m_windowHandle{0};
    std::atomic<uint64_t> m_inputGeneration{1};
    std::atomic<uint64_t> m_wheelMailbox{PackWheelMailbox(1, 0)};
    std::atomic<int> m_cursorModeCommand{-1};
    std::atomic<uint64_t> m_panelAimTotal{0};
    std::atomic<uint64_t> m_panelAimGuiResetBoundary{0};
    std::atomic<uint32_t> m_panelAimGuiResetGeneration{0};
    // GUI-thread-only generation source.
    uint32_t m_panelAimGuiResetGenerationShadow = 0;
    // Emulation-thread-only cursor into the GUI-owned cumulative total.
    uint64_t m_panelAimCursor = 0;
    uint32_t m_panelAimGuiResetSeen = 0;
    // GUI-published DS coordinate under the pointer. Packed so the emulation
    // thread cannot observe X/Y from different mouse events; bit 31 is valid.
    std::atomic<uint32_t> m_stylusPointer{0};
    std::atomic<uint32_t> m_runtimeBits{1u};
    std::atomic_bool m_cursorVisibleDesired{true};
    std::atomic<uint32_t> m_guiRequests{0};
    std::atomic<uint64_t> m_guiWorkRevision{1};
    // EmuThread-only revision source; GUI only acquire-loads the publication.
    uint64_t m_guiWorkRevisionShadow = 1;
    // SPSC level request kept separate from the multi-bit GUI command word so
    // the 60/120 Hz QCursor fallback does not issue a locked fetch_or.
    std::atomic_bool m_recenterPending{false};
    std::atomic<uint32_t> m_persistGeneration{0};
    std::atomic<uint64_t> m_aimSensitivityPersist{0};
};

} // namespace MelonPrime

#endif // MELONPRIME_THREAD_BRIDGE_H
