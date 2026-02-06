#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeInputState.h"
#include "MelonPrimeWinInternal.h"
#include <QCoreApplication>

namespace MelonPrime {

    std::atomic<int> RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;

    RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, void* windowHandle) {
        if (s_refCount.fetch_add(1) == 0) {
            s_instance = new RawInputWinFilter(joy2KeySupport, static_cast<HWND>(windowHandle));
        }
        else if (s_instance) {
            s_instance->setJoy2KeySupport(joy2KeySupport);
            s_instance->setRawInputTarget(static_cast<HWND>(windowHandle));
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
        CreateHiddenWindow();
        RegisterDevices();
    }

    RawInputWinFilter::~RawInputWinFilter() {
        UnregisterDevices();
        DestroyHiddenWindow();
    }

    void RawInputWinFilter::CreateHiddenWindow() {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MelonPrimeInputHost";
        RegisterClassW(&wc);
        m_hHiddenWnd = CreateWindowW(L"MelonPrimeInputHost", L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, this);
    }

    void RawInputWinFilter::DestroyHiddenWindow() {
        if (m_hHiddenWnd) {
            DestroyWindow(m_hHiddenWnd);
            m_hHiddenWnd = nullptr;
        }
        UnregisterClassW(L"MelonPrimeInputHost", GetModuleHandle(nullptr));
    }

    LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport != enable) {
            m_joy2KeySupport = enable;
            UnregisterDevices();
            RegisterDevices();
        }
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        if (m_hwndQtTarget != hwnd) {
            m_hwndQtTarget = hwnd;
            if (m_joy2KeySupport && m_isRegistered) {
                UnregisterDevices();
                RegisterDevices();
            }
        }
    }

    void RawInputWinFilter::RegisterDevices() {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage = 0x02;
        rid[1].usUsagePage = 0x01;
        rid[1].usUsage = 0x06;

        if (m_joy2KeySupport) {
            rid[0].dwFlags = 0; rid[0].hwndTarget = m_hwndQtTarget;
            rid[1].dwFlags = 0; rid[1].hwndTarget = m_hwndQtTarget;
        }
        else {
            rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = m_hHiddenWnd;
            rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = m_hHiddenWnd;
        }
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = true;
    }

    void RawInputWinFilter::UnregisterDevices() {
        if (!m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;
        rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = false;
    }

    // =============================================================================
    // Poll (Direct Polling) - 最速版
    // =============================================================================
    void RawInputWinFilter::poll() {
        if (m_joy2KeySupport) return;

        // NtUserGetRawInputBuffer を呼んだ時点で、
        // 読み取られたメッセージはキューから「自動的に」削除されます。
        // なので掃除処理(Drain)は不要です。
        m_state->processRawInputBatched();
    }

    bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
        if (!m_joy2KeySupport) return false;
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_INPUT) {
            HRAWINPUT hRaw = reinterpret_cast<HRAWINPUT>(msg->lParam);
            m_state->processRawInput(hRaw);
        }
        return false;
    }

    void RawInputWinFilter::discardDeltas() { m_state->discardDeltas(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    void RawInputWinFilter::pollHotkeys(FrameHotkeyState& out) { m_state->pollHotkeys(out); }
    void RawInputWinFilter::resetAllKeys() { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges() { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) { m_state->fetchMouseDelta(outX, outY); }

} // namespace MelonPrime
#endif