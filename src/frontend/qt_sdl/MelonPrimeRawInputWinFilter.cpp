#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include <windows.h>
#include <winternl.h>

//=====================================================
// NtUser API typedef
//=====================================================
typedef UINT(WINAPI* NtUserGetRawInputData_t)(
    HRAWINPUT, UINT, LPVOID, PUINT, UINT);

typedef BOOL(WINAPI* NtUserPeekMessage_t)(
    LPMSG, HWND, UINT, UINT, UINT, BOOL);

static NtUserGetRawInputData_t pNtGetRawInputData = nullptr;
static NtUserPeekMessage_t     pNtPeekMessage = nullptr;


//=====================================================
// Load NtUser functions
//=====================================================
static void LoadNtUserAPIs()
{
    HMODULE h = LoadLibraryA("win32u.dll");
    if (!h) return;

    pNtGetRawInputData = (NtUserGetRawInputData_t)
        GetProcAddress(h, "NtUserGetRawInputData");

    pNtPeekMessage = (NtUserPeekMessage_t)
        GetProcAddress(h, "NtUserPeekMessage");
}


//=====================================================
// static LUT
//=====================================================
alignas(128)
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];


//=====================================================
// Constructor
//=====================================================
RawInputWinFilter::RawInputWinFilter()
{
    LoadNtUserAPIs();

    for (auto& v : m_vkCompat) v.store(0, std::memory_order_relaxed);
    for (auto& m : m_mbCompat) m.store(0, std::memory_order_relaxed);
    for (auto& p : m_hkPrev)   p.store(0, std::memory_order_relaxed);

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
    }
}


//=====================================================
// Mouse handler (リングバッファ対応版)
//=====================================================
void RawInputWinFilter::onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWMOUSE& m = raw->data.mouse;

    LONG dx = m.lLastX;
    LONG dy = m.lLastY;

    if (dx | dy) {
        // ★ロスレス push（最大64）
        self->pushDelta((int32_t)dx, (int32_t)dy);
    }

    USHORT flags = m.usButtonFlags;
    if (!(flags & 0x03FF)) return;

    uint8_t cur = self->m_state.mouse.load(std::memory_order_relaxed);
    uint8_t d = 0, u = 0;

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

    self->m_state.mouse.store(nxt, std::memory_order_relaxed);

    self->m_mbCompat[0].store((nxt & 1) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[1].store((nxt & 2) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[2].store((nxt & 4) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[3].store((nxt & 8) ? 1 : 0, std::memory_order_relaxed);
    self->m_mbCompat[4].store((nxt & 16) ? 1 : 0, std::memory_order_relaxed);
}


//=====================================================
// Keyboard handler
//=====================================================
void RawInputWinFilter::onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw)
{
    const RAWKEYBOARD& kb = raw->data.keyboard;

    UINT vk = kb.VKey;
    bool isUp = (kb.Flags & RI_KEY_BREAK);
    bool down = !isUp;

    if (vk == VK_SHIFT) {
        UINT sc = kb.MakeCode;
        vk = (sc == 0x2A ? VK_LSHIFT :
            (sc == 0x36 ? VK_RSHIFT : VK_SHIFT));
    }
    else if (vk == VK_CONTROL) {
        vk = (kb.Flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
    }
    else if (vk == VK_MENU) {
        vk = (kb.Flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
    }

    if (vk < 256)
        self->m_vkCompat[vk].store(down, std::memory_order_relaxed);

    uint64_t bit = 1ULL << (vk & 63);
    uint32_t w = vk >> 6;

    uint64_t cur = self->m_state.vk[w].load(std::memory_order_relaxed);
    uint64_t nxt = down ? (cur | bit) : (cur & ~bit);

    self->m_state.vk[w].store(nxt, std::memory_order_relaxed);
}


//=====================================================
// Qt nativeEventFilter
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG* msg = reinterpret_cast<MSG*>(message);
    if (!msg) return false;

    if (msg->message == WM_INPUT)
    {
        UINT size = sizeof(RAWINPUT);

        if (pNtGetRawInputData) {
            pNtGetRawInputData(
                (HRAWINPUT)msg->lParam,
                RID_INPUT,
                m_rawBuf,
                &size,
                sizeof(RAWINPUTHEADER)
            );
        }
        else {
            GetRawInputData(
                (HRAWINPUT)msg->lParam,
                RID_INPUT,
                m_rawBuf,
                &size,
                sizeof(RAWINPUTHEADER)
            );
        }

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
        handlerTbl[raw->header.dwType](this, raw);
    }

    return false;
}


//=====================================================
// Thread entry
//=====================================================
DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    reinterpret_cast<RawInputWinFilter*>(param)->threadLoop();
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
    wc.lpszClassName = L"MPH_RI_HIDDEN";
    RegisterClassW(&wc);

    hiddenWnd = CreateWindowW(
        L"MPH_RI_HIDDEN", L"",
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
        BOOL got = FALSE;

        if (pNtPeekMessage) {
            got = pNtPeekMessage(&msg, nullptr, 0, 0, PM_REMOVE, FALSE);
            if (!got)
                got = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
        }
        else {
            got = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
        }

        if (!got)
            continue;

        if (msg.message == WM_INPUT)
        {
            UINT size = sizeof(RAWINPUT);

            if (pNtGetRawInputData) {
                pNtGetRawInputData(
                    (HRAWINPUT)msg.lParam,
                    RID_INPUT,
                    m_rawBuf,
                    &size,
                    sizeof(RAWINPUTHEADER)
                );
            }
            else {
                GetRawInputData(
                    (HRAWINPUT)msg.lParam,
                    RID_INPUT,
                    m_rawBuf,
                    &size,
                    sizeof(RAWINPUTHEADER)
                );
            }

            RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
            handlerTbl[raw->header.dwType](this, raw);
            continue;
        }

        if (msg.message == WM_QUIT)
            break;
    }

    hiddenWnd = nullptr;
}


//=====================================================
// HiddenWndProc
//=====================================================
LRESULT CALLBACK RawInputWinFilter::HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}


