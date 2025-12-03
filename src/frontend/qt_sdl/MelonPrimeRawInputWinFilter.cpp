#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <algorithm>
#include <windowsx.h>

//======================================================
// 1024 LUT 初期化
//======================================================
RawInputWinFilter::BtnEntry RawInputWinFilter::s_btnLut[1024];

static struct InitRawLut {
    InitRawLut() {
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
} g_InitRawLut;


//======================================================
// Constructor
//======================================================
RawInputWinFilter::RawInputWinFilter()
{
    memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    for (auto& a : m_vkDownCompat) a.store(0);
    for (auto& b : m_mbCompat)     b.store(0);
    for (auto& w : m_hkPrev)       w.store(0);

    runThread.store(true);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}

//======================================================
// Destructor
//======================================================
RawInputWinFilter::~RawInputWinFilter()
{
    runThread.store(false);

    if (hiddenWnd)
        PostMessage(hiddenWnd, WM_CLOSE, 0, 0);

    if (hThread)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
}

//======================================================
// Qt EventFilter（最新版のまま＝RawInput fallback）
//======================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType != "windows_generic_MSG") return false;

    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    if (msg->message == WM_INPUT)
    {
        UINT size = sizeof(m_rawBuf);
        if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return false;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

        //==========================================
        // RawInput: Mouse
        //==========================================
        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            const RAWMOUSE& m = raw->data.mouse;
            int dx_ = m.lLastX;
            int dy_ = m.lLastY;

            if ((dx_ | dy_) != 0)
            {
                dx.fetch_add(dx_, std::memory_order_relaxed);
                dy.fetch_add(dy_, std::memory_order_relaxed);
            }

            USHORT flags = m.usButtonFlags;
            if (flags & kAllMouseBtnMask)
            {
                BtnEntry ent = s_btnLut[flags & 0x3FF];

                uint8_t cur = m_state.mouseButtons.load();
                uint8_t nxt = (cur | ent.d) & (uint8_t)(~ent.u);
                m_state.mouseButtons.store(nxt);

                if (ent.d | ent.u)
                {
                    if (ent.d & 0x01) m_mbCompat[kMB_Left].store(1);
                    if (ent.u & 0x01) m_mbCompat[kMB_Left].store(0);
                    if (ent.d & 0x02) m_mbCompat[kMB_Right].store(1);
                    if (ent.u & 0x02) m_mbCompat[kMB_Right].store(0);
                    if (ent.d & 0x04) m_mbCompat[kMB_Middle].store(1);
                    if (ent.u & 0x04) m_mbCompat[kMB_Middle].store(0);
                    if (ent.d & 0x08) m_mbCompat[kMB_X1].store(1);
                    if (ent.u & 0x08) m_mbCompat[kMB_X1].store(0);
                    if (ent.d & 0x10) m_mbCompat[kMB_X2].store(1);
                    if (ent.u & 0x10) m_mbCompat[kMB_X2].store(0);
                }
            }
        }

        //==========================================
        // RawInput: Keyboard
        //==========================================
        else if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            bool isUp = (kb.Flags & RI_KEY_BREAK);

            switch (vk)
            {
            case VK_SHIFT:
                vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                break;
            case VK_CONTROL:
                vk = (kb.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
                break;
            case VK_MENU:
                vk = (kb.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
                break;
            }

            setVkBit(vk, !isUp);
            if (vk < m_vkDownCompat.size())
                m_vkDownCompat[vk].store(!isUp);
        }
    }

    return false;
}


//======================================================
// ThreadFunc
//======================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    reinterpret_cast<RawInputWinFilter*>(param)->threadLoop();
    return 0;
}


