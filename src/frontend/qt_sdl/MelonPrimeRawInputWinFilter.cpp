#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

alignas(128) RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    for (auto& a : m_vkDownCompat) a.store(0);
    for (auto& b : m_mbCompat)     b.store(0);
    for (auto& w : m_hkPrevAll)    w.store(0);

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
    }
}


//=====================================================
// 【2】Mouse handler（高速 dispatch 対応）
//=====================================================
void RawInputWinFilter::onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWMOUSE& m = raw->data.mouse;

    LONG dx = m.lLastX;
    LONG dy = m.lLastY;

    if (dx | dy) {
        InterlockedExchangeAdd(&self->dx, dx);
        InterlockedExchangeAdd(&self->dy, dy);
    }

    USHORT flags = m.usButtonFlags;
    if (!(flags & (RI_MOUSE_LEFT_BUTTON_DOWN |
        RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN |
        RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN |
        RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN |
        RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN |
        RI_MOUSE_BUTTON_5_UP)))
        return;

    uint8_t cur = self->m_state.mouseButtons.load();
    uint8_t d = 0, u = 0;

    // 元の melonDS の判定そのまま
    if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)  d |= 1;
    if (flags & RI_MOUSE_LEFT_BUTTON_UP)    u |= 1;
    if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN) d |= 2;
    if (flags & RI_MOUSE_RIGHT_BUTTON_UP)   u |= 2;
    if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) d |= 4;
    if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)   u |= 4;
    if (flags & RI_MOUSE_BUTTON_4_DOWN) d |= 8;
    if (flags & RI_MOUSE_BUTTON_4_UP)   u |= 8;
    if (flags & RI_MOUSE_BUTTON_5_DOWN) d |= 16;
    if (flags & RI_MOUSE_BUTTON_5_UP)   u |= 16;

    uint8_t nxt = (cur | d) & ~u;

    self->m_state.mouseButtons.store(nxt);

    // Qt fallback 互換
    self->m_mbCompat[0].store((nxt & 1) ? 1 : 0);
    self->m_mbCompat[1].store((nxt & 2) ? 1 : 0);
    self->m_mbCompat[2].store((nxt & 4) ? 1 : 0);
    self->m_mbCompat[3].store((nxt & 8) ? 1 : 0);
    self->m_mbCompat[4].store((nxt & 16) ? 1 : 0);
}


//=====================================================
// 【2】Keyboard handler（高速 dispatch 対応）
//=====================================================
void RawInputWinFilter::onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWKEYBOARD& kb = raw->data.keyboard;

    UINT vk = kb.VKey;
    bool isUp = (kb.Flags & RI_KEY_BREAK);
    bool down = !isUp;

    // 元の melonDS と同じ Shift/CTRL/ALT 修正
    if (vk == VK_SHIFT) {
        UINT code = kb.MakeCode;
        if (code == 0x2A) vk = VK_LSHIFT;
        else if (code == 0x36) vk = VK_RSHIFT;
    }
    else if (vk == VK_CONTROL) {
        vk = (kb.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
    }
    else if (vk == VK_MENU) {
        vk = (kb.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
    }

    if (vk < 256)
        self->m_vkDownCompat[vk].store(down);

    uint32_t w = vk >> 6;
    uint64_t bit = 1ULL << (vk & 63);

    uint64_t cur = self->m_state.vkDown[w].load();
    uint64_t want = down ? bit : 0ULL;
    uint64_t mask = (cur ^ want) & bit;

    self->m_state.vkDown[w].store(cur ^ mask);
}


//=====================================================
// nativeEventFilter
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    if (msg->message == WM_INPUT) {
        RAWINPUT* raw = nullptr;
        UINT size = sizeof(RAWINPUT);

        GetRawInputData(
            (HRAWINPUT)msg->lParam,
            RID_INPUT,
            m_rawBuf,
            &size,
            sizeof(RAWINPUTHEADER)
        );

        raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);

        handlerTbl[raw->header.dwType](this, raw);
    }
    return false;
}


//=====================================================
// Thread
//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    reinterpret_cast<RawInputWinFilter*>(param)->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MPH_RI_HIDDEN_CLASS_LV_MIN";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_HIDDEN_CLASS_LV_MIN", L"",
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
        DWORD w = MsgWaitForMultipleObjectsEx(
            0, nullptr,
            INFINITE,
            QS_INPUT,
            MWMO_INPUTAVAILABLE
        );

        if (w == WAIT_FAILED)
            continue;

        //==================================================
        // ★ DispatchMessage を完全に排除
        // ★ WM_INPUT だけ処理する
        //==================================================
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            switch (msg.message)
            {
            case WM_INPUT:
            {
                UINT size = sizeof(RAWINPUT);
                GetRawInputData(
                    (HRAWINPUT)msg.lParam,
                    RID_INPUT,
                    m_rawBuf,
                    &size,
                    sizeof(RAWINPUTHEADER)
                );

                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
                handlerTbl[raw->header.dwType](this, raw);
                break;
            }

            case WM_QUIT:
                goto exit;

            default:
                // 何もしない（DispatchMessage を呼ばない）
                break;
            }
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
        // ここでは GetRawInputData しない → threadLoop 側に集中させる
        return 0;

    case WM_CLOSE:
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
    outDx = InterlockedExchange(&dx, 0);
    outDy = InterlockedExchange(&dy, 0);
}

void RawInputWinFilter::discardDeltas()
{
    InterlockedExchange(&dx, 0);
    InterlockedExchange(&dy, 0);
}

void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vkDown) w.store(0);
    for (auto& a : m_vkDownCompat) a.store(0);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0);
    for (auto& b : m_mbCompat) b.store(0);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& w : m_hkPrevAll) w.store(0);
}


//------------------------------
// hotkeyDown / pressed / released
//------------------------------
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) {
        const HotkeyMask& m = m_hkMask[hk];
        if (m.hasMask) {
            uint64_t a = m_state.vkDown[0].load() & m.vkMask[0];
            uint64_t b = m_state.vkDown[1].load() & m.vkMask[1];
            uint64_t c = m_state.vkDown[2].load() & m.vkMask[2];
            uint64_t d = m_state.vkDown[3].load() & m.vkMask[3];

            uint64_t r = (a | b) | (c | d);

            bool vkHit = (r != 0);
            bool mouseHit = (m_state.mouseButtons.load() & m.mouseMask);

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
    uint8_t prev = m_hkPrevAll[hk & 1023].exchange(now ? 1u : 0u);
    return now && !prev;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);
    uint8_t prev = m_hkPrevAll[hk & 1023].exchange(now ? 1u : 0u);
    return (!now) && prev;
}


//=====================================================
// setHotkeyVks
//=====================================================
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId) {
        HotkeyMask& m = m_hkMask[hk];
        std::memset(&m, 0, sizeof(m));

        size_t n = (vks.size() > 64 ? 64 : vks.size());
        for (size_t i = 0; i < n; i++)
            addVkToMask(m, vks[i]);
    }
    else {
        m_hkToVk[hk] = vks;
    }
}

#endif