//=====================================================
// fetchMouseDelta（リングバッファ）
//=====================================================
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    outDx = 0;
    outDy = 0;

    uint32_t t = m_tail.load(std::memory_order_relaxed);
    uint32_t h = m_head.load(std::memory_order_relaxed);

    while (t != h) {
        outDx += m_dxBuf[t];
        outDy += m_dyBuf[t];
        t = (t + 1) & MOUSEBUF_MASK;
    }

    m_tail.store(t, std::memory_order_relaxed);
}


//=====================================================
// discardDeltas
//=====================================================
void RawInputWinFilter::discardDeltas()
{
    uint32_t h = m_head.load(std::memory_order_relaxed);
    m_tail.store(h, std::memory_order_relaxed);
}


//=====================================================
// resetAllKeys
//=====================================================
void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vk) w.store(0, std::memory_order_relaxed);
    for (auto& v : m_vkCompat) v.store(0, std::memory_order_relaxed);
}


//=====================================================
// resetMouseButtons
//=====================================================
void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouse.store(0, std::memory_order_relaxed);
    for (auto& m : m_mbCompat) m.store(0, std::memory_order_relaxed);
}


//=====================================================
// resetHotkeyEdges
//=====================================================
void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& p : m_hkPrev) p.store(0, std::memory_order_relaxed);
}


//=====================================================
// hotkeyDown
//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if ((unsigned)hk < kMaxHotkeyId) {
        const HotkeyMask& m = m_hkMask[hk];

        if (m.hasMask) {
            uint64_t a = m_state.vk[0].load(std::memory_order_relaxed) & m.vkMask[0];
            uint64_t b = m_state.vk[1].load(std::memory_order_relaxed) & m.vkMask[1];
            uint64_t c = m_state.vk[2].load(std::memory_order_relaxed) & m.vkMask[2];
            uint64_t d = m_state.vk[3].load(std::memory_order_relaxed) & m.vkMask[3];

            if ((a | b | c | d) != 0) return true;
            if (m_state.mouse.load(std::memory_order_relaxed) & m.mouseMask) return true;

            return false;
        }
    }

    auto it = m_hkFallback.find(hk);
    if (it != m_hkFallback.end()) {
        for (UINT vk : it->second) {
            if (vk < 8) {
                uint8_t idx = kMouseButtonLUT[vk];
                if (idx < 5 && getMouse(idx)) return true;
            }
            else if (getVk(vk)) return true;
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
    uint8_t prev = m_hkPrev[hk & 1023].exchange(now ? 1u : 0u, std::memory_order_relaxed);
    return now && !prev;
}


//=====================================================
// hotkeyReleased
//=====================================================
bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    bool now = hotkeyDown(hk);
    uint8_t prev = m_hkPrev[hk & 1023].exchange(now ? 1u : 0u, std::memory_order_relaxed);
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
        m_hkFallback[hk] = vks;
    }
}

#endif // _WIN32
