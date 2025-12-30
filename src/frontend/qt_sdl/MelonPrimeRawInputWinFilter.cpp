#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>

//=====================================================
// ★ マウスボタン LUT（1024）
//=====================================================
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

static struct RawLutInit {
    RawLutInit() {
        for (int i = 0; i < 1024; ++i)
        {
            uint8_t d = 0, u = 0;

            if (i & 0x0001) d |= 0x01;
            if (i & 0x0004) d |= 0x02;
            if (i & 0x0010) d |= 0x04;
            if (i & 0x0040) d |= 0x08;
            if (i & 0x0100) d |= 0x10;

            if (i & 0x0002) u |= 0x01;
            if (i & 0x0008) u |= 0x02;
            if (i & 0x0020) u |= 0x04;
            if (i & 0x0080) u |= 0x08;
            if (i & 0x0200) u |= 0x10;

            RawInputWinFilter::s_btnLut[i] = { d, u };
        }
    }
} g_rawLutInit;

//=====================================================
// ★ ctor
//=====================================================
RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
{
    // hotkey edge cache（EmuThread 専用）
    for (auto& w : m_hkPrev) w = 0;

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    // dx/dy 累積（RMW回避）
    m_prodDx = 0;
    m_prodDy = 0;
    m_consPrevDx = 0;
    m_consPrevDy = 0;
    m_mouseAccPacked.store(0, std::memory_order_relaxed);

    m_targetHwnd = mainHwnd;
    m_joy2KeySupport.store(joy2KeySupport, std::memory_order_relaxed);

    // true: Qt 側（呼び出し元が installNativeEventFilter する想定）
    // false: 独自スレッド（ここで起動）
    if (!joy2KeySupport) {
        startThreadIfNeeded();
    }
    else {
        // true側は mainHwnd に登録（Qt WM_INPUT を拾う）
        registerRawToTarget(m_targetHwnd);
    }
}

//=====================================================
// ★ dtor
//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    stopThreadIfRunning();
}

//=====================================================
// ★ target HWND 更新（true側の RegisterRawInputDevices 用）
//=====================================================
void RawInputWinFilter::setRawInputTarget(HWND hwnd)
{
    m_targetHwnd = hwnd;

    // joy2KeySupport=true の時だけ即座に再登録
    if (m_joy2KeySupport.load(std::memory_order_relaxed)) {
        registerRawToTarget(m_targetHwnd);
    }
}

//=====================================================
// ★ joy2KeySupport 切り替え
//=====================================================
void RawInputWinFilter::setJoy2KeySupport(bool enable)
{
    const bool prev = m_joy2KeySupport.load(std::memory_order_relaxed);
    if (prev == enable) return;

    m_joy2KeySupport.store(enable, std::memory_order_relaxed);

    if (enable)
    {
        // false→true：独自スレッド停止 → mainHwnd 登録
        stopThreadIfRunning();
        registerRawToTarget(m_targetHwnd);
    }
    else
    {
        // true→false：独自スレッド開始（hiddenWnd 登録は thread 内）
        startThreadIfNeeded();
    }
}

//=====================================================
// ★ RawInput 登録（RIDEV_NOLEGACY / RIDEV_NOHOTKEYS は使わない）
//=====================================================
void RawInputWinFilter::registerRawToTarget(HWND targetHwnd) noexcept
{
    if (!targetHwnd) return;

    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = 0;    // 禁止フラグは使わない
    rid[0].hwndTarget = targetHwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = 0;    // 禁止フラグは使わない
    rid[1].hwndTarget = targetHwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

//=====================================================
// ★ Qt native event filter（true の時だけ最短処理）
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    // false 時は native event filter 自体を install しない想定だが、
    // 念のためここで切る
    if (!m_joy2KeySupport.load(std::memory_order_relaxed)) return false;

    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
    return false;
}

//=====================================================
// ★ 最速パス：RawInput 処理コア（64B バッファに直読み）
//=====================================================
FORCE_INLINE void RawInputWinFilter::processRawInput(HRAWINPUT hRaw) noexcept
{
    UINT size = sizeof(m_rawBuf);
    if (GetRawInputData(hRaw, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    const DWORD type = raw->header.dwType;

    //---------------- Mouse ----------------
    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;

        const int32_t dx_ = (int32_t)m.lLastX;
        const int32_t dy_ = (int32_t)m.lLastY;

        if (dx_ | dy_) {
            // 生産者ローカル累積（単一 writer 前提）
            // 負値も uint32_t として 2の補数で加算される（wrap差分で復元できる）
            m_prodDx += (uint32_t)dx_;
            m_prodDy += (uint32_t)dy_;

            // 共有は store 1回だけ（RMWなし）
            m_mouseAccPacked.store(PackU32U32(m_prodDx, m_prodDy), std::memory_order_relaxed);
        }

        const USHORT f = m.usButtonFlags;
        if (!(f & kAllMouseBtnMask)) return;

        const BtnLutEntry lut = s_btnLut[f & 0x3FF];

        const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        const uint8_t nxt = (uint8_t)((cur | lut.down) & (uint8_t)~lut.up);
        m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
        return;
    }

    //---------------- Keyboard ----------------
    if (type == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;

        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;
        const bool isUp = (flags & RI_KEY_BREAK) != 0;

        switch (vk) {
        case VK_SHIFT:   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX); break;
        case VK_CONTROL: vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL; break;
        case VK_MENU:    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU; break;
        default: break;
        }

        setVkBit(vk, !isUp);
        return;
    }
}

