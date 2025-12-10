#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>


//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& e : m_hkPrevAll)    e.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    std::memset(m_usedVkTable.data(), 0, sizeof(m_usedVkTable));
    std::memset(m_usedVkMask, 0, sizeof(m_usedVkMask));
    std::memset(m_keyBitmap.data(), 0, sizeof(m_keyBitmap));

    runThread.store(true, std::memory_order_relaxed);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//=====================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false, std::memory_order_relaxed);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
}


//=====================================================
// Mouse fast
//=====================================================
void RawInputWinFilter::onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWMOUSE& m = raw->data.mouse;

    LONG dx = m.lLastX;
    LONG dy = m.lLastY;
    if (dx | dy)
        self->addDelta(dx, dy);

    USHORT f = m.usButtonFlags;
    if (!(f & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)))
        return;

    uint8_t cur = self->m_state.mouseButtons.load(std::memory_order_relaxed);
    uint8_t d = 0, u = 0;

    if (f & RI_MOUSE_LEFT_BUTTON_DOWN)  d |= 1;
    if (f & RI_MOUSE_LEFT_BUTTON_UP)    u |= 1;
    if (f & RI_MOUSE_RIGHT_BUTTON_DOWN) d |= 2;
    if (f & RI_MOUSE_RIGHT_BUTTON_UP)   u |= 2;
    if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN)d |= 4;
    if (f & RI_MOUSE_MIDDLE_BUTTON_UP)  u |= 4;
    if (f & RI_MOUSE_BUTTON_4_DOWN)     d |= 8;
    if (f & RI_MOUSE_BUTTON_4_UP)       u |= 8;
    if (f & RI_MOUSE_BUTTON_5_DOWN)     d |= 16;
    if (f & RI_MOUSE_BUTTON_5_UP)       u |= 16;

    uint8_t nxt = (cur | d) & ~u;
    self->m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

    self->m_mbCompat[0].store((nxt & 1) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[1].store((nxt & 2) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[2].store((nxt & 4) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[3].store((nxt & 8) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[4].store((nxt & 16) ? 1 : 0, std::memory_order_relaxed);
}


//=====================================================
// Keyboard fast
//=====================================================
void RawInputWinFilter::onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWKEYBOARD& k = raw->data.keyboard;

    UINT vk = k.VKey;
    bool isUp = (k.Flags & RI_KEY_BREAK);
    bool down = !isUp;

    if (vk == VK_SHIFT)
    {
        UINT sc = k.MakeCode;
        vk = (sc == 0x2A) ? VK_LSHIFT :
            (sc == 0x36) ? VK_RSHIFT : VK_SHIFT;
    }
    else if (vk == VK_CONTROL)
    {
        vk = (k.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
    }
    else if (vk == VK_MENU)
    {
        vk = (k.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
    }

    if (vk < 256)
        self->m_vkDownCompat[vk].store(down, std::memory_order_relaxed);

    uint32_t w = vk >> 6;
    uint64_t bit = 1ULL << (vk & 63);

    uint64_t cur = self->m_state.vkDown[w].load(std::memory_order_relaxed);
    uint64_t nxt = down ? (cur | bit) : (cur & ~bit);

    self->m_state.vkDown[w].store(nxt, std::memory_order_relaxed);
}


//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID p)
{
    reinterpret_cast<RawInputWinFilter*>(p)->threadLoop();
    return 0;
}


//=====================================================
// RawInput thread loop
//=====================================================
void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RI_THREAD_CLASS";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_THREAD_CLASS", L"",
        0, 0, 0, 0, 0,
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
        if (!PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            syncWithSendInput();
            continue;
        }

        if (msg.message == WM_INPUT)
        {
            UINT size = 0;

            GetRawInputData(
                reinterpret_cast<HRAWINPUT>(msg.lParam),
                RID_INPUT,
                nullptr,
                &size,
                sizeof(RAWINPUTHEADER)
            );

            if (size <= sizeof(m_rawBuf))
            {
                GetRawInputData(
                    reinterpret_cast<HRAWINPUT>(msg.lParam),
                    RID_INPUT,
                    m_rawBuf,
                    &size,
                    sizeof(RAWINPUTHEADER)
                );

                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

                if (raw->header.dwType == RIM_TYPEMOUSE)
                    onMouse_fast(this, raw);
                else
                    onKeyboard_fast(this, raw);
            }

            continue;
        }

        if (msg.message == WM_QUIT)
            break;
    }

    hiddenWnd = nullptr;
}


//=====================================================
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(
    HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:   return 0;
    case WM_INPUT:    return 0;
    case WM_CLOSE:
    case WM_DESTROY:  PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}


//=====================================================
// Joy2Key 補正（bitmap + BFS）
//=====================================================
void RawInputWinFilter::syncWithSendInput()
{
    //--- 256キーをまとめて1回取得 ---
    BYTE buf[256];
    GetKeyboardState(buf);

    for (int i = 0; i < 256; i++)
        m_keyBitmap[i] = (buf[i] & 0x80) ? 1 : 0;


    //--- BSF で使用キーだけ走査 ---
    for (int blk = 0; blk < 4; blk++)
    {
        uint64_t mask = m_usedVkMask[blk];

        while (mask)
        {
            unsigned long bit;
            _BitScanForward64(&bit, mask);

            UINT vk = (blk << 6) | bit;
            mask &= (mask - 1);

            bool down = m_keyBitmap[vk] != 0;
            bool raw = getVkState(vk);

            if (raw != down)
                setVkBit(vk, down);

            m_vkDownCompat[vk].store(down, std::memory_order_relaxed);
        }
    }


    //--- Mouse 補正 ---
    struct { UINT vk; uint8_t idx; } tbl[]{
        {VK_LBUTTON,0}, {VK_RBUTTON,1}, {VK_MBUTTON,2},
        {VK_XBUTTON1,3}, {VK_XBUTTON2,4},
    };

    for (auto& m : tbl)
    {
        if (!m_usedVkTable[m.vk]) continue;

        bool down = (GetAsyncKeyState(m.vk) & 0x8000) != 0;
        bool raw = getMouseButton(m.idx);

        if (raw != down)
        {
            uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            uint8_t nxt = down ? (cur | (1 << m.idx)) : (cur & ~(1 << m.idx));
            m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
        }

        m_mbCompat[m.idx].store(down, std::memory_order_relaxed);
    }
}


//=====================================================
void RawInputWinFilter::resetAllKeys()
{
    for (auto& v : m_state.vkDown)
        v.store(0, std::memory_order_relaxed);

    for (auto& c : m_vkDownCompat)
        c.store(0, std::memory_order_relaxed);
}


//=====================================================
void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);

    for (auto& m : m_mbCompat)
        m.store(0, std::memory_order_relaxed);
}


