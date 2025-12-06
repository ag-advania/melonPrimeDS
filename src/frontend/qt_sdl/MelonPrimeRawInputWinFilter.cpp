#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

//=====================================================
// LUT 初期化（128B整列）
//=====================================================
alignas(128) RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

static struct LutInit_Level3 {
    LutInit_Level3() {
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
} _lutInit_Level3;


//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& w : m_hkPrevAll)    w.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    runThread.store(true, std::memory_order_relaxed);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//=====================================================
// Destructor
//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false, std::memory_order_relaxed);
    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = nullptr;
    }
}


//=====================================================
// RawInput handlers（分岐ゼロ dispatch 用）
//=====================================================
void RawInputWinFilter::onMouse(RAWINPUT* raw)
{
    const RAWMOUSE& m = raw->data.mouse;

    LONG dx_ = m.lLastX;
    LONG dy_ = m.lLastY;
    if (dx_ | dy_) {
        dx.fetch_add((int)dx_, std::memory_order_relaxed);
        dy.fetch_add((int)dy_, std::memory_order_relaxed);
    }

    const USHORT flags = m.usButtonFlags;
    if (!(flags & kAllMouseBtnMask)) return;

    const BtnLutEntry lut = s_btnLut[flags & 0x3FF];

    uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);

    // Level3 XOR方式
    uint8_t flip = (cur ^ lut.down) & lut.down;
    uint8_t nxt = (cur ^ flip) & (uint8_t)~lut.up;

    m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

    // 5ボタン分をループで更新
    static constexpr uint8_t maskTbl[5] = { 1,2,4,8,16 };
    for (int i = 0; i < 5; i++) {
        uint8_t mk = maskTbl[i];
        if (lut.down & mk) m_mbCompat[i].store(1, std::memory_order_relaxed);
        if (lut.up & mk) m_mbCompat[i].store(0, std::memory_order_relaxed);
    }
}

void RawInputWinFilter::onKeyboard(RAWINPUT* raw)
{
    const RAWKEYBOARD& kb = raw->data.keyboard;
    UINT vk = kb.VKey;
    const USHORT flags = kb.Flags;
    bool isUp = (flags & RI_KEY_BREAK);

    // MapVirtualKey の高速代替（LUT）
    if (vk == VK_SHIFT) {
        UINT code = kb.MakeCode;
        // 左右Shiftの判定
        vk = (code == 0x2A ? VK_LSHIFT :
            (code == 0x36 ? VK_RSHIFT : VK_LSHIFT));
    }
    else if (vk == VK_CONTROL) {
        vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
    }
    else if (vk == VK_MENU) {
        vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
    }

    bool down = !isUp;
    setVkBit(vk, down);
    if (vk < 256)
        m_vkDownCompat[vk].store(down, std::memory_order_relaxed);
}


//=====================================================
// nativeEventFilter（Qt/Joy2Key互換維持）
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    UINT m = msg->message;

    //=========================
    // WM_KEYDOWN/WM_KEYUP
    //=========================
    if (m == WM_KEYDOWN || m == WM_KEYUP) [[likely]]
    {
        UINT vk = (UINT)msg->wParam;
        bool down = (m == WM_KEYDOWN);

        setVkBit(vk, down);
        if (vk < 256) m_vkDownCompat[vk].store(down, std::memory_order_relaxed);

        return false;
    }

    //=========================
    // WM_INPUT
    //=========================
    if (m == WM_INPUT) [[likely]]
    {
        UINT size = sizeof(RAWINPUT); // Level3固定サイズ
        if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT, m_rawBuf,
            &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return false;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

        // dispatchテーブル
        static constexpr auto handler = std::array<
            void (RawInputWinFilter::*)(RAWINPUT*), 2>{
            &RawInputWinFilter::onMouse,
            &RawInputWinFilter::onKeyboard
        };

        UINT t = raw->header.dwType; // 0=mouse,1=keyboard
        (this->*(handler[t]))(raw);

        return false;
    }

    return false;
}


//=====================================================
// RawInput thread
//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    RawInputWinFilter* self =
        reinterpret_cast<RawInputWinFilter*>(param);
    self->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RI_HIDDEN_CLASS_LV3";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_HIDDEN_CLASS_LV3",
        L"",
        0,
        0, 0, 0, 0,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this
    );

    RAWINPUTDEVICE rid[2]{
        {1,2,0,hiddenWnd}, // Mouse
        {1,6,0,hiddenWnd}  // Keyboard
    };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    MSG msg;
    while (runThread.load(std::memory_order_relaxed))
    {
        // Level3：APC無効化でレイテンシ最小
        DWORD r = MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            INFINITE,
            QS_INPUT,
            MWMO_INPUTAVAILABLE
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
// Hidden window proc
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
            reinterpret_cast<RawInputWinFilter*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA));

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
// Utility
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
    for (auto& w : m_hkPrevAll) w.store(0, std::memory_order_relaxed);
}


//=====================================================
// hotkey masks
//=====================================================
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId) {
        HotkeyMask& m = m_hkMask[hk];
        std::memset(&m, 0, sizeof(m));

        size_t n = (vks.size() > 64 ? 64 : vks.size());
        for (size_t i = 0; i < n; i++) addVkToMask(m, vks[i]);
    }
    else {
        m_hkToVk[hk] = vks;
    }
}


//=====================================================
// hotkeyDown
//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) [[likely]]
    {
        const HotkeyMask& m = m_hkMask[hk];
        if (m.hasMask) {
            // ORツリー構造（パイプライン最適）
            uint64_t a = (m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0]);
            uint64_t b = (m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1]);
            uint64_t c = (m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2]);
            uint64_t d = (m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3]);

            uint64_t r = (a | b) | (c | d);

            bool vkHit = (r != 0);
            bool mouseHit = (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask);

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


//=====================================================
// hotkeyPressed
//=====================================================
bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    size_t i = (size_t)hk & 1023;
    uint8_t prev = m_hkPrevAll[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);

    return now && !prev;
}


//=====================================================
// hotkeyReleased
//=====================================================
bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    size_t i = (size_t)hk & 1023;
    uint8_t prev = m_hkPrevAll[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);

    return (!now) && prev;
}

#endif // _WIN32
