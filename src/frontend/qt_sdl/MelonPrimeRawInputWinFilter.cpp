#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"

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
// RawInput register helper
//=====================================================
void RawInputWinFilter::registerRawToTarget(HWND target, bool inputSink) noexcept
{
    RAWINPUTDEVICE rid[2];
    rid[0] = { 1, 2, (DWORD)(inputSink ? RIDEV_INPUTSINK : 0), target };
    rid[1] = { 1, 6, (DWORD)(inputSink ? RIDEV_INPUTSINK : 0), target };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

//=====================================================
// ctor / dtor
//=====================================================
RawInputWinFilter::RawInputWinFilter(bool joy2keySupport, HWND mainHwnd)
{
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& w : m_hkPrev)       w.store(0, std::memory_order_relaxed);
    for (auto& x : m_hkPrevAll)    x.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    m_targetHwnd = mainHwnd;
    m_joy2keySupport.store(joy2keySupport, std::memory_order_relaxed);

    // 初期：ターゲットを固定して登録（true側低遅延化の本体）
    registerRawToTarget(m_targetHwnd, false);

    // 低遅延（false）なら hiddenWnd スレッドを起動し、登録を hiddenWndへ切り替える
    if (!joy2keySupport) {
        startThreadIfNeeded();
    }
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopThreadIfRunning();

    HotkeyDynTable* cur = m_hkDyn.load(std::memory_order_relaxed);
    if (cur) m_hkDynGarbage.push_back(cur);
    for (HotkeyDynTable* p : m_hkDynGarbage)
        ::operator delete((void*)p);
    m_hkDynGarbage.clear();
    m_hkDyn.store(nullptr, std::memory_order_relaxed);
}

//=====================================================
// mode
//=====================================================
void RawInputWinFilter::setJoy2KeySupport(bool enabled) noexcept
{
    const bool cur = m_joy2keySupport.load(std::memory_order_relaxed);
    if (cur == enabled) return;

    m_joy2keySupport.store(enabled, std::memory_order_relaxed);

    if (enabled) {
        stopThreadIfRunning();
        registerRawToTarget(m_targetHwnd, false);
    } else {
        startThreadIfNeeded();
    }
}

void RawInputWinFilter::setRawInputTarget(HWND hwnd) noexcept
{
    m_targetHwnd = hwnd;

    // false側（スレッド中）は hiddenWnd に向けるので触らない
    if (runThread.load(std::memory_order_relaxed))
        return;

    // true側のみ：配送先固定で再登録
    registerRawToTarget(m_targetHwnd, false);
}

//=====================================================
// Qt WM_INPUT path
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    // false側（スレッド）中は Qt 経由を捨てて二重処理回避
    if (runThread.load(std::memory_order_relaxed))
        return false;

    processRawInputHandle((HRAWINPUT)msg->lParam);
    return false;
}

