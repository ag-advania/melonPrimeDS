#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>

namespace MelonPrime {

    std::atomic<int> RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
    NtUserGetRawInputBuffer_t RawInputWinFilter::s_fnGetRawInputBuffer = ::GetRawInputBuffer;
    std::once_flag RawInputWinFilter::s_initFlag;

    RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, void* windowHandle) {
        if (s_refCount.fetch_add(1) == 0) {
            s_instance = new RawInputWinFilter(joy2KeySupport, static_cast<HWND>(windowHandle));
        }
        else if (s_instance) {
            // 既存インスタンスの設定更新
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

        // EmuThread上で実行される前提で、即座に隠しウィンドウを作成
        CreateHiddenWindow();

        if (m_joy2KeySupport) {
            // Joy2Keyモード: Qtのメインウィンドウにアタッチ (メインスレッド処理)
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            // RawInput(低遅延)モード: 隠しウィンドウにアタッチ (EmuThread処理)
            RegisterDevices(m_hHiddenWnd, true);
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        UnregisterDevices();
        DestroyHiddenWindow();
    }

    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();
            if (WinInternal::fnNtUserGetRawInputBuffer) {
                s_fnGetRawInputBuffer = WinInternal::fnNtUserGetRawInputBuffer;
            }
            });
    }

    // ★ 修正版 Poll(): 順序を「読み取り」→「掃除」に変更
    void RawInputWinFilter::Poll() {
        if (m_joy2KeySupport) return;

        // 1. バッファ読み取り (重要: PeekMessageより先に！)
        // 先にOSのバッファから入力を吸い上げます。
        // これを後回しにすると、下のPeekMessageがWM_INPUTを消してしまうため、入力が取れなくなります。
        m_state->processRawInputBatched();

        // 2. フォーカスチェック
        // 吸い上げた入力を使うべきか判定します。
        bool isFocused = false;
        HWND foreground = ::GetForegroundWindow();

        if (foreground) {
            if (foreground == m_hwndQtTarget) {
                isFocused = true;
            }
            else {
                // OpenGLパネルなどがフォーカスを持っている場合、親ウィンドウ(QtTarget)の所有物か確認
                HWND rootForeground = ::GetAncestor(foreground, GA_ROOTOWNER);
                HWND rootTarget = ::GetAncestor(m_hwndQtTarget, GA_ROOTOWNER);
                if (rootForeground == rootTarget && rootTarget != nullptr) {
                    isFocused = true;
                }
            }
        }

        if (!isFocused) {
            // 非アクティブなら、さっき読み取った移動量などを破棄する
            m_state->discardDeltas();
        }

        // 3. メッセージポンプ (隠しウィンドウ用)
        // メッセージキューに溜まったゴミ(処理済みのWM_INPUT含む)を掃除してフリーズを防ぐ
        MSG msg;
        while (PeekMessageW(&msg, m_hHiddenWnd, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&msg);
        }
    }

    void RawInputWinFilter::CreateHiddenWindow() {
        if (m_hHiddenWnd) return;
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MelonPrimeInputThread";
        RegisterClassW(&wc);

        m_hHiddenWnd = CreateWindowW(L"MelonPrimeInputThread", L"", 0, 0, 0, 0, 0,
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

    void RawInputWinFilter::setJoy2KeySupport(bool enable) {
        if (m_joy2KeySupport != enable) {
            UnregisterDevices();
            m_joy2KeySupport = enable;

            if (m_joy2KeySupport) {
                RegisterDevices(m_hwndQtTarget, false);
            }
            else {
                RegisterDevices(m_hHiddenWnd, true);
            }
        }
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        m_hwndQtTarget = hwnd;
        if (m_joy2KeySupport && m_isRegistered) {
            UnregisterDevices();
            RegisterDevices(m_hwndQtTarget, false);
        }
    }

    void RawInputWinFilter::RegisterDevices(HWND target, bool useHiddenWindow) {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02; // Mouse
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06; // Keyboard

        if (useHiddenWindow) {
            rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = target;
            rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = target;
        }
        else {
            rid[0].dwFlags = 0; rid[0].hwndTarget = target;
            rid[1].dwFlags = 0; rid[1].hwndTarget = target;
        }
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = true;
    }

    void RawInputWinFilter::UnregisterDevices() {
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;
        rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = false;
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
}
#endif