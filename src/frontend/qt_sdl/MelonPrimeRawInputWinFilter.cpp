#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

//=====================================================
// LUT 初期化
//=====================================================
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

static struct LutInit {
    LutInit() {
        for (int i = 0; i < 1024; i++) {
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
} _lutInit;


//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0);
    for (auto& b : m_mbCompat) b.store(0);
    for (auto& w : m_hkPrev) w.store(0);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    runThread.store(true);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//=====================================================
// Destructor
//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = nullptr;
    }
}


//=====================================================
// nativeEventFilter（最適化済）
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    UINT m = msg->message;

    //=========================
    // Keyboard (WM_KEYDOWN/UP)
    //=========================
    if (m == WM_KEYDOWN || m == WM_KEYUP) [[likely]]
    {
        UINT vk = (UINT)msg->wParam;
        bool down = (m == WM_KEYDOWN);

        setVkBit(vk, down);
        if (vk < 256) m_vkDownCompat[vk].store(down);

        return false;
    }

    //=========================
    // RawInput (高速経路)
    //=========================
    if (m == WM_INPUT) [[likely]]
    {
        UINT size = sizeof(m_rawBuf);
        if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT, m_rawBuf,
            &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return false;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

        //--------------------------------------------------
        // Mouse
        //--------------------------------------------------
        if (raw->header.dwType == RIM_TYPEMOUSE) [[likely]] {
            const RAWMOUSE& mo = raw->data.mouse;

            LONG dx_ = mo.lLastX;
            LONG dy_ = mo.lLastY;
            if (dx_ | dy_) {
                dx.fetch_add((int)dx_, std::memory_order_relaxed);
                dy.fetch_add((int)dy_, std::memory_order_relaxed);
            }

            const USHORT f = mo.usButtonFlags;
            if (!(f & kAllMouseBtnMask)) return false;

            const BtnLutEntry lut = s_btnLut[f & 0x3FF];

            uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            uint8_t nxt = (cur | lut.down) & (uint8_t)~lut.up;
            m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

            if (lut.down | lut.up) {
                if (lut.down & 1)  m_mbCompat[kMB_Left].store(1);
                if (lut.up & 1)    m_mbCompat[kMB_Left].store(0);

                if (lut.down & 2)  m_mbCompat[kMB_Right].store(1);
                if (lut.up & 2)    m_mbCompat[kMB_Right].store(0);

                if (lut.down & 4)  m_mbCompat[kMB_Middle].store(1);
                if (lut.up & 4)    m_mbCompat[kMB_Middle].store(0);

                if (lut.down & 8)  m_mbCompat[kMB_X1].store(1);
                if (lut.up & 8)    m_mbCompat[kMB_X1].store(0);

                if (lut.down & 16) m_mbCompat[kMB_X2].store(1);
                if (lut.up & 16)   m_mbCompat[kMB_X2].store(0);
            }
        }

        //--------------------------------------------------
        // Keyboard
        //--------------------------------------------------
        else {
            const RAWKEYBOARD& kb = raw->data.keyboard;

            UINT vk = kb.VKey;
            USHORT flags = kb.Flags;
            bool isUp = (flags & RI_KEY_BREAK) != 0;

            switch (vk) {
            case VK_SHIFT:
                vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                break;
            case VK_CONTROL:
                vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
                break;
            case VK_MENU:
                vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
                break;
            }

            setVkBit(vk, !isUp);
            if (vk < 256) m_vkDownCompat[vk].store(!isUp);
        }

        return false;
    }

    return false;
}


//=====================================================
// RawInput thread
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
    wc.lpszClassName = L"MPH_RI_HIDDEN_CLASS";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_HIDDEN_CLASS",
        L"",
        0,
        0, 0, 0, 0,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this
    );

    RAWINPUTDEVICE rid[2]{
        {1,2,0,hiddenWnd},
        {1,6,0,hiddenWnd}
    };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    MSG msg;
    while (runThread.load(std::memory_order_relaxed))
    {
        DWORD r = MsgWaitForMultipleObjectsEx(
            0, nullptr, INFINITE,
            QS_INPUT,
            MWMO_INPUTAVAILABLE | MWMO_ALERTABLE
        );

        if (r == WAIT_FAILED) continue;

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto exit;

            DispatchMessage(&msg);
        }
    }

exit:
    hiddenWnd = nullptr;
}


//=====================================================
// HiddenWndProc
//=====================================================
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
        RawInputWinFilter* self =
            reinterpret_cast<RawInputWinFilter*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        if (self) {
            MSG m{};
            m.message = WM_INPUT;
            m.wParam = wp;
            m.lParam = lp;

            self->nativeEventFilter(QByteArray(), &m, nullptr);
        }
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
// その他
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
        uint8_t idx = kMouseButtonLUT[vk];
        if (idx < 5) {
            m.mouseMask |= (1u << idx);
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

        size_t n = vks.size() > 8 ? 8 : vks.size();
        for (size_t i = 0; i < n; i++) addVkToMask(m, vks[i]);
    }
    else {
        m_hkToVk[hk] = vks;
    }
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) [[likely]] {
        const HotkeyMask& m = m_hkMask[hk];
        if (m.hasMask) {
            uint64_t r =
                (m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0]) |
                (m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1]);

            r |= (m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2]);
            r |= (m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3]);

            bool vkHit = (r != 0);
            bool mouseHit = (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0;

            if (vkHit | mouseHit) return true;
        }
    }

    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end()) {
        for (UINT vk : it->second) {
            if (vk < 8) {
                uint8_t idx = kMouseButtonLUT[vk];
                if (idx < 5 && getMouseButton(idx)) return true;
            }
            else if (getVkState(vk)) return true;
        }
    }

    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) [[likely]] {
        size_t idx = (unsigned)hk >> 6;
        uint64_t bit = 1ULL << (hk & 63);

        if (now) {
            uint64_t prev = m_hkPrev[idx].fetch_or(bit, std::memory_order_relaxed);
            return !(prev & bit);
        }
        else {
            m_hkPrev[idx].fetch_and(~bit, std::memory_order_relaxed);
            return false;
        }
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    size_t i = (unsigned)hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);
    return now && !prev;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) [[likely]] {
        size_t idx = (unsigned)hk >> 6;
        uint64_t bit = 1ULL << (hk & 63);

        if (!now) {
            uint64_t prev = m_hkPrev[idx].fetch_and(~bit, std::memory_order_relaxed);
            return (prev & bit) != 0;
        }
        else {
            m_hkPrev[idx].fetch_or(bit, std::memory_order_relaxed);
            return false;
        }
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    size_t i = (unsigned)hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);
    return (!now) && prev;
}

#endif // _WIN32
