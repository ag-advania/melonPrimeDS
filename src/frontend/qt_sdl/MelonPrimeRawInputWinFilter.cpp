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
// ★ コンストラクタ：専用スレッドを起動
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& w : m_hkPrev)       w.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    // ★ RawInputThread 相当のスレッド起動
    runThread.store(true);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//=====================================================
// ★ デストラクタ
//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = nullptr;
    }
}


//=====================================================
// ★ Qt の fallback （GUI 経由WM_INPUTが来た時の保険）
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    UINT size = sizeof(m_rawBuf);
    if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT,
        m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    const DWORD type = raw->header.dwType;

    //---------------- Mouse ----------------
    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;

        const LONG dx_ = m.lLastX;
        const LONG dy_ = m.lLastY;

        if (dx_ | dy_) {
            dx.fetch_add((int)dx_, std::memory_order_relaxed);
            dy.fetch_add((int)dy_, std::memory_order_relaxed);
        }

        const USHORT f = m.usButtonFlags;
        if (!(f & kAllMouseBtnMask)) return false;

        const BtnLutEntry lut = s_btnLut[f & 0x3FF];

        const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        const uint8_t nxt = (uint8_t)((cur | lut.down) & (uint8_t)~lut.up);
        m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

        if (lut.down | lut.up) {
            if (lut.down & 0x01) m_mbCompat[kMB_Left].store(1, std::memory_order_relaxed);
            if (lut.up & 0x01) m_mbCompat[kMB_Left].store(0, std::memory_order_relaxed);

            if (lut.down & 0x02) m_mbCompat[kMB_Right].store(1, std::memory_order_relaxed);
            if (lut.up & 0x02) m_mbCompat[kMB_Right].store(0, std::memory_order_relaxed);

            if (lut.down & 0x04) m_mbCompat[kMB_Middle].store(1, std::memory_order_relaxed);
            if (lut.up & 0x04) m_mbCompat[kMB_Middle].store(0, std::memory_order_relaxed);

            if (lut.down & 0x08) m_mbCompat[kMB_X1].store(1, std::memory_order_relaxed);
            if (lut.up & 0x08) m_mbCompat[kMB_X1].store(0, std::memory_order_relaxed);

            if (lut.down & 0x10) m_mbCompat[kMB_X2].store(1, std::memory_order_relaxed);
            if (lut.up & 0x10) m_mbCompat[kMB_X2].store(0, std::memory_order_relaxed);
        }

        return false;
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
        case VK_MENU:    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;    break;
        default: break;
        }

        setVkBit(vk, !isUp);

        if (vk < m_vkDownCompat.size())
            m_vkDownCompat[vk].store(!isUp, std::memory_order_relaxed);

        return false;
    }

    return false;
}


//=====================================================
// ★ RawInputWinFilter 内蔵スレッド
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
        this // userdata に RawInputWinFilter*
    );

    RAWINPUTDEVICE rid[2];
    rid[0] = { 1, 2, 0, hiddenWnd };
    rid[1] = { 1, 6, 0, hiddenWnd };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

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
        MSG m{};
        m.hwnd = hwnd;
        m.message = msg;
        m.wParam = wp;
        m.lParam = lp;

        if (self)
            self->nativeEventFilter(QByteArray(), &m, nullptr);
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


//=====================================================
//  小物
//=====================================================
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    outDx = dx.exchange(0, std::memory_order_relaxed);
    outDy = dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas()
{
    dx.exchange(0, std::memory_order_relaxed);
    dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vkDown) w.store(0, std::memory_order_relaxed);
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat) b.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);
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
    if ((unsigned)hk < kMaxHotkeyId) {
        HotkeyMask& m = m_hkMask[hk];
        std::memset(&m, 0, sizeof(m));
        const size_t n = vks.size() > 8 ? 8 : vks.size();
        for (size_t i = 0; i < n; ++i) addVkToMask(m, vks[i]);
        return;
    }
    m_hkToVk[hk] = vks;
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) {

        const HotkeyMask& m = m_hkMask[hk];
        if (m.hasMask) {

            uint64_t r0 = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
            uint64_t r1 = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
            uint64_t r2 = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
            uint64_t r3 = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

            const bool vkHit = (r0 | r1 | r2 | r3) != 0ULL;
            const bool mouseHit =
                (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0u;

            if (vkHit | mouseHit) return true;
        }
    }

    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end()) {
        for (UINT vk : it->second) {
            if (vk < 8) {
                const uint8_t b = kMouseButtonLUT[vk];
                if (b < 5 && getMouseButton(b)) return true;
            }
            else if (getVkState(vk)) {
                return true;
            }
        }
    }
    return false;
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
        }
        else {
            m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
            return false;
        }
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    const size_t i = (unsigned)hk & 1023;
    const uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
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
        }
        else {
            m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
            return false;
        }
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    const size_t i = (unsigned)hk & 1023;
    const uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
    return (!now) && prev;
}

#endif
