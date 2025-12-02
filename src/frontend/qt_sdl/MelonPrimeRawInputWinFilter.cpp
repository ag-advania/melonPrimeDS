#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>


//---------------------------------------------------------
// LUT
//---------------------------------------------------------
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

static struct InitRawLut {
    InitRawLut() {
        for (int i = 0; i < 1024; i++) {
            uint8_t d = 0, u = 0;

            if (i & 0x0001)d |= 0x01;
            if (i & 0x0004)d |= 0x02;
            if (i & 0x0010)d |= 0x04;
            if (i & 0x0040)d |= 0x08;
            if (i & 0x0100)d |= 0x10;

            if (i & 0x0002)u |= 0x01;
            if (i & 0x0008)u |= 0x02;
            if (i & 0x0020)u |= 0x04;
            if (i & 0x0080)u |= 0x08;
            if (i & 0x0200)u |= 0x10;

            RawInputWinFilter::s_btnLut[i] = { d,u };
        }
    }
} g_InitRawLut;


//---------------------------------------------------------
// コンストラクタ：専用スレッド起動
//---------------------------------------------------------
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat)a.store(0, std::memory_order_relaxed);
    for (auto& a : m_mbCompat)a.store(0, std::memory_order_relaxed);
    for (auto& w : m_hkPrev)w.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));

    runThread.store(true);
    hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);
}


//---------------------------------------------------------
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


//---------------------------------------------------------
// Qt fallback → 完全無効化
//---------------------------------------------------------
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void*, qintptr*)
{
    return false;
}


//---------------------------------------------------------
// ThreadFunc
//---------------------------------------------------------
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID p)
{
    reinterpret_cast<RawInputWinFilter*>(p)->threadLoop();
    return 0;
}


//---------------------------------------------------------
// threadLoop（MWMO最速）
//---------------------------------------------------------
void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RAWINPUT_HIDDEN";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RAWINPUT_HIDDEN", L"",
        0,
        0, 0, 0, 0,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        this
    );

    RAWINPUTDEVICE rid[2];
    rid[0] = { 1,2,0,hiddenWnd };
    rid[1] = { 1,6,0,hiddenWnd };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    MSG msg;

    while (runThread.load(std::memory_order_relaxed))
    {
        DWORD w = MsgWaitForMultipleObjectsEx(
            0, nullptr,
            INFINITE,
            QS_INPUT,
            MWMO_INPUTAVAILABLE | MWMO_ALERTABLE
        );

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                goto exit_loop;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Joy2Key補正
        syncWithSendInput();
    }

exit_loop:
    hiddenWnd = nullptr;
}


