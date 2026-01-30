// MelonPrimeRawInputWinFilter.cpp
#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>
#include <process.h> // for _beginthreadex if needed, but CreateThread is fine

//=====================================================
// LUT Init
//=====================================================
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];
static struct RawLutInit {
    RawLutInit() {
        for (int i = 0; i < 1024; ++i) {
            uint8_t d = 0, u = 0;
            if (i & RI_MOUSE_BUTTON_1_DOWN) d |= 0x01;
            if (i & RI_MOUSE_BUTTON_1_UP)   u |= 0x01;
            if (i & RI_MOUSE_BUTTON_2_DOWN) d |= 0x02;
            if (i & RI_MOUSE_BUTTON_2_UP)   u |= 0x02;
            if (i & RI_MOUSE_BUTTON_3_DOWN) d |= 0x04;
            if (i & RI_MOUSE_BUTTON_3_UP)   u |= 0x04;
            if (i & RI_MOUSE_BUTTON_4_DOWN) d |= 0x08;
            if (i & RI_MOUSE_BUTTON_4_UP)   u |= 0x08;
            if (i & RI_MOUSE_BUTTON_5_DOWN) d |= 0x10;
            if (i & RI_MOUSE_BUTTON_5_UP)   u |= 0x10;
            RawInputWinFilter::s_btnLut[i] = { d, u };
        }
    }
} s_lutInit;

//=====================================================
// Constructor / Destructor
//=====================================================
RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
    : m_mainHwnd(mainHwnd)
{
    // 初期化
    for (auto& a : m_state.vkDown) a.store(0);
    m_state.mouseButtons.store(0);
    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));

    // 初期モード設定
    m_joy2KeySupport.store(joy2KeySupport);

    if (joy2KeySupport) {
        // ON: メインウィンドウに登録 (Qt経由)
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        // OFF: 独自スレッド起動 (低遅延)
        startThreadIfNeeded();
    }
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopThreadIfRunning();
    // スレッドが止まった後にデストラクタが走るので安全
}

//=====================================================
// モード切り替え (元コードのロジックを再現+最適化)
//=====================================================
void RawInputWinFilter::setJoy2KeySupport(bool enable)
{
    const bool prev = m_joy2KeySupport.load(std::memory_order_relaxed);
    if (prev == enable) return;

    m_joy2KeySupport.store(enable, std::memory_order_relaxed);

    if (enable) {
        // OFF -> ON: 独自スレッド停止 -> メインウィンドウ登録
        stopThreadIfRunning();
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        // ON -> OFF: 独自スレッド開始
        // (注: RawInputは最後に登録したウィンドウが優先されるため、
        //  スレッド側で隠しウィンドウに登録すればメイン側の受信は止まる)
        startThreadIfNeeded();
    }
}

void RawInputWinFilter::setRawInputTarget(HWND hwnd)
{
    // メインウィンドウハンドルの更新
    if (m_mainHwnd != hwnd) {
        m_mainHwnd = hwnd;
        // もし現在 ON モードなら、新しいウィンドウに対して再登録
        if (m_joy2KeySupport.load(std::memory_order_relaxed)) {
            if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
        }
    }
}

//=====================================================
// RawInput 登録
//=====================================================
void RawInputWinFilter::registerRawToTarget(HWND targetHwnd)
{
    if (!targetHwnd) return;

    RAWINPUTDEVICE rid[2];
    // Mouse
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK; // バックグラウンドでも受信
    rid[0].hwndTarget = targetHwnd;

    // Keyboard
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = targetHwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

//=====================================================
// Qt Native Filter (ON の時のみ)
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    // OFF のときはスレッド側で処理しているので、こちらは無視
    if (!m_joy2KeySupport.load(std::memory_order_relaxed)) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));

    // Qtにイベントを流す（消費しない）
    return false;
}

//=====================================================
// 最速パス：RawInput 処理コア (最適化版)
//=====================================================
FORCE_INLINE void RawInputWinFilter::processRawInput(HRAWINPUT hRaw) noexcept
{
    // スタックバッファの代わりにメンバ変数 m_rawBuf を使用（アロケーション回避）
    // この関数は「メインスレッド(ON)」または「独自スレッド(OFF)」のどちらか一方からしか
    // 呼ばれないため、ロックなしで共有バッファを使って問題ない。
    UINT size = sizeof(m_rawBuf);

    if (GetRawInputData(hRaw, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    const DWORD type = raw->header.dwType;

    // ---------------- Mouse ----------------
    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;

        // 移動量 (Relative)
        if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            const LONG dx_ = m.lLastX;
            const LONG dy_ = m.lLastY;
            if (dx_ | dy_) {
                m_mouseDeltaX.fetch_add((int)dx_, std::memory_order_relaxed);
                m_mouseDeltaY.fetch_add((int)dy_, std::memory_order_relaxed);
            }
        }

        // ボタン
        const USHORT f = m.usButtonFlags & 0x03FF;
        if (f) {
            const auto& lut = s_btnLut[f];
            if (lut.downBits | lut.upBits) {
                uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt = (cur | lut.downBits) & ~lut.upBits;
                if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
            }
        }
        return;
    }

    // ---------------- Keyboard ----------------
    if (type == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;

        if (vk == 0 || vk >= 255) return;

        // 左右補正
        if (vk == VK_SHIFT)   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
        else if (vk == VK_CONTROL) vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
        else if (vk == VK_MENU)    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;

        bool isDown = !(flags & RI_KEY_BREAK);
        setVkBit(vk, isDown);
        return;
    }
}