//=====================================================
void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& h : m_hkPrevAll)
        h.store(0, std::memory_order_relaxed);
}


//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId)
    {
        const HotkeyMask& m = m_hkMask[hk];

        if (m.hasMask)
        {
            uint64_t a = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
            uint64_t b = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
            uint64_t c = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
            uint64_t d = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

            if ((a | b | c | d) != 0)
                return true;

            if (m.mouseMask & m_state.mouseButtons.load(std::memory_order_relaxed))
                return true;

            return false;
        }
    }

    // fallback
    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end())
    {
        for (UINT vk : it->second)
        {
            if (vk < 8)
            {
                static constexpr uint8_t tbl[8] = { 255,0,1,255,2,3,4,255 };
                uint8_t idx = tbl[vk];
                if (idx < 5 && getMouseButton(idx))
                    return true;
            }
            else if (vk < 256 && getVkState(vk))
                return true;
        }
    }

    return false;
}


//=====================================================
bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    bool now = hotkeyDown(hk);
    uint8_t prev = m_hkPrevAll[hk & 1023].exchange(now, std::memory_order_acq_rel);
    return (!prev && now);
}


//=====================================================
bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);
    uint8_t prev = m_hkPrevAll[hk & 1023].exchange(now, std::memory_order_acq_rel);
    return (prev && !now);
}


//=====================================================
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId)
    {
        HotkeyMask& m = m_hkMask[hk];
        std::memset(&m, 0, sizeof(m));

        for (UINT vk : vks)
        {
            if (vk == 0) continue;
            addVkToMask(m, vk);

            if (vk < 256)
            {
                m_usedVkTable[vk] = 1;
                m_usedVkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
    }
    else
    {
        m_hkToVk[hk] = vks;

        for (UINT vk : vks)
        {
            if (vk < 256)
            {
                m_usedVkTable[vk] = 1;
                m_usedVkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
    }
}

#endif // _WIN32