//=====================================================
// Raw parsing
//=====================================================
void RawInputWinFilter::processRawInputHandle(HRAWINPUT hRaw) noexcept
{
    UINT size = sizeof(m_rawBuf);
    if (GetRawInputData(hRaw, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    const DWORD type = raw->header.dwType;

    if (type == RIM_TYPEMOUSE) {
        processRawMouse(raw->data.mouse);
        return;
    }
    if (type == RIM_TYPEKEYBOARD) {
        processRawKeyboard(raw->data.keyboard);
        return;
    }
}

FORCE_INLINE void RawInputWinFilter::processRawMouse(const RAWMOUSE& m) noexcept
{
    const LONG dx_ = m.lLastX;
    const LONG dy_ = m.lLastY;

    if (dx_ | dy_) {
        dx.fetch_add((int)dx_, std::memory_order_relaxed);
        dy.fetch_add((int)dy_, std::memory_order_relaxed);
    }

    const USHORT f = m.usButtonFlags;
    if (!(f & kAllMouseBtnMask)) return;

    const BtnLutEntry lut = s_btnLut[f & 0x3FF];

    const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
    const uint8_t nxt = (uint8_t)((cur | lut.down) & (uint8_t)~lut.up);
    m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

    // compat は差分更新（WM_INPUTのボタンイベント時だけ）
    const uint8_t diff = (uint8_t)(cur ^ nxt);
    if (!diff) return;

    if (diff & 0x01) m_mbCompat[kMB_Left].store((nxt >> 0) & 1u, std::memory_order_relaxed);
    if (diff & 0x02) m_mbCompat[kMB_Right].store((nxt >> 1) & 1u, std::memory_order_relaxed);
    if (diff & 0x04) m_mbCompat[kMB_Middle].store((nxt >> 2) & 1u, std::memory_order_relaxed);
    if (diff & 0x08) m_mbCompat[kMB_X1].store((nxt >> 3) & 1u, std::memory_order_relaxed);
    if (diff & 0x10) m_mbCompat[kMB_X2].store((nxt >> 4) & 1u, std::memory_order_relaxed);
}

FORCE_INLINE void RawInputWinFilter::processRawKeyboard(const RAWKEYBOARD& kb) noexcept
{
    UINT vk = kb.VKey;
    const USHORT flags = kb.Flags;
    const bool isUp = (flags & RI_KEY_BREAK) != 0;

    switch (vk) {
    case VK_SHIFT:   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX); break;
    case VK_CONTROL: vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL; break;
    case VK_MENU:    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;    break;
    default: break;
    }

    setVkBit(vk, !isUp);

    if (vk < 256) {
        const uint8_t nv = (uint8_t)(!isUp ? 1u : 0u);
        const uint8_t pv = m_vkDownCompat[vk].load(std::memory_order_relaxed);
        if (pv != nv) m_vkDownCompat[vk].store(nv, std::memory_order_relaxed);
    }
}

//=====================================================
// Small utils
//=====================================================
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy) noexcept
{
    outDx = dx.exchange(0, std::memory_order_relaxed);
    outDy = dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas() noexcept
{
    (void)dx.exchange(0, std::memory_order_relaxed);
    (void)dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetAllKeys() noexcept
{
    for (auto& w : m_state.vkDown) w.store(0, std::memory_order_relaxed);
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetMouseButtons() noexcept
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat) b.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges() noexcept
{
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);
    for (auto& x : m_hkPrevAll) x.store(0, std::memory_order_relaxed);
}

//=====================================================
// hk>=256 dyn table (unordered_map 排除)
//=====================================================
void RawInputWinFilter::setHotkeyDynMask(uint32_t idx, const HotkeyMask& nm)
{
    HotkeyDynTable* oldT = m_hkDyn.load(std::memory_order_acquire);
    const uint32_t oldCount = oldT ? oldT->count : 0;

    if (oldT && idx < oldCount) {
        if (std::memcmp(&oldT->masks[idx], &nm, sizeof(HotkeyMask)) == 0)
            return;
    }

    uint32_t newCount = oldCount;
    if (newCount == 0) newCount = 64;
    while (idx >= newCount) newCount <<= 1;

    const size_t bytes = sizeof(HotkeyDynTable) + (size_t)(newCount - 1) * sizeof(HotkeyMask);
    HotkeyDynTable* newT = reinterpret_cast<HotkeyDynTable*>(::operator new(bytes));
    newT->count = newCount;

    std::memset(newT->masks, 0, (size_t)newCount * sizeof(HotkeyMask));

    if (oldT && oldCount)
        std::memcpy(newT->masks, oldT->masks, (size_t)oldCount * sizeof(HotkeyMask));

    newT->masks[idx] = nm;

    m_hkDyn.store(newT, std::memory_order_release);
    if (oldT) m_hkDynGarbage.push_back(oldT);
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    HotkeyMask nm{};
    std::memset(&nm, 0, sizeof(nm));

    const size_t n = vks.size();
    for (size_t i = 0; i < n; ++i) addVkToMask(nm, vks[i]);

    if ((unsigned)hk < kMaxHotkeyId) {
        HotkeyMask& m = m_hkMask[(size_t)hk];
        if (std::memcmp(&m, &nm, sizeof(HotkeyMask)) == 0)
            return;
        m = nm;
        return;
    }

    const uint32_t idx = (hk >= 256) ? (uint32_t)(hk - 256) : 0;
    setHotkeyDynMask(idx, nm);
}

//=====================================================
// hotkey queries
//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) {
        const HotkeyMask& m = m_hkMask[(size_t)hk];
        if (!m.hasMask) return false;

        const uint64_t r0 = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
        const uint64_t r1 = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
        const uint64_t r2 = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
        const uint64_t r3 = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

        const bool vkHit = (r0 | r1 | r2 | r3) != 0ULL;
        const bool mouseHit =
            (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0u;

        return (vkHit | mouseHit) != 0;
    }

    const uint32_t idx = (hk >= 256) ? (uint32_t)(hk - 256) : 0;
    const HotkeyMask* pm = getDynMask(idx);
    if (!pm || !pm->hasMask) return false;

    const uint64_t r0 = m_state.vkDown[0].load(std::memory_order_relaxed) & pm->vkMask[0];
    const uint64_t r1 = m_state.vkDown[1].load(std::memory_order_relaxed) & pm->vkMask[1];
    const uint64_t r2 = m_state.vkDown[2].load(std::memory_order_relaxed) & pm->vkMask[2];
    const uint64_t r3 = m_state.vkDown[3].load(std::memory_order_relaxed) & pm->vkMask[3];

    const bool vkHit = (r0 | r1 | r2 | r3) != 0ULL;
    const bool mouseHit =
        (m_state.mouseButtons.load(std::memory_order_relaxed) & pm->mouseMask) != 0u;

    return (vkHit | mouseHit) != 0;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    const bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) {
        const size_t idx = (unsigned)hk >> 6;
        const uint64_t bit = 1ULL << (hk & 63);

        if (now) {
            const uint64_t prev = m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
            return !(prev & bit);
        } else {
            m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
            return false;
        }
    }

    const size_t i = (unsigned)hk & 1023;
    const uint8_t prev = m_hkPrevAll[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
    return now && !prev;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    const bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) {
        const size_t idx = (unsigned)hk >> 6;
        const uint64_t bit = 1ULL << (hk & 63);

        if (!now) {
            const uint64_t prev = m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
            return (prev & bit) != 0;
        } else {
            m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
            return false;
        }
    }

    const size_t i = (unsigned)hk & 1023;
    const uint8_t prev = m_hkPrevAll[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
    return (!now) && prev;
}

//=====================================================
// thread (joy2keySupport=false)
//=====================================================
void RawInputWinFilter::startThreadIfNeeded() noexcept
{
    if (runThread.load(std::memory_order_relaxed))
        return;

    runThread.store(true, std::memory_order_relaxed);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
    if (!hThread) {
        runThread.store(false, std::memory_order_relaxed);
    }
}

void RawInputWinFilter::stopThreadIfRunning() noexcept
{
    if (!runThread.load(std::memory_order_relaxed))
        return;

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

    if (m_joy2keySupport.load(std::memory_order_relaxed))
        registerRawToTarget(m_targetHwnd, false);
}

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

    // hiddenWnd はフォーカスを持たないので INPUTSINK
    // NOLEGACY は使わない
    registerRawToTarget(hiddenWnd, true);

    MSG msg;
    while (runThread.load(std::memory_order_relaxed))
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto exit_loop;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Sleep(0);
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
    {
        if (self)
            self->processRawInputHandle((HRAWINPUT)lp);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

#endif // _WIN32
