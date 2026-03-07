#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>

namespace MelonPrime {

    std::atomic<int>    RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
    std::once_flag      RawInputWinFilter::s_initFlag;

    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();
            });
    }

    RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, void* windowHandle) {
        if (s_refCount.fetch_add(1) == 0) {
            s_instance = new RawInputWinFilter(joy2KeySupport, static_cast<HWND>(windowHandle));
        }
        else if (s_instance) {
            s_instance->setRawInputTarget(static_cast<HWND>(windowHandle));
            if (s_instance->m_joy2KeySupport != joy2KeySupport) {
                s_instance->setJoy2KeySupport(joy2KeySupport);
            }
        }
        return s_instance;
    }

    void RawInputWinFilter::Release() {
        if (s_refCount.fetch_sub(1) == 1) {
            delete s_instance;
            s_instance = nullptr;
        }
    }

    RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND hwnd)
        : m_state(std::make_unique<InputState>())
        , m_hwndQtTarget(hwnd)
        , m_hHiddenWnd(nullptr)
        , m_joy2KeySupport(joy2KeySupport)
        , m_isRegistered(false)
    {
        InputState::InitializeTables();
        InitializeApiFuncs();

        if (m_joy2KeySupport) {
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            CreateHiddenWindow();
            RegisterDevices(m_hHiddenWnd, true);
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        UnregisterDevices();
        DestroyHiddenWindow();
    }

    // =========================================================================
    // REFACTORED (R1): drainPendingMessages -- extracted from Poll()/PollAndSnapshot()
    // =========================================================================
    void RawInputWinFilter::drainPendingMessages() noexcept {
        // Safeguard: Always rescue pending raw input data BEFORE PeekMessage
        // removes WM_INPUT from the queue. PeekMessage(PM_REMOVE) makes messages
        // invisible to GetRawInputBuffer — if we don't read first, data is lost.
        // When called after processRawInputBatched already ran (e.g. from Poll()),
        // this returns 0 events and costs only one syscall.
        if (m_state && !m_joy2KeySupport) {
            m_state->processRawInputBatched();
        }

        MSG msg;
        if (LIKELY(WinInternal::fnNtUserPeekMessage != nullptr)) {
            while (WinInternal::fnNtUserPeekMessage(&msg, m_hHiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE, FALSE)) {}
        }
        else {
            while (PeekMessageW(&msg, m_hHiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {}
        }
    }

    void RawInputWinFilter::Poll() {
        if (m_joy2KeySupport) return;

        m_state->processRawInputBatched();
        drainPendingMessages();
    }

    // =========================================================================
    // P-22: PollAndSnapshot — drain deferred to DeferredDrain().
    //
    // processRawInputBatched (GetRawInputBuffer) reads pending raw input
    // in batch. Any WM_INPUT dispatched later (by SDL or drain) is caught
    // by HiddenWndProc → processRawInput (P-19). So data is never lost
    // regardless of when draining happens.
    //
    // Deferring the drain removes 2-10 PeekMessage syscalls from the
    // latency-critical input→RunFrame path.
    // =========================================================================
    void RawInputWinFilter::PollAndSnapshot(
        FrameHotkeyState& outHk, int& outMouseX, int& outMouseY)
    {
        auto* const state = m_state.get();

        if (!m_joy2KeySupport) {
            state->processRawInputBatched();
            // Drain deferred — see DeferredDrain()
        }

        state->snapshotInputFrame(outHk, outMouseX, outMouseY);
    }

    // =========================================================================
    // V2: PollAndSnapshotNoEdges — re-entrant path helper.
    //
    // Same drain/capture semantics as PollAndSnapshot, but preserves hkPrev so
    // the next outer frame still sees press edges correctly.
    // =========================================================================
    void RawInputWinFilter::PollAndSnapshotNoEdges(
        FrameHotkeyState& outHk, int& outMouseX, int& outMouseY)
    {
        auto* const state = m_state.get();

        if (!m_joy2KeySupport) {
            state->processRawInputBatched();
            // Drain deferred — see DeferredDrain()
        }

        state->snapshotInputFrameNoEdges(outHk, outMouseX, outMouseY);
    }

    // =========================================================================
    // P-22 / P-32: DeferredDrain — drain WM_INPUT queue AFTER RunFrame.
    //
    // PeekMessage(PM_REMOVE) dispatches each WM_INPUT to HiddenWndProc,
    // which calls processRawInput (P-19) — so data is captured, not lost.
    // Current path drains every frame; the old throttle experiment was retired.
    // =========================================================================
    void RawInputWinFilter::DeferredDrain() noexcept {
        if (!m_joy2KeySupport) {
            drainPendingMessages();
        }
    }

    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport == enable) return;

        UnregisterDevices();

        if (enable) {
            DestroyHiddenWindow();
            m_joy2KeySupport = true;
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            m_joy2KeySupport = false;
            CreateHiddenWindow();
            RegisterDevices(m_hHiddenWnd, true);
        }

        m_state->resetAll();             // VK + mouse buttons + hkPrev (single fence)
        m_state->resetHotkeyEdges();     // Re-sync edge detection after mode switch
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        if (m_hwndQtTarget == hwnd) return;
        m_hwndQtTarget = hwnd;
        if (m_joy2KeySupport && m_isRegistered) {
            UnregisterDevices();
            RegisterDevices(m_hwndQtTarget, false);
        }
    }

    void RawInputWinFilter::CreateHiddenWindow() {
        if (m_hHiddenWnd) return;
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MelonPrimeRawInputSink";
        RegisterClassW(&wc);

        m_hHiddenWnd = CreateWindowW(
            L"MelonPrimeRawInputSink", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, this);
    }

    void RawInputWinFilter::DestroyHiddenWindow() {
        if (m_hHiddenWnd) {
            DestroyWindow(m_hHiddenWnd);
            m_hHiddenWnd = nullptr;
        }
        UnregisterClassW(L"MelonPrimeRawInputSink", GetModuleHandle(nullptr));
    }

    // =========================================================================
    // P-19: HiddenWndProc — process raw input on dispatch.
    //
    // Problem: SDL_JoystickUpdate calls PeekMessage(NULL, ..., PM_REMOVE)
    // which dispatches ALL pending messages including WM_INPUT. Once dispatched,
    // the message is REMOVED from the queue — GetRawInputBuffer can never see it.
    //
    // Old approach (return 0): Data lost. PeekMessage already consumed the message.
    //
    // Correct approach: Read the raw input data via processRawInput(HRAWINPUT)
    // before returning. This captures the data that would otherwise be lost.
    // DefWindowProcW is NOT called, so there's no double-read.
    //
    // No race condition:
    //   - GetRawInputBuffer reads first → message gone → PeekMessage skips it
    //   - PeekMessage dispatches first → processRawInput reads it → safe
    //
    // Both paths run on the emu thread (hidden window owned by emu thread),
    // so they serialize naturally — no concurrent access issue.
    // =========================================================================
    LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_INPUT) {
            if (s_instance && s_instance->m_state) {
                s_instance->m_state->processRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RawInputWinFilter::RegisterDevices(HWND target, bool useHiddenWindow) {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;

        const DWORD flags = useHiddenWindow ? RIDEV_INPUTSINK : 0;
        rid[0].dwFlags = flags; rid[0].hwndTarget = target;
        rid[1].dwFlags = flags; rid[1].hwndTarget = target;

        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = true;
    }

    void RawInputWinFilter::UnregisterDevices() {
        if (!m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0] = { 0x01, 0x02, RIDEV_REMOVE, nullptr };
        rid[1] = { 0x01, 0x06, RIDEV_REMOVE, nullptr };
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = false;
    }

    bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
        if (!m_joy2KeySupport) return false;

        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_INPUT) {
            m_state->processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
        }
        return false;
    }

    // =========================================================================
    // R2: setHotkeyVks — added pointer+count overload for zero-allocation path.
    // =========================================================================
    void RawInputWinFilter::setHotkeyVks(int id, const UINT* vks, size_t count) {
        m_state->setHotkeyVks(id, vks, count);
    }

    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) {
        m_state->setHotkeyVks(id, vks.data(), vks.size());
    }

    void RawInputWinFilter::discardDeltas() { m_state->discardDeltas(); }
    void RawInputWinFilter::pollHotkeys(FrameHotkeyState& out) { m_state->pollHotkeys(out); }
    void RawInputWinFilter::snapshotInputFrame(FrameHotkeyState& outHk, int& outMouseX, int& outMouseY)
    {
        m_state->snapshotInputFrame(outHk, outMouseX, outMouseY);
    }
    void RawInputWinFilter::snapshotInputFrameNoEdges(FrameHotkeyState& outHk, int& outMouseX, int& outMouseY)
    {
        m_state->snapshotInputFrameNoEdges(outHk, outMouseX, outMouseY);
    }
    void RawInputWinFilter::resetAllKeys() { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() { m_state->resetMouseButtons(); }

    // P-9: Combined reset — single call replaces resetAllKeys + resetMouseButtons.
    // Route to InputState::resetAll() so the implementation and comment stay aligned.
    void RawInputWinFilter::resetAll() { m_state->resetAll(); }
    void RawInputWinFilter::resetHotkeyEdges() { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) { m_state->fetchMouseDelta(outX, outY); }

} // namespace MelonPrime
#endif
