#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>

namespace MelonPrime {

    // =========================================================================
    // Static members
    // =========================================================================
    std::atomic<int>          RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter*        RawInputWinFilter::s_instance = nullptr;
    std::once_flag            RawInputWinFilter::s_initFlag;

    NtUserMsgWaitForMultipleObjectsEx_t  RawInputWinFilter::s_fnWait = nullptr;
    PeekMessageW_t                        RawInputWinFilter::s_fnPeek = nullptr;

    // =========================================================================
    // NtPeekAdapter — PeekMessageW-compatible thunk for NtUserPeekMessage
    //
    // Bakes in bProcessSideEffects=FALSE (6th arg) to skip message hooks.
    // This eliminates per-call branching: s_fnPeek is either PeekMessageW
    // or this adapter, decided once at init time.
    // =========================================================================
    BOOL WINAPI RawInputWinFilter::NtPeekAdapter(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
    {
        return WinInternal::fnNtUserPeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, FALSE);
    }

    // =========================================================================
    // One-time API resolution — decides the function pointers for the entire
    // lifetime of the process. After this, the hot loop uses only indirect
    // calls through s_fnWait and s_fnPeek — zero conditional branches.
    // =========================================================================
    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();

            // s_fnWait: prefer NtUserMsgWaitForMultipleObjectsEx (avoids user32 shim),
            //           fall back to MsgWaitForMultipleObjectsEx (same signature).
            s_fnWait = WinInternal::fnNtUserMsgWaitForMultipleObjectsEx
                ? WinInternal::fnNtUserMsgWaitForMultipleObjectsEx
                : reinterpret_cast<NtUserMsgWaitForMultipleObjectsEx_t>(&MsgWaitForMultipleObjectsEx);

