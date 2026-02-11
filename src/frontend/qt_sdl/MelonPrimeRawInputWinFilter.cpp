#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>

namespace MelonPrime {

    // =========================================================================
    // Static members
    // =========================================================================
    std::atomic<int>    RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
    std::once_flag      RawInputWinFilter::s_initFlag;
    PeekMessageW_t      RawInputWinFilter::s_fnPeek = nullptr;

    // =========================================================================
    // NtPeekAdapter — PeekMessageW-compatible thunk for NtUserPeekMessage
    //
    // Bakes in bProcessSideEffects=FALSE (6th arg) to skip message hooks.
    // Resolved once at init — s_fnPeek points here or to PeekMessageW.
    // =========================================================================
    BOOL WINAPI RawInputWinFilter::NtPeekAdapter(
        LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
    {
        return WinInternal::fnNtUserPeekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, FALSE);
    }

    // =========================================================================
    // One-time API resolution
    // =========================================================================
    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();

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
            CreateHiddenWindow();
            RegisterDevices(m_hHiddenWnd, true);
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        UnregisterDevices();
        DestroyHiddenWindow();
    }

    // =========================================================================
    // Poll() — Frame-synchronous update (called from RunFrameHook)
    //
    // Hot path structure (Joy2Key OFF):
    //
    //   1. processRawInputBatched()
    //      Main harvest — GetRawInputBuffer reads all pending raw input
    //      directly from the OS kernel buffer. Fastest path available.
    //
    //   2. s_fnPeek drain WM_INPUT
    //      The batch read consumed the data, but corresponding WM_INPUT
    //      messages still sit in the message queue. Remove them to prevent
    //      queue overflow. s_fnPeek is pre-resolved — no branch.
    //      Filter range limited to WM_INPUT; DispatchMessageW avoided.
    //
    //   3. processRawInputBatched() (safety-net)
    //      New input may have arrived during step 2's queue drain.
    //      processRawInputBatched returns instantly on empty buffer
    //      (single GetRawInputBuffer call returns 0 → early exit).
    //
    // Total API dispatch branches in hot path: ZERO.
    // s_fnPeek resolved once at InitializeApiFuncs().
    // =========================================================================
    void RawInputWinFilter::Poll() {
        if (m_joy2KeySupport) return;

        // 1. Batch read (main harvest)
        m_state->processRawInputBatched();

        // 2. Drain WM_INPUT from queue (data already consumed above)
        MSG msg;
        while (s_fnPeek(&msg, m_hHiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {}

        // 3. Safety-net re-read (catches arrivals during drain)
        m_state->processRawInputBatched();
    }

    // =========================================================================
    // Mode switching
    // =========================================================================
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
    // Hidden window — message sink for RIDEV_INPUTSINK
    // =========================================================================
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

    LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // =========================================================================
    // Device registration
    // =========================================================================
    void RawInputWinFilter::RegisterDevices(HWND target, bool useHiddenWindow) {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02; // Mouse
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06; // Keyboard

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
    void RawInputWinFilter::discardDeltas() { m_state->discardDeltas(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    void RawInputWinFilter::pollHotkeys(FrameHotkeyState& out) { m_state->pollHotkeys(out); }
    void RawInputWinFilter::resetAllKeys() { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges() { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) { m_state->fetchMouseDelta(outX, outY); }

} // namespace MelonPrime
#endif