//=====================================================
// ★ 独自スレッド start/stop
//=====================================================
void RawInputWinFilter::startThreadIfNeeded()
{
    if (runThread.load(std::memory_order_relaxed)) return;

    runThread.store(true, std::memory_order_relaxed);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}

void RawInputWinFilter::stopThreadIfRunning()
{
    runThread.store(false, std::memory_order_relaxed);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = nullptr;
    }

    hiddenWnd = nullptr;
}

//=====================================================
// ★ スレッド本体：hiddenWnd + PeekMessage 最短ループ
//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    RawInputWinFilter* self = reinterpret_cast<RawInputWinFilter*>(param);
    self->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RawInputWndClass";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RawInputWndClass",
        L"RawInputHidden",
        0,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    // false側は hiddenWnd に登録（Qt 経由ゼロ）
    registerRawToTarget(hiddenWnd);

    MSG msg;
    while (runThread.load(std::memory_order_relaxed))
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto exit_loop;

            // WM_INPUT は Dispatch を挟まず最短で処理
            if (msg.message == WM_INPUT)
            {
                processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 最速寄り（CPU は食うが latency が最小になりやすい）
        Sleep(0);
        // SwitchToThread(); でもOK。好みで切替可。
    }

exit_loop:
    hiddenWnd = nullptr;
}

LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RawInputWinFilter* self =
        reinterpret_cast<RawInputWinFilter*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    case WM_INPUT:
        // threadLoop 側で最短処理しているが、
        // 万一ここに来ても落ちないよう保険で処理
        if (self) self->processRawInput(reinterpret_cast<HRAWINPUT>(lp));
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

//=====================================================
//  小物
//=====================================================
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    // 累積の現在値を読む（RMW無し）
    const uint64_t cur = m_mouseAccPacked.load(std::memory_order_relaxed);

    uint32_t curDxU, curDyU;
    UnpackU32U32(cur, curDxU, curDyU);

    // wrap 差分（unsigned）→ signed に戻す
    const uint32_t dDxU = curDxU - m_consPrevDx;
    const uint32_t dDyU = curDyU - m_consPrevDy;

    m_consPrevDx = curDxU;
    m_consPrevDy = curDyU;

    outDx = (int32_t)dDxU;
    outDy = (int32_t)dDyU;
}

void RawInputWinFilter::discardDeltas()
{
    // 共有累積値に consumer 基準を合わせる（RMW無し）
    const uint64_t cur = m_mouseAccPacked.load(std::memory_order_relaxed);

    uint32_t curDxU, curDyU;
    UnpackU32U32(cur, curDxU, curDyU);

    m_consPrevDx = curDxU;
    m_consPrevDy = curDyU;
}

void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vkDown) w.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrev) w = 0;
}

FORCE_INLINE void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8) {
        const uint8_t b = kMouseButtonLUT[vk];
        if (b < 5) {
            m.mouseMask |= (1u << b);
            m.hasMask = 1;
        }
    }
    else if (vk < 256) {
        m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
        m.hasMask = 1;
    }
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    // melonPrimeDS の HK_* は HK_MAX が小さい前提。
    // hk は 0..255 のみを扱う（それ以外は無視）
    if ((unsigned)hk >= kMaxHotkeyId) return;

    HotkeyMask& m = m_hkMask[(unsigned)hk];
    std::memset(&m, 0, sizeof(m));

    // vks は上限を設けず全部入れて良い（vkMask/mouseMask へ集約される）
    for (UINT vk : vks) addVkToMask(m, vk);
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;

    const HotkeyMask& m = m_hkMask[(unsigned)hk];
    if (!m.hasMask) return false;

    const uint64_t r0 = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
    const uint64_t r1 = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
    const uint64_t r2 = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
    const uint64_t r3 = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

    const bool vkHit = (r0 | r1 | r2 | r3) != 0ULL;
    const bool mouseHit =
        (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0u;

    return (vkHit | mouseHit);
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;

    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);

    const bool prev = (m_hkPrev[idx] & bit) != 0ULL;
    if (now) {
        m_hkPrev[idx] |= bit;
        return !prev;
    }
    m_hkPrev[idx] &= ~bit;
    return false;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    if ((unsigned)hk >= kMaxHotkeyId) return false;

    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);

    const bool prev = (m_hkPrev[idx] & bit) != 0ULL;
    if (!now) {
        m_hkPrev[idx] &= ~bit;
        return prev;
    }
    m_hkPrev[idx] |= bit;
    return false;
}

#endif
