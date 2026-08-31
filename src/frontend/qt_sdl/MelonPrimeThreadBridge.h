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
        m_focused.store(value, std::memory_order_release);
    }
    void SetCaptureWantedFromGui(bool value) noexcept
    {
        m_captureWanted.store(value, std::memory_order_release);
    }
    void SetPanelAvailableFromGui(bool value) noexcept
    {
        m_panelAvailable.store(value, std::memory_order_release);
    }
    void PublishCenterFromGui(int x, int y) noexcept
    {
        m_centerX.store(x, std::memory_order_relaxed);
        m_centerY.store(y, std::memory_order_release);
    }
    void PublishWindowHandleFromGui(uintptr_t handle) noexcept
    {
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
        m_panelAimX.fetch_add(dx, std::memory_order_relaxed);
        m_panelAimY.fetch_add(dy, std::memory_order_release);
    }
    void ResetPanelAimDeltaFromGui() noexcept
    {
        m_panelAimX.store(0, std::memory_order_relaxed);
        m_panelAimY.store(0, std::memory_order_release);
    }
    void PublishStylusPointerFromGui(int x, int y, bool valid) noexcept
    {
        uint32_t packed = 0;
        if (valid) {
            packed = (1u << 31)
                | (static_cast<uint32_t>(y) & 0xFFu) << 8
                | (static_cast<uint32_t>(x) & 0xFFu);
        }
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
        (void)m_aimSensitivityPersist.exchange(0, std::memory_order_acq_rel);
    }

    [[nodiscard]] bool FocusedForEmu() const noexcept
    {
        return m_focused.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool CaptureWantedForEmu() const noexcept
    {
        return m_captureWanted.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool PanelAvailableForEmu() const noexcept
    {
        return m_panelAvailable.load(std::memory_order_acquire);
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
        dx = m_panelAimX.exchange(0, std::memory_order_acq_rel);
        dy = m_panelAimY.exchange(0, std::memory_order_acq_rel);
    }
    void resetAimMouseDelta() noexcept
    {
        ResetPanelAimDeltaFromGui();
    }
    void ReadCenterForEmu(int& x, int& y) const noexcept
    {
        y = m_centerY.load(std::memory_order_acquire);
        x = m_centerX.load(std::memory_order_relaxed);
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
        out.focused = m_focused.load(std::memory_order_acquire);
        out.captureWanted = m_captureWanted.load(std::memory_order_acquire);
        out.centerY = m_centerY.load(std::memory_order_acquire);
        out.centerX = m_centerX.load(std::memory_order_relaxed);
        return out;
    }

    void RequestGuiFromEmu(uint32_t requests) noexcept
    {
        m_guiRequests.fetch_or(requests, std::memory_order_release);
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
        m_guiRequests.fetch_or(
            GuiRequestReconcileCursor | additionalRequests,
            std::memory_order_release);
    }

    // A new ROM/session supersedes every cursor request from the old one.
    // Replace, rather than OR, pending requests so a stale hide/recenter cannot
    // be replayed after the new session has returned to menu cursor mode.
    void ResetCursorPresentationFromEmu() noexcept
    {
        m_cursorVisibleDesired.store(true, std::memory_order_release);
        (void)m_guiRequests.exchange(
            GuiRequestReconcileCursor, std::memory_order_acq_rel);
    }

    [[nodiscard]] bool CursorVisibleDesiredForGui() const noexcept
    {
        return m_cursorVisibleDesired.load(std::memory_order_acquire);
    }

    uint32_t TakeGuiRequestsFromGui() noexcept
    {
        return m_guiRequests.exchange(0, std::memory_order_acq_rel);
    }

private:
    static uint64_t PackWheelMailbox(uint32_t generation, int32_t steps) noexcept
    {
        return (static_cast<uint64_t>(generation) << 32)
            | static_cast<uint32_t>(steps);
    }

    std::atomic_bool m_focused{false};
    std::atomic_bool m_captureWanted{false};
    std::atomic_bool m_panelAvailable{false};
    std::atomic<int> m_centerX{0};
    std::atomic<int> m_centerY{0};
    std::atomic<uint64_t> m_layoutGeneration{1};
    std::atomic<uintptr_t> m_windowHandle{0};
    std::atomic<uint64_t> m_inputGeneration{1};
    std::atomic<uint64_t> m_wheelMailbox{PackWheelMailbox(1, 0)};
    std::atomic<int> m_cursorModeCommand{-1};
    std::atomic<int32_t> m_panelAimX{0};
    std::atomic<int32_t> m_panelAimY{0};
    // GUI-published DS coordinate under the pointer. Packed so the emulation
    // thread cannot observe X/Y from different mouse events; bit 31 is valid.
    std::atomic<uint32_t> m_stylusPointer{0};
    std::atomic<uint32_t> m_runtimeBits{1u};
    std::atomic_bool m_cursorVisibleDesired{true};
    std::atomic<uint32_t> m_guiRequests{0};
    std::atomic<uint32_t> m_persistGeneration{0};
    std::atomic<uint64_t> m_aimSensitivityPersist{0};
};

} // namespace MelonPrime

#endif // MELONPRIME_THREAD_BRIDGE_H