//=====================================================
// 独自スレッド制御 (低遅延モード用)
//=====================================================
void RawInputWinFilter::startThreadIfNeeded()
{
    if (m_runThread.load(std::memory_order_relaxed)) return;

    m_runThread.store(true, std::memory_order_relaxed);
    m_hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);

    // スレッド優先度を上げて、メインスレッドが重くても入力だけは処理させる
    if (m_hThread) {
        SetThreadPriority(m_hThread, THREAD_PRIORITY_HIGHEST);
    }
}

void RawInputWinFilter::stopThreadIfRunning()
{
    if (!m_runThread.load(std::memory_order_relaxed)) return;

    m_runThread.store(false, std::memory_order_relaxed);

    // スレッド内のメッセージループを終了させるために WM_CLOSE を送る
    if (m_hiddenWnd) {
        PostMessage(m_hiddenWnd, WM_CLOSE, 0, 0);
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 1000); // タイムアウト付き待機
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    m_hiddenWnd = nullptr;
}

DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    static_cast<RawInputWinFilter*>(param)->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    // メッセージ専用ウィンドウを作成 (HWND_MESSAGE)
    // これにより、画面を持たない軽量なウィンドウでメッセージを受け取る
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("MelonPrimeRawInputWorker");
    RegisterClassEx(&wc);

    m_hiddenWnd = CreateWindowEx(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    if (!m_hiddenWnd) {
        return;
    }

    // この隠しウィンドウに対して RawInput を登録する
    // これにより、このスレッドのメッセージキューに WM_INPUT が飛んでくるようになる
    registerRawToTarget(m_hiddenWnd);

    // メッセージループ
    MSG msg;
    // GetMessage はメッセージが来るまでスリープするためCPU負荷は低い
    while (m_runThread.load(std::memory_order_relaxed))
    {
        BOOL ret = GetMessage(&msg, nullptr, 0, 0);
        if (ret == -1 || ret == 0) break;

        if (msg.message == WM_INPUT) {
            processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
        }
        else if (msg.message == WM_CLOSE) {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyWindow(m_hiddenWnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    m_hiddenWnd = nullptr;
}

//=====================================================
// その他 API (互換性維持)
//=====================================================
void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks)
{
    if (id < 0 || (size_t)id >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[id];
    m.vkMask[0] = m.vkMask[1] = m.vkMask[2] = m.vkMask[3] = 0;
    m.mouseMask = 0;
    m.hasMask = false;
    if (vks.empty()) return;
    for (UINT k : vks) {
        if (k >= VK_LBUTTON && k <= VK_XBUTTON2) {
            int bit = -1;
            switch (k) {
            case VK_LBUTTON: bit = 0; break;
            case VK_RBUTTON: bit = 1; break;
            case VK_MBUTTON: bit = 2; break;
            case VK_XBUTTON1: bit = 3; break;
            case VK_XBUTTON2: bit = 4; break;
            }
            if (bit >= 0) m.mouseMask |= (1 << bit);
        }
        else if (k < 256) {
            m.vkMask[k >> 6] |= (1ULL << (k & 63));
        }
    }
    m.hasMask = true;
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const HotkeyMask& m = m_hkMask[(unsigned)hk];
    if (!m.hasMask) return false;
    if (m.mouseMask && (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask)) return true;
    if (m.vkMask[0] & m_state.vkDown[0].load(std::memory_order_relaxed)) return true;
    if (m.vkMask[1] & m_state.vkDown[1].load(std::memory_order_relaxed)) return true;
    if (m.vkMask[2] & m_state.vkDown[2].load(std::memory_order_relaxed)) return true;
    if (m.vkMask[3] & m_state.vkDown[3].load(std::memory_order_relaxed)) return true;
    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    const bool prev = (m_hkPrev[idx] & bit) != 0;
    if (now != prev) {
        if (now) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
    return (now && !prev);
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    const bool prev = (m_hkPrev[idx] & bit) != 0;
    if (now != prev) {
        if (now) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
    return (!now && prev);
}

void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) {
    outX = m_mouseDeltaX.exchange(0, std::memory_order_relaxed);
    outY = m_mouseDeltaY.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas() {
    m_mouseDeltaX.store(0, std::memory_order_relaxed);
    m_mouseDeltaY.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetAllKeys() {
    for (auto& a : m_state.vkDown) a.store(0, std::memory_order_relaxed);
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    memset(m_hkPrev, 0, sizeof(m_hkPrev));
}

void RawInputWinFilter::resetMouseButtons() {
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges() {
    for (size_t i = 0; i < kMaxHotkeyId; ++i) {
        bool down = hotkeyDown((int)i);
        const size_t idx = i >> 6;
        const uint64_t bit = 1ULL << (i & 63);
        if (down) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
}

void RawInputWinFilter::clearAllBindings() {
    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));
}

#endif // _WIN32