//======================================================
// HiddenWndProc（RawInputThread側）
//======================================================
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    RawInputWinFilter* self = reinterpret_cast<RawInputWinFilter*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self)
    {
        if (msg == WM_CREATE)
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    if (msg == WM_INPUT)
    {
        UINT size = sizeof(self->m_rawBuf);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, self->m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return 0;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(self->m_rawBuf);

        //--------------------------------------
        // Mouse
        //--------------------------------------
        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            const RAWMOUSE& m = raw->data.mouse;

            int dx = m.lLastX;
            int dy = m.lLastY;
            if ((dx | dy) != 0)
            {
                self->dx.fetch_add(dx, std::memory_order_relaxed);
                self->dy.fetch_add(dy, std::memory_order_relaxed);
            }

            USHORT flags = m.usButtonFlags;
            if (flags & kAllMouseBtnMask)
            {
                BtnEntry ent = s_btnLut[flags & 0x3FF];

                uint8_t cur = self->m_state.mouseButtons.load();
                uint8_t nxt = (cur | ent.d) & (uint8_t)(~ent.u);
                self->m_state.mouseButtons.store(nxt);

                if (ent.d | ent.u)
                {
                    if (ent.d & 0x01) self->m_mbCompat[kMB_Left].store(1);
                    if (ent.u & 0x01) self->m_mbCompat[kMB_Left].store(0);
                    if (ent.d & 0x02) self->m_mbCompat[kMB_Right].store(1);
                    if (ent.u & 0x02) self->m_mbCompat[kMB_Right].store(0);
                    if (ent.d & 0x04) self->m_mbCompat[kMB_Middle].store(1);
                    if (ent.u & 0x04) self->m_mbCompat[kMB_Middle].store(0);
                    if (ent.d & 0x08) self->m_mbCompat[kMB_X1].store(1);
                    if (ent.u & 0x08) self->m_mbCompat[kMB_X1].store(0);
                    if (ent.d & 0x10) self->m_mbCompat[kMB_X2].store(1);
                    if (ent.u & 0x10) self->m_mbCompat[kMB_X2].store(0);
                }
            }
        }

        //--------------------------------------
        // Keyboard
        //--------------------------------------
        else if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            bool isUp = (kb.Flags & RI_KEY_BREAK);

            switch (vk)
            {
            case VK_SHIFT:
                vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                break;
            case VK_CONTROL:
                vk = (kb.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
                break;
            case VK_MENU:
                vk = (kb.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
                break;
            }

            self->setVkBit(vk, !isUp);
            if (vk < self->m_vkDownCompat.size())
                self->m_vkDownCompat[vk].store(!isUp);
        }

        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}


//======================================================
// Thread Loop（最新版ベース＋Joy2Key統合）
//======================================================
void RawInputWinFilter::threadLoop()
{
    // -------- Hidden Window 作成 --------
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RAWINPUT_HIDDEN_LATEST";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RAWINPUT_HIDDEN_LATEST", L"",
        0,
        0, 0, 0, 0,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this     // lpCreateParams
    );

    SetWindowLongPtr(hiddenWnd, GWLP_USERDATA, (LONG_PTR)this);

    // ★ HiddenWnd 作成後に RawInput を登録（必須）
    RAWINPUTDEVICE rid[2];
    rid[0] = { 1, 2, 0, hiddenWnd };   // keyboard
    rid[1] = { 1, 6, 0, hiddenWnd };   // mouse
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    MSG msg{};

    while (runThread.load(std::memory_order_relaxed))
    {
        // WM_INPUT → HiddenWndProc
        while (PeekMessage(&msg, hiddenWnd, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            DispatchMessage(&msg);
        }

        // Joy2Key（最新版：bounded VK only）
        joy2keyUpdateBoundKeys();

        // 高速ループ
        Sleep(0);
    }
}


//======================================================
// Joy2Key: バインドされたキーのみ
//======================================================
void RawInputWinFilter::joy2keyUpdateBoundKeys()
{
    for (size_t hk = 0; hk < kMaxHotkeyId; hk++)
    {
        const HotkeyMask& m = m_hkMask[hk];
        if (!m.hasMask) continue;

        // -------------- Keyboard --------------
        for (int w = 0; w < 4; w++)
        {
            uint64_t mask = m.vkMask[w];
            while (mask)
            {
                uint64_t bit = mask & -mask;
                uint32_t vk = (w << 6) + __builtin_ctzll(mask);

                SHORT st = GetAsyncKeyState(vk);
                bool down = (st & 0x8000);
                bool raw = getVkState(vk);

                if (down != raw)
                    setVkBit(vk, down);

                m_vkDownCompat[vk].store(down);

                mask ^= bit;
            }
        }

        // -------------- Mouse --------------
        uint8_t mm = m.mouseMask;
        if (mm)
        {
            static const UINT mbVk[5] = {
                VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
            };

            for (int i = 0; i < 5; i++)
            {
                if (!(mm & (1u << i))) continue;

                UINT vk = mbVk[i];
                SHORT st = GetAsyncKeyState(vk);
                bool down = (st & 0x8000);
                bool raw = getMouseButton(i);

                if (down != raw)
                {
                    uint8_t cur = m_state.mouseButtons.load();
                    uint8_t nxt = down ? (cur | (1u << i)) : (cur & ~(1u << i));
                    m_state.mouseButtons.store(nxt);
                }

                m_mbCompat[i].store(down);
            }
        }
    }
}


//======================================================
// Mouse Delta
//======================================================
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    outDx = dx.exchange(0);
    outDy = dy.exchange(0);
}