            // s_fnPeek: prefer NtUserPeekMessage via adapter (skips hooks),
            //           fall back to PeekMessageW (same 5-arg signature).
            s_fnPeek = WinInternal::fnNtUserPeekMessage
                ? &NtPeekAdapter
                : reinterpret_cast<PeekMessageW_t>(&PeekMessageW);
        });
    }

    // =========================================================================
    // Singleton lifecycle
    // =========================================================================
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

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
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
            StartInputThread();
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        StopInputThread();
        if (m_joy2KeySupport) {
            UnregisterDevices();
        }
    }

    // =========================================================================
    // Dedicated Input Thread — Event-Driven (Joy2Key OFF)
    //
    // Latency chain:
    //   HID device → OS raw input buffer → GetRawInputBuffer → atomic store
    //   → (EmuThread atomic load) → game logic
    //
    // Hot loop invariant: ZERO conditional branches for API dispatch.
    //   s_fnWait and s_fnPeek are pre-resolved function pointers.
    //   The only branches are the wait result check and the peek drain loop.
    // =========================================================================

    void RawInputWinFilter::StartInputThread() {
        if (m_inputThread.joinable()) return;

        m_stopThread.store(false, std::memory_order_release);
        m_threadReady.store(false, std::memory_order_release);

        if (!m_hStopEvent) {
            m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        }
        else {
            ResetEvent(m_hStopEvent);
        }

        m_inputThread = std::thread(&RawInputWinFilter::InputThreadProc, this);

        // Spin-wait for thread init (sub-ms)
        while (!m_threadReady.load(std::memory_order_acquire)) {
            _mm_pause();
        }
    }

    void RawInputWinFilter::StopInputThread() {
        if (!m_inputThread.joinable()) return;

        m_stopThread.store(true, std::memory_order_release);
        if (m_hStopEvent) SetEvent(m_hStopEvent);

        m_inputThread.join();

        if (m_hStopEvent) {
            CloseHandle(m_hStopEvent);
            m_hStopEvent = nullptr;
        }
    }

    void RawInputWinFilter::InputThreadProc() {
        // --- Thread-local init ---
        CreateHiddenWindow();
        RegisterDevices(m_hHiddenWnd, true);  // RIDEV_INPUTSINK
        m_threadReady.store(true, std::memory_order_release);

        constexpr DWORD kWakeMask = QS_RAWINPUT;
        MSG msg;

        // =================================================================
        // Hot loop — event-driven, 0% CPU when idle
        //
        //  s_fnWait : NtUserMsgWaitForMultipleObjectsEx or MsgWaitForMultipleObjectsEx
        //  s_fnPeek : NtPeekAdapter (hook bypass) or PeekMessageW
        //
        //  Both resolved once at startup. No if/else in this loop.
        // =================================================================
        while (!m_stopThread.load(std::memory_order_relaxed)) {

            // 1. Sleep until raw input arrives or stop event fires.
            DWORD ret = s_fnWait(1, &m_hStopEvent, INFINITE, kWakeMask, MWMO_INPUTAVAILABLE);
            if (ret == WAIT_OBJECT_0 || ret == WAIT_FAILED) {
                break;
            }

            // 2. Batch read from OS raw input buffer (main harvest).
            m_state->processRawInputBatched();

            // 3. Drain WM_INPUT from message queue.
            //    Data already consumed by step 2. Just remove messages to
            //    prevent queue overflow. s_fnPeek resolved once — no branch.
            while (s_fnPeek(&msg, m_hHiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {}

            // 4. WM_QUIT check.
            if (s_fnPeek(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
                m_stopThread.store(true, std::memory_order_relaxed);
                break;
            }

            // 5. Safety-net re-read.
            //    New input may have arrived during step 3's queue drain.
            //    processRawInputBatched is near-zero-cost on empty buffer.
            m_state->processRawInputBatched();
        }

        UnregisterDevices();
        DestroyHiddenWindow();
    }

    // =========================================================================
    // Poll() — EmuThread entry point (every frame)
    // =========================================================================
    void RawInputWinFilter::Poll() {
        if (m_joy2KeySupport) return;

        if (!CheckFocused()) {
            m_state->discardDeltas();
        }
    }

    bool RawInputWinFilter::CheckFocused() const {
        HWND foreground = ::GetForegroundWindow();
        if (!foreground) return false;
        if (foreground == m_hwndQtTarget) return true;

        HWND rootFg     = ::GetAncestor(foreground, GA_ROOTOWNER);
        HWND rootTarget = ::GetAncestor(m_hwndQtTarget, GA_ROOTOWNER);
        return (rootFg == rootTarget && rootTarget != nullptr);
    }

    // =========================================================================
    // Mode switching
    // =========================================================================
    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport == enable) return;

        if (enable) {
            StopInputThread();
            m_joy2KeySupport = true;
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            UnregisterDevices();
            m_joy2KeySupport = false;
            StartInputThread();
        }

        m_state->resetAllKeys();
        m_state->resetMouseButtons();
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        m_hwndQtTarget = hwnd;
        if (m_joy2KeySupport && m_isRegistered) {
            UnregisterDevices();
            RegisterDevices(m_hwndQtTarget, false);
        }
    }

    // =========================================================================
    // Hidden window
    // =========================================================================
    void RawInputWinFilter::CreateHiddenWindow() {
        if (m_hHiddenWnd) return;
        WNDCLASSW wc = {};
        wc.lpfnWndProc   = HiddenWndProc;
        wc.hInstance      = GetModuleHandle(nullptr);
        wc.lpszClassName  = L"MelonPrimeInputThread";
        RegisterClassW(&wc);

        m_hHiddenWnd = CreateWindowW(
            L"MelonPrimeInputThread", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, this);
    }

    void RawInputWinFilter::DestroyHiddenWindow() {
        if (m_hHiddenWnd) {
            DestroyWindow(m_hHiddenWnd);
            m_hHiddenWnd = nullptr;
        }
        UnregisterClassW(L"MelonPrimeInputThread", GetModuleHandle(nullptr));
    }

    LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // =========================================================================
    // Device registration
    // =========================================================================
    void RawInputWinFilter::RegisterDevices(HWND target, bool useInputSink) {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;

        const DWORD flags = useInputSink ? RIDEV_INPUTSINK : 0;
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

    // =========================================================================
    // Qt native event filter (Joy2Key ON mode)
    // =========================================================================
    bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
        if (!m_joy2KeySupport) return false;

        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_INPUT) {
            m_state->processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
        }
        return false;
    }

    // =========================================================================
    // Delegated InputState methods
    // =========================================================================
    void RawInputWinFilter::discardDeltas()                           { m_state->discardDeltas(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    void RawInputWinFilter::pollHotkeys(FrameHotkeyState& out)       { m_state->pollHotkeys(out); }
    void RawInputWinFilter::resetAllKeys()                            { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons()                       { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges()                        { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY)    { m_state->fetchMouseDelta(outX, outY); }

} // namespace MelonPrime
#endif
