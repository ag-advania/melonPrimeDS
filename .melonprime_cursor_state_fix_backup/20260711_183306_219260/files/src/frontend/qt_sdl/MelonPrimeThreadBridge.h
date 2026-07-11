#ifndef MELONPRIME_THREAD_BRIDGE_H
#define MELONPRIME_THREAD_BRIDGE_H

#include <atomic>
#include <cstdint>

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

// Explicit GUI/EmuThread boundary for MelonPrime-only state. GUI producers
// write command/input mailboxes; EmuThread publishes the runtime snapshot and
// GUI request bits. No QWidget or platform GUI object crosses this boundary.
class MelonPrimeThreadBridge {
public:
    enum GuiRequest : uint32_t {
        GuiRequestNone = 0,
        GuiRequestRecenter = 1u << 0,
        GuiRequestRefreshCapture = 1u << 1,
        GuiRequestShowCursor = 1u << 2,
        GuiRequestHideCursor = 1u << 3,
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
        m_wheelSteps.fetch_add(steps, std::memory_order_release);
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
    int ConsumeWheelForEmu() noexcept
    {
        return m_wheelSteps.exchange(0, std::memory_order_acq_rel);
    }
    int ConsumeCursorModeForEmu() noexcept
    {
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
    uint32_t TakeGuiRequestsFromGui() noexcept
    {
        return m_guiRequests.exchange(0, std::memory_order_acq_rel);
    }

private:
    std::atomic_bool m_focused{false};
    std::atomic_bool m_captureWanted{false};
    std::atomic_bool m_panelAvailable{false};
    std::atomic<int> m_centerX{0};
    std::atomic<int> m_centerY{0};
    std::atomic<uint64_t> m_layoutGeneration{1};
    std::atomic<uintptr_t> m_windowHandle{0};
    std::atomic<int> m_wheelSteps{0};
    std::atomic<int> m_cursorModeCommand{-1};
    std::atomic<int32_t> m_panelAimX{0};
    std::atomic<int32_t> m_panelAimY{0};
    std::atomic<uint32_t> m_runtimeBits{1u};
    std::atomic<uint32_t> m_guiRequests{0};
};

} // namespace MelonPrime

#endif // MELONPRIME_THREAD_BRIDGE_H
