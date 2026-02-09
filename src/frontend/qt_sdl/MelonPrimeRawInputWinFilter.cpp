#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <QCoreApplication>
#include <chrono>

namespace MelonPrime {

    std::atomic<int> RawInputWinFilter::s_refCount{ 0 };
    RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;

    // 初期値は標準API (user32.dll) に設定
    NtUserMsgWaitForMultipleObjectsEx_t RawInputWinFilter::s_fnWait = ::MsgWaitForMultipleObjectsEx;
    RawInputWinFilter::PeekMessageFunc_t RawInputWinFilter::s_fnPeek = ::PeekMessageW;
    std::once_flag RawInputWinFilter::s_initFlag;

    // NtUserPeekMessage (6引数) → PeekMessageW 互換シグネチャ (5引数) の薄いラッパー
    // call_once で s_fnPeek に一度だけセットされ、以降は間接呼び出し1回のみ
    static BOOL WINAPI NtPeekMessageWrapper(LPMSG pMsg, HWND hWnd,
        UINT wMsgFilterMin, UINT wMsgFilterMax,
        UINT wRemoveMsg) {
        return WinInternal::fnNtUserPeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg, TRUE);
    }

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
        m_hStopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // テーブル初期化 (ここで WinInternal::ResolveNtApis() が呼ばれる)
        InputState::InitializeTables();

        // ★ API関数ポインタの最適化 (初回のみ実行)
        InitializeApiFuncs();

        if (m_joy2KeySupport) {
            RegisterDevices(m_hwndQtTarget, false);
        }
        else {
            StartWorkerThread();
        }
    }

    RawInputWinFilter::~RawInputWinFilter() {
        StopWorkerThread();
        UnregisterDevices();
        if (m_hStopEvent) {
            CloseHandle(m_hStopEvent);
            m_hStopEvent = nullptr;
        }
    }

    // ★ 最適な関数ポインタを選択するロジック (初回のみ実行)
    void RawInputWinFilter::InitializeApiFuncs() {
        std::call_once(s_initFlag, []() {
            // WinInternal::ResolveNtApis() は InputState::InitializeTables() で呼ばれている前提
            // カーネル直通APIが使えるなら差し替える
            if (WinInternal::fnNtUserMsgWaitForMultipleObjectsEx) {
                s_fnWait = WinInternal::fnNtUserMsgWaitForMultipleObjectsEx;
            }
            if (WinInternal::fnNtUserPeekMessage) {
                s_fnPeek = NtPeekMessageWrapper;
            }
            });
    }

    void RawInputWinFilter::StartWorkerThread() {
        if (m_workerThread.joinable()) return;
        m_stopThread = false;
        m_threadInitialized = false;
        if (m_hStopEvent) ResetEvent(m_hStopEvent);

        m_workerThread = std::thread(&RawInputWinFilter::InputThreadProc, this);

        std::unique_lock<std::mutex> lock(m_startupMutex);
        m_startupCv.wait(lock, [this] { return m_threadInitialized; });
    }

    void RawInputWinFilter::StopWorkerThread() {
        if (!m_workerThread.joinable()) return;
        m_stopThread = true;
        if (m_hStopEvent) SetEvent(m_hStopEvent);
        if (m_workerThread.joinable()) m_workerThread.join();
    }

    void RawInputWinFilter::InputThreadProc() {
        CreateHiddenWindow();
        RegisterDevices(m_hHiddenWnd, true);

        {
            std::lock_guard<std::mutex> lock(m_startupMutex);
            m_threadInitialized = true;
        }
        m_startupCv.notify_all();

        // 初回の吸い出し — RegisterDevices～ここまでの間に溜まった入力を回収
        m_state->processRawInputBatched();

        // 待機マスク: WM_INPUT (QS_RAWINPUT) + WM_QUIT (QS_POSTMESSAGE) のみ
        // QS_ALLINPUT だと WM_PAINT, WM_TIMER 等で無駄に起床してしまう
        constexpr DWORD kWakeMask = QS_RAWINPUT | QS_POSTMESSAGE;

        MSG msg;

        while (!m_stopThread.load(std::memory_order_relaxed)) {
            // 1. イベント待機 (CPU負荷 0%)
            //    - m_hStopEvent がシグナル → 終了
            //    - kWakeMask に該当するメッセージ到着 → 処理へ
            DWORD ret = s_fnWait(1, &m_hStopEvent, INFINITE, kWakeMask, MWMO_INPUTAVAILABLE);

            if (ret == WAIT_OBJECT_0 || ret == WAIT_FAILED) {
                break;
            }

            // 2. バッファ一括読み取り (メイン)
            //    GetRawInputBuffer で溜まっているRawInputを最速回収
            m_state->processRawInputBatched();

            // 3. WM_INPUT メッセージの消化
            //    processRawInputBatched はバッファから直接読むため、
            //    メッセージキューに残った WM_INPUT を除去する必要がある。
            //    フィルタ範囲を WM_INPUT に限定し、DispatchMessageW を回避。
            //    s_fnPeek は InitializeApiFuncs() で1回だけ解決済み (分岐なし)。
            // データは processRawInputBatched で処理済み。
            // PM_REMOVE でキューから WM_INPUT を排出するだけでよい。
            while (s_fnPeek(&msg, m_hHiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {}

            // 4. WM_QUIT チェック
            //    PostQuitMessage() による終了通知を検出
            if (s_fnPeek(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
                m_stopThread.store(true, std::memory_order_relaxed);
                break;
            }

            // 5. 追っかけ読み取り (セーフティネット)
            //    ステップ3のメッセージ消化中に新たな入力が到着した場合に備え、
            //    もう一度バッファを確認する。
            //    processRawInputBatched は空バッファ時にほぼノーコストなので常に呼ぶ。
            m_state->processRawInputBatched();
        }

        UnregisterDevices();
        DestroyHiddenWindow();
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
            m_joy2KeySupport = enable;
            UnregisterDevices();
            if (m_joy2KeySupport) {
                StopWorkerThread();
                RegisterDevices(m_hwndQtTarget, false);
            }
            else {
                StartWorkerThread();
            }
        }
    }

    void RawInputWinFilter::setRawInputTarget(HWND hwnd) {
        if (m_hwndQtTarget != hwnd) {
            m_hwndQtTarget = hwnd;
            if (m_joy2KeySupport && m_isRegistered) {
                UnregisterDevices();
                RegisterDevices(m_hwndQtTarget, false);
            }
        }
    }

    void RawInputWinFilter::RegisterDevices(HWND target, bool isThreaded) {
        if (m_isRegistered) return;
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02;
        rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06;

        if (isThreaded) {
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