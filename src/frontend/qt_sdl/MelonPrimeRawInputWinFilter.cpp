#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>
#include <chrono>

namespace MelonPrime {

    std::atomic<int> RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;

    RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, void* windowHandle) {
        if (s_refCount.fetch_add(1) == 0) {
            s_instance = new RawInputWinFilter(joy2KeySupport, static_cast<HWND>(windowHandle));
        }
        else if (s_instance) {
            s_instance->setRawInputTarget(static_cast<HWND>(windowHandle));
            s_instance->setJoy2KeySupport(joy2KeySupport);
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

        if (m_joy2KeySupport) {
            // Joy2Keyモード: メインスレッドでウィンドウ登録
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            // Directモード: 入力専用スレッドを起動
            StartWorkerThread();
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        StopWorkerThread(); // 先にスレッドを止める
        UnregisterDevices();
    }

    // ========================================================================
    // Thread Management
    // ========================================================================

    void RawInputWinFilter::StartWorkerThread() {
        if (m_workerThread.joinable()) return;

        m_stopThread = false;
        m_threadInitialized = false;

        m_workerThread = std::thread(&RawInputWinFilter::InputThreadProc, this);

        // スレッド内でウィンドウ作成とデバイス登録が完了するまで待機
        std::unique_lock<std::mutex> lock(m_startupMutex);
        m_startupCv.wait(lock, [this] { return m_threadInitialized; });
    }

    void RawInputWinFilter::StopWorkerThread() {
        if (!m_workerThread.joinable()) return;

        m_stopThread = true;
        // スレッドがSleep中かもしれないので、必要ならWakeさせる処理を入れるが、
        // 今回は1ms Sleepのループなので最大1ms遅延で停止する。

        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    void RawInputWinFilter::InputThreadProc() {
        // 1. スレッドに紐づく隠しウィンドウを作成
        CreateHiddenWindow();

        // 2. デバイス登録 (このスレッドのメッセージキューに入るようにする)
        RegisterDevices(m_hHiddenWnd, true);

        // 3. 初期化完了を通知
        {
            std::lock_guard<std::mutex> lock(m_startupMutex);
            m_threadInitialized = true;
        }
        m_startupCv.notify_all();

        MSG msg;

        // ★修正ポイント: メッセージループの順序変更
        while (!m_stopThread) {

            // A. 先に RawInput バッファを読み取る (最優先)
            // これが WM_INPUT をキューから消費しながらデータを更新します
            m_state->processRawInputBatched();

            // B. その他のメッセージ (WM_QUITなど) を処理する
            // WM_INPUT 以外に残っているメッセージがあればここで処理
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    m_stopThread = true;
                }
                // もし万が一 GetRawInputBuffer が取りこぼした WM_INPUT があっても
                // ここで Dispatch されれば (DefWindowProc行きで) 無視されるだけなので安全
                DispatchMessageW(&msg);
            }

            // C. CPU負荷対策 (1ms休止)
            // ポーリング頻度は 1000Hz 相当確保されるため、Aim操作に影響はありません
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // 終了処理
        UnregisterDevices();
        DestroyHiddenWindow();
    }

    // ========================================================================
    // Device & Window Helper
    // ========================================================================

    void RawInputWinFilter::CreateHiddenWindow() {
        // 既に作成済みなら何もしない (スレッドセーフティのためチェック)
        if (m_hHiddenWnd) return;

        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MelonPrimeInputThread";
        RegisterClassW(&wc);

        // HWND_MESSAGE: メッセージ専用ウィンドウを作成
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
            m_joy2KeySupport = enable;

            // モード切り替え時は全リセット
            UnregisterDevices();

            if (m_joy2KeySupport) {
                // Direct(Thread) -> Joy2Key
                StopWorkerThread();
                RegisterDevices(m_hwndQtTarget, false);
            }
            else {
                // Joy2Key -> Direct(Thread)
                StartWorkerThread();
            }
        }
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        if (m_hwndQtTarget != hwnd) {
            m_hwndQtTarget = hwnd;
            // Joy2Keyモードの場合のみ、ターゲット変更時に再登録が必要
            if (m_joy2KeySupport && m_isRegistered) {
                UnregisterDevices();
                RegisterDevices(m_hwndQtTarget, false);
            }
        }
    }

    void RawInputWinFilter::RegisterDevices(HWND target, bool isThreaded) {
        if (m_isRegistered) return; // 念のため

        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage = 0x02; // Mouse
        rid[1].usUsagePage = 0x01;
        rid[1].usUsage = 0x06; // Keyboard

        if (isThreaded) {
            // スレッドモード: RIDEV_INPUTSINK を使用して、バックグラウンドでも受け取れるようにする
            // (HiddenWindowはフォーカスを持てないため INPUTSINK が必須)
            rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = target;
            rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = target;
        }
        else {
            // Joy2Key (Main Thread) モード: 通常の登録
            rid[0].dwFlags = 0; rid[0].hwndTarget = target;
            rid[1].dwFlags = 0; rid[1].hwndTarget = target;
        }

        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = true;
    }

    void RawInputWinFilter::UnregisterDevices() {
        // 登録解除は「登録したスレッド」で行うのが安全だが、
        // RIDEV_REMOVE はターゲットウィンドウが NULL でも機能するため、ここでの呼び出しで概ね動作する。
        // ただし、スレッドモードの場合は InputThreadProc の最後で呼ぶのが定石。
        // ここでは setJoy2KeySupport からの呼び出し用に強制解除を行う。

        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;
        rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
        m_isRegistered = false;
    }

    // =============================================================================
    // Poll Interface
    // =============================================================================
    void RawInputWinFilter::poll() {
        // Threadモードではスレッドが自動更新するため何もしない。
        // Joy2KeyモードではQtイベントで更新されるため何もしない。
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

    // その他の転送関数
    void RawInputWinFilter::discardDeltas() { m_state->discardDeltas(); }
    void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks) { m_state->setHotkeyVks(id, vks); }
    void RawInputWinFilter::pollHotkeys(FrameHotkeyState& out) { m_state->pollHotkeys(out); }
    void RawInputWinFilter::resetAllKeys() { m_state->resetAllKeys(); }
    void RawInputWinFilter::resetMouseButtons() { m_state->resetMouseButtons(); }
    void RawInputWinFilter::resetHotkeyEdges() { m_state->resetHotkeyEdges(); }
    void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) { m_state->fetchMouseDelta(outX, outY); }

} // namespace MelonPrime
#endif