//---------------------------------------------------------
// HiddenWndProc（RawInputのみ処理）
//---------------------------------------------------------
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
        if (!self) return 0;

        UINT size = sizeof(self->m_rawBuf);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT,
            self->m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return 0;

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(self->m_rawBuf);

        //---------------- Mouse ----------------
        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            const RAWMOUSE& m = raw->data.mouse;

            LONG dx_ = m.lLastX;
            LONG dy_ = m.lLastY;

            if (dx_ | dy_) {
                self->dx.fetch_add((int)dx_, std::memory_order_relaxed);
                self->dy.fetch_add((int)dy_, std::memory_order_relaxed);
            }

            USHORT f = m.usButtonFlags;
            if (f & RawInputWinFilter::kAllMouseBtnMask)
            {
                auto lut = RawInputWinFilter::s_btnLut[f & 0x3FF];

                uint8_t cur = self->m_state.mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt = (cur | lut.down) & ~lut.up;
                self->m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

                if (lut.down | lut.up) {
                    if (lut.down & 0x01)self->m_mbCompat[kMB_Left].store(1, std::memory_order_relaxed);
                    if (lut.up & 0x01)self->m_mbCompat[kMB_Left].store(0, std::memory_order_relaxed);

                    if (lut.down & 0x02)self->m_mbCompat[kMB_Right].store(1, std::memory_order_relaxed);
                    if (lut.up & 0x02)self->m_mbCompat[kMB_Right].store(0, std::memory_order_relaxed);

                    if (lut.down & 0x04)self->m_mbCompat[kMB_Middle].store(1, std::memory_order_relaxed);
                    if (lut.up & 0x04)self->m_mbCompat[kMB_Middle].store(0, std::memory_order_relaxed);

                    if (lut.down & 0x08)self->m_mbCompat[kMB_X1].store(1, std::memory_order_relaxed);
                    if (lut.up & 0x08)self->m_mbCompat[kMB_X1].store(0, std::memory_order_relaxed);

                    if (lut.down & 0x10)self->m_mbCompat[kMB_X2].store(1, std::memory_order_relaxed);
                    if (lut.up & 0x10)self->m_mbCompat[kMB_X2].store(0, std::memory_order_relaxed);
                }
            }
            return 0;
        }

        //---------------- Keyboard ----------------
        if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& kb = raw->data.keyboard;

            UINT vk = kb.VKey;
            bool isUp = (kb.Flags & RI_KEY_BREAK) != 0;

            switch (vk) {
            case VK_SHIFT:   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX); break;
            case VK_CONTROL: vk = (kb.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL; break;
            case VK_MENU:    vk = (kb.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU; break;
            default: break;
            }

            self->setVkBit(vk, !isUp);

            if (vk < self->m_vkDownCompat.size())
                self->m_vkDownCompat[vk].store(!isUp, std::memory_order_relaxed);

            return 0;
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


//---------------------------------------------------------
// Joy2Key（SendInput）補正
//---------------------------------------------------------
//---------------------------------------------------------
// Joy2Key（SendInput）補正：必要なキーだけ同期
//---------------------------------------------------------
void RawInputWinFilter::syncWithSendInput()
{
    //-----------------------------------------------------
    // 1) Keyboard: ホットキーで使われている VK だけ補正
    //-----------------------------------------------------
    for (size_t hk = 0; hk < kMaxHotkeyId; hk++)
    {
        const HotkeyMask& m = m_hkMask[hk];
        if (!m.hasMask) continue; // このHKはVK/Mouseどちらも未使用

        // 256bit を4ワードとして走査
        for (int wi = 0; wi < 4; wi++)
        {
            uint64_t mask = m.vkMask[wi];
            while (mask)
            {
                // 最下位セットビット
                uint64_t bit = mask & -mask;
                int vk = (wi << 6) | __builtin_ctzll(mask);

                SHORT st = GetAsyncKeyState(vk);
                bool down = (st & 0x8000) != 0;

                bool raw = getVkState(vk);
                if (raw != down)
                    setVkBit(vk, down);

                m_vkDownCompat[vk].store(down, std::memory_order_relaxed);

                mask ^= bit; // 次のビットへ
            }
        }
    }

    //-----------------------------------------------------
    // 2) Mouse: ホットキーで使われている Mouse bit だけ補正
    //-----------------------------------------------------
    for (size_t hk = 0; hk < kMaxHotkeyId; hk++)
    {
        const HotkeyMask& m = m_hkMask[hk];
        if (!m.hasMask) continue;
        const uint8_t mm = m.mouseMask;
        if (!mm) continue;

        // L/R/M/X1/X2 の5bitを見る
        for (int idx = 0; idx < 5; idx++)
        {
            const uint8_t bit = (1u << idx);
            if (!(mm & bit)) continue; // 未使用

            UINT vk =
                (idx == kMB_Left) ? VK_LBUTTON :
                (idx == kMB_Right) ? VK_RBUTTON :
                (idx == kMB_Middle) ? VK_MBUTTON :
                (idx == kMB_X1) ? VK_XBUTTON1 :
                VK_XBUTTON2;

            SHORT st = GetAsyncKeyState(vk);
            bool down = (st & 0x8000) != 0;

            bool raw = getMouseButton(idx);
            if (raw != down)
            {
                uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt = down ? (cur | bit) : (cur & ~bit);
                m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
            }

            m_mbCompat[idx].store(down, std::memory_order_relaxed);
        }
    }
}



//---------------------------------------------------------
// Utility functions
//---------------------------------------------------------
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
    for (auto& w : m_state.vkDown)w.store(0, std::memory_order_relaxed);
    for (auto& a : m_vkDownCompat)a.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    for (auto& m : m_mbCompat)m.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrev)w.store(0, std::memory_order_relaxed);
}

FORCE_INLINE void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8) {
        uint8_t b = kMouseButtonLUT[vk];
        if (b < 5) { m.mouseMask |= (1 << b); m.hasMask = 1; }
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
        for (size_t i = 0; i < n; i++)addVkToMask(m, vks[i]);
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

            bool vkHit = (r0 | r1 | r2 | r3) != 0;
            bool mouseHit = (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0;

            if (vkHit | mouseHit) return true;
        }
    }

    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end()) {
        for (UINT vk : it->second) {
            if (vk < 8) {
                uint8_t b = kMouseButtonLUT[vk];
                if (b < 5 && getMouseButton(b))return true;
            }
            else if (getVkState(vk))return true;
        }
    }
    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) {
        size_t idx = hk >> 6;
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
    size_t i = hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);
    return now && !prev;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);

    if ((unsigned)hk < kMaxHotkeyId) {
        size_t idx = hk >> 6;
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
    size_t i = hk & 1023;
    uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_relaxed);
    return (!now) && prev;
}

#endif