void RawInputWinFilter::discardDeltas()
{
    dx.exchange(0);
    dy.exchange(0);
}


//======================================================
// Resets
//======================================================
void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vkDown) w.store(0);
    for (auto& c : m_vkDownCompat) c.store(0);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0);
    for (auto& c : m_mbCompat) c.store(0);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrev) w.store(0);
}


//======================================================
// Hotkey Mask
//======================================================
void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8)
    {
        switch (vk)
        {
        case VK_LBUTTON:    m.mouseMask |= (1u << kMB_Left);   break;
        case VK_RBUTTON:    m.mouseMask |= (1u << kMB_Right);  break;
        case VK_MBUTTON:    m.mouseMask |= (1u << kMB_Middle); break;
        case VK_XBUTTON1:   m.mouseMask |= (1u << kMB_X1);     break;
        case VK_XBUTTON2:   m.mouseMask |= (1u << kMB_X2);     break;
        }
        m.hasMask = 1;
    }
    else if (vk < 256)
    {
        m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
        m.hasMask = 1;
    }
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId)
    {
        HotkeyMask& m = m_hkMask[hk];
        memset(&m, 0, sizeof(m));

        const size_t n = (vks.size() > 8 ? 8 : vks.size());
        for (size_t i = 0; i < n; i++)
            addVkToMask(m, vks[i]);

        return;
    }

    m_hkToVk[hk] = vks;
}


//======================================================
// Hotkey Query
//======================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId)
    {
        const HotkeyMask& m = m_hkMask[hk];
        if (m.hasMask)
        {
            uint64_t r =
                (m_state.vkDown[0].load() & m.vkMask[0]) |
                (m_state.vkDown[1].load() & m.vkMask[1]) |
                (m_state.vkDown[2].load() & m.vkMask[2]) |
                (m_state.vkDown[3].load() & m.vkMask[3]);

            uint8_t mb = m_state.mouseButtons.load() & m.mouseMask;
            return (r | mb) != 0;
        }
    }

    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end())
    {
        for (UINT vk : it->second)
        {
            if (vk < 8)
            {
                switch (vk)
                {
                case VK_LBUTTON: if (getMouseButton(kMB_Left)) return true; break;
                case VK_RBUTTON: if (getMouseButton(kMB_Right)) return true; break;
                case VK_MBUTTON: if (getMouseButton(kMB_Middle)) return true; break;
                case VK_XBUTTON1: if (getMouseButton(kMB_X1)) return true; break;
                case VK_XBUTTON2: if (getMouseButton(kMB_X2)) return true; break;
                }
            }
            else if (getVkState(vk)) return true;
        }
    }
    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId)
    {
        size_t idx = hk >> 6;
        uint64_t bit = 1ULL << (hk & 63);

        uint64_t prev = now ?
            m_hkPrev[idx].fetch_or(bit) :
            m_hkPrev[idx].fetch_and(~bit);

        return now && !(prev & bit);
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    size_t i = hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u);
    return now && (prev == 0);
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId)
    {
        size_t idx = hk >> 6;
        uint64_t bit = 1ULL << (hk & 63);

        uint64_t prev = now ?
            m_hkPrev[idx].fetch_or(bit) :
            m_hkPrev[idx].fetch_and(~bit);

        return (!now) && (prev & bit);
    }

    static std::array<std::atomic<uint8_t>, 1024> s{};
    size_t i = hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u);
    return (!now) && (prev == 1);
}

#endif
