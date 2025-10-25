#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include <cstring>

namespace {

    static constexpr USHORT kAllMouseBtnMask =
        0x0001 | 0x0002 | // LeftDown/Up
        0x0004 | 0x0008 | // RightDown/Up
        0x0010 | 0x0020 | // MiddleDown/Up
        0x0040 | 0x0080 | // X1Down/Up
        0x0100 | 0x0200;  // X2Down/Up

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF, 0, 1, 0xFF, 2, 3, 4, 0xFF
    };

    FORCE_INLINE uint8_t mask_down_from_flags(USHORT f) noexcept {
        uint8_t m = 0;
        m |= (f & 0x0001) ? 0x01 : 0; // L
        m |= (f & 0x0004) ? 0x02 : 0; // R
        m |= (f & 0x0010) ? 0x04 : 0; // M
        m |= (f & 0x0040) ? 0x08 : 0; // X1
        m |= (f & 0x0100) ? 0x10 : 0; // X2
        return m;
    }
    FORCE_INLINE uint8_t mask_up_from_flags(USHORT f) noexcept {
        uint8_t m = 0;
        m |= (f & 0x0002) ? 0x01 : 0; // L
        m |= (f & 0x0008) ? 0x02 : 0; // R
        m |= (f & 0x0020) ? 0x04 : 0; // M
        m |= (f & 0x0080) ? 0x08 : 0; // X1
        m |= (f & 0x0200) ? 0x10 : 0; // X2
        return m;
    }

} // namespace

//------------------------------------------------------------------------------
// ctor/dtor（登録はここでは行わない：二重登録を避ける）
//------------------------------------------------------------------------------
RawInputWinFilter::RawInputWinFilter()
{
    m_rid[0] = { 0x01, 0x02, 0, nullptr }; // Mouse
    m_rid[1] = { 0x01, 0x06, 0, nullptr }; // Keyboard

    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& a : m_mbCompat)     a.store(0, std::memory_order_relaxed);
    for (auto& a : m_hkPrev)       a.store(0, std::memory_order_relaxed);
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopInputThread();
}

//------------------------------------------------------------------------------
// 共通：全RAWINPUT登録の除去
//------------------------------------------------------------------------------
void RawInputWinFilter::clearAllRawInputRegistration() noexcept
{
    RAWINPUTDEVICE unreg[2];
    unreg[0] = { 0x01, 0x02, RIDEV_REMOVE, nullptr };
    unreg[1] = { 0x01, 0x06, RIDEV_REMOVE, nullptr };
    RegisterRawInputDevices(unreg, 2, sizeof(RAWINPUTDEVICE));
}

//------------------------------------------------------------------------------
// 明示登録：指定hwndへバインド
//------------------------------------------------------------------------------
bool RawInputWinFilter::registerRawInput(HWND hwnd)
{
    clearAllRawInputRegistration();
    m_rid[0] = { 0x01, 0x02, 0, hwnd };
    m_rid[1] = { 0x01, 0x06, 0, hwnd };
    return RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}

//------------------------------------------------------------------------------
// nativeEventFilter（スレッド未使用時のフォールバック）
//------------------------------------------------------------------------------
bool RawInputWinFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
    if (m_threadRunning.load(std::memory_order_acquire)) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    UINT sz = sizeof(m_rawBuf);
    if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT, m_rawBuf, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;

    const RAWINPUT& ri = *reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    if (ri.header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = ri.data.mouse;

        // 相対移動（fetch_add二重バッファ）～イベント毎に即時反映
        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            const LONG dx = (LONG)m.lLastX;
            const LONG dy = (LONG)m.lLastY;
            if ((dx | dy) != 0) accumMouseDelta(dx, dy);
        }

        // ボタン
        const USHORT f = m.usButtonFlags;
        if ((f & kAllMouseBtnMask) != 0) {
            const uint8_t downMask = mask_down_from_flags(f);
            const uint8_t upMask = mask_up_from_flags(f);

            const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
            m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

            const uint8_t chg = (uint8_t)(cur ^ nxt);
            if (chg) {
                if (chg & 0x01) m_mbCompat[0].store((nxt & 0x01) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x02) m_mbCompat[1].store((nxt & 0x02) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x04) m_mbCompat[2].store((nxt & 0x04) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x08) m_mbCompat[3].store((nxt & 0x08) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x10) m_mbCompat[4].store((nxt & 0x10) ? 1u : 0u, std::memory_order_relaxed);
            }
        }

        return false;
    }
    else if (ri.header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& k = ri.data.keyboard;

        UINT vk = k.VKey;
        const bool breakFlag = (k.Flags & RI_KEY_BREAK) != 0;
        const bool e0 = (k.Flags & RI_KEY_E0) != 0;
        const bool e1 = (k.Flags & RI_KEY_E1) != 0;
        (void)e1;

        if (vk == VK_SHIFT) {
            const UINT sc = k.MakeCode;
            vk = (sc == 0x36) ? VK_RSHIFT : VK_LSHIFT;
        }
        else if (vk == VK_CONTROL) {
            vk = e0 ? VK_RCONTROL : VK_LCONTROL;
        }
        else if (vk == VK_MENU) {
            vk = e0 ? VK_RMENU : VK_LMENU;
        }

        setVkBit(vk, !breakFlag);
        if (vk < 256u) m_vkDownCompat[vk].store(!breakFlag ? 1u : 0u, std::memory_order_relaxed);
        return false;
    }

    return false;
}

//------------------------------------------------------------------------------
// Mouse Δ（二重バッファ＋fetch_add）
//------------------------------------------------------------------------------
FORCE_INLINE void RawInputWinFilter::accumMouseDelta(LONG dx, LONG dy) noexcept
{
    if ((dx | dy) == 0) return;
    const uint8_t wi = m_writeIdx.load(std::memory_order_relaxed);
    m_delta[wi].dx.fetch_add((int32_t)dx, std::memory_order_relaxed);
    m_delta[wi].dy.fetch_add((int32_t)dy, std::memory_order_relaxed);
}

void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    const uint8_t readIdx = (uint8_t)(m_writeIdx.fetch_xor(1u, std::memory_order_acq_rel) ^ 1u);
    outDx = m_delta[readIdx].dx.exchange(0, std::memory_order_relaxed);
    outDy = m_delta[readIdx].dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas()
{
    (void)m_delta[0].dx.exchange(0, std::memory_order_relaxed);
    (void)m_delta[0].dy.exchange(0, std::memory_order_relaxed);
    (void)m_delta[1].dx.exchange(0, std::memory_order_relaxed);
    (void)m_delta[1].dy.exchange(0, std::memory_order_relaxed);
    (void)m_dxyPack.exchange(0, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// Resets
//------------------------------------------------------------------------------
void RawInputWinFilter::resetAll()
{
    resetAllKeys();
    resetMouseButtons();
    resetHotkeyEdges();
    discardDeltas();
}

void RawInputWinFilter::resetAllKeys()
{
    for (auto& w : m_state.vkDown) w.store(0, std::memory_order_relaxed);
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    for (auto& a : m_mbCompat) a.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& a : m_hkPrev) a.store(0, std::memory_order_relaxed);
}

//------------------------------------------------------------------------------
// Queries
//------------------------------------------------------------------------------
FORCE_INLINE bool RawInputWinFilter::getVkState(UINT vk) const noexcept
{
    if (vk >= 256u) return false;
    const uint64_t word = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
    return (word & (1ULL << (vk & 63u))) != 0;
}

FORCE_INLINE bool RawInputWinFilter::getMouseButton(int b) const noexcept
{
    if (b < 0 || b >= 5) return false;
    const uint8_t mb = m_state.mouseButtons.load(std::memory_order_relaxed);
    return (mb & (1u << b)) != 0;
}

bool RawInputWinFilter::keyDown(UINT vk) const noexcept { return getVkState(vk); }
bool RawInputWinFilter::mouseButtonDown(int b) const noexcept { return getMouseButton(b); }

//------------------------------------------------------------------------------
// Hotkey
//------------------------------------------------------------------------------
FORCE_INLINE void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8u) {
        const uint8_t id = kMouseButtonLUT[vk];
        if (id != 0xFF) {
            m.mouseMask = (uint8_t)(m.mouseMask | (1u << id));
            m.hasMask = 1;
            return;
        }
    }
    if (vk < 256u) {
        m.vkMask[vk >> 6] |= (1ULL << (vk & 63u));
        m.hasMask = 1;
    }
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks) noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[hk];
    std::memset(&m, 0, sizeof(m));
    for (UINT vk : vks) addVkToMask(m, vk);
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return false;
    const HotkeyMask& m = m_hkMask[hk];
    if (!m.hasMask) return false;

    for (int i = 0; i < 4; ++i) {
        const uint64_t cur = m_state.vkDown[i].load(std::memory_order_relaxed);
        if (cur & m.vkMask[i]) return true;
    }
    if (m.mouseMask != 0) {
        const uint8_t mb = m_state.mouseButtons.load(std::memory_order_relaxed);
        if ((mb & m.mouseMask) != 0) return true;
    }
    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return false;
    const bool cur = hotkeyDown(hk);
    const int wordIdx = hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    uint64_t old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
    const bool prev = (old & bit) != 0;

    if (cur && !prev) {
        uint64_t n;
        do {
            old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
            n = old | bit;
        } while (!m_hkPrev[wordIdx].compare_exchange_weak(old, n, std::memory_order_relaxed, std::memory_order_relaxed));
        return true;
    }
    if (!cur && prev) {
        uint64_t n;
        do {
            old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
            n = old & ~bit;
        } while (!m_hkPrev[wordIdx].compare_exchange_weak(old, n, std::memory_order_relaxed, std::memory_order_relaxed));
    }
    return false;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return false;
    const bool cur = hotkeyDown(hk);
    const int wordIdx = hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    uint64_t old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
    const bool prev = (old & bit) != 0;

    if (!cur && prev) {
        uint64_t n;
        do {
            old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
            n = old & ~bit;
        } while (!m_hkPrev[wordIdx].compare_exchange_weak(old, n, std::memory_order_relaxed, std::memory_order_relaxed));
        return true;
    }
    if (cur && !prev) {
        uint64_t n;
        do {
            old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
            n = old | bit;
        } while (!m_hkPrev[wordIdx].compare_exchange_weak(old, n, std::memory_order_relaxed, std::memory_order_relaxed));
    }
    return false;
}

//------------------------------------------------------------------------------
// Level3: 入力専用スレッド（レベルAのみ適用）
//------------------------------------------------------------------------------
bool RawInputWinFilter::startInputThread(bool inputSink) noexcept
{
    bool expected = false;
    if (!m_threadRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true; // already running
    }
    m_stopRequested.store(false, std::memory_order_relaxed);

    try {
        m_inputThread = std::thread([this, inputSink]() { inputThreadMain(inputSink); });
    }
    catch (...) {
        m_threadRunning.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void RawInputWinFilter::stopInputThread() noexcept
{
    if (!m_threadRunning.load(std::memory_order_acquire)) return;
    m_stopRequested.store(true, std::memory_order_release);

    if (HWND w = m_threadHwnd.load(std::memory_order_acquire)) {
        PostMessageW(w, WM_CLOSE, 0, 0);
    }
    if (m_inputThread.joinable()) m_inputThread.join();

    m_threadHwnd.store(nullptr, std::memory_order_release);
    m_threadRunning.store(false, std::memory_order_release);
}

LRESULT CALLBACK RawInputWinFilter::ThreadWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<RawInputWinFilter*>((void*)GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_INPUT:   self->handleRawInputMessage(lParam); return 0; // ★ここで処理
    case WM_CLOSE:   DestroyWindow(hWnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

void RawInputWinFilter::inputThreadMain(bool inputSink) noexcept
{
    // A-2: 優先度を固定的に引き上げ（動的ブースト無効化）
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadPriorityBoost(GetCurrentThread(), TRUE);

    // 1) メッセージ専用ウィンドウ
    WNDCLASSW wc{};
    wc.lpfnWndProc = &RawInputWinFilter::ThreadWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"RawInputWinFilterThreadWindow";
    RegisterClassW(&wc);

    HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    m_threadHwnd.store(h, std::memory_order_release);
    if (!h) return;

    // 2) 二重登録の除去→このスレッドへ登録（NOLEGACY/NOHOTKEYSは使わない）
    clearAllRawInputRegistration();
    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = inputSink ? RIDEV_INPUTSINK : 0;
    rid[0].hwndTarget = h;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = inputSink ? RIDEV_INPUTSINK : 0;
    rid[1].hwndTarget = h;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    // 3) ループ：QS_RAWINPUTで起床（A-1）、WM_INPUTだけをポンプしてDispatch（Translate不要）
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        (void)MsgWaitForMultipleObjectsEx(
            0, nullptr, INFINITE,
            QS_RAWINPUT,       // WM_INPUT到着でのみ起床
            MWMO_INPUTAVAILABLE
        );

        MSG msg;
        while (PeekMessageW(&msg, nullptr, WM_INPUT, WM_INPUT, PM_REMOVE)) {
            // WM_INPUTは翻訳不要
            DispatchMessageW(&msg);
        }
    }
}

//------------------------------------------------------------------------------
// RAWINPUT共通ハンドラ（Qt/Thread）
//------------------------------------------------------------------------------
void RawInputWinFilter::handleRawInputMessage(LPARAM lParam) noexcept
{
    UINT sz = sizeof(m_rawBuf);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, m_rawBuf, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;

    const RAWINPUT& ri = *reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    if (ri.header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = ri.data.mouse;

        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            accumMouseDelta((LONG)m.lLastX, (LONG)m.lLastY); // A：即時反映
        }

        const USHORT f = m.usButtonFlags;
        if ((f & kAllMouseBtnMask) != 0) {
            const uint8_t downMask = mask_down_from_flags(f);
            const uint8_t upMask = mask_up_from_flags(f);

            const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
            m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

            const uint8_t chg = (uint8_t)(cur ^ nxt);
            if (chg) {
                if (chg & 0x01) m_mbCompat[0].store((nxt & 0x01) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x02) m_mbCompat[1].store((nxt & 0x02) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x04) m_mbCompat[2].store((nxt & 0x04) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x08) m_mbCompat[3].store((nxt & 0x08) ? 1u : 0u, std::memory_order_relaxed);
                if (chg & 0x10) m_mbCompat[4].store((nxt & 0x10) ? 1u : 0u, std::memory_order_relaxed);
            }
        }
    }
    else if (ri.header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& k = ri.data.keyboard;

        UINT vk = k.VKey;
        const bool breakFlag = (k.Flags & RI_KEY_BREAK) != 0;
        const bool e0 = (k.Flags & RI_KEY_E0) != 0;
        const bool e1 = (k.Flags & RI_KEY_E1) != 0; (void)e1;

        if (vk == VK_SHIFT) {
            const UINT sc = k.MakeCode;
            vk = (sc == 0x36) ? VK_RSHIFT : VK_LSHIFT;
        }
        else if (vk == VK_CONTROL) {
            vk = e0 ? VK_RCONTROL : VK_LCONTROL;
        }
        else if (vk == VK_MENU) {
            vk = e0 ? VK_RMENU : VK_LMENU;
        }

        setVkBit(vk, !breakFlag);
        if (vk < 256u) m_vkDownCompat[vk].store(!breakFlag ? 1u : 0u, std::memory_order_relaxed);
    }
}

//------------------------------------------------------------------------------
// VK set/clear
//------------------------------------------------------------------------------
FORCE_INLINE void RawInputWinFilter::setVkBit(UINT vk, bool down) noexcept
{
    if (vk >= 256u) return;
    auto& word = m_state.vkDown[vk >> 6];
    const uint64_t bit = 1ULL << (vk & 63u);

    uint64_t oldv = word.load(std::memory_order_relaxed);
    uint64_t newv;
    if (down) {
        do {
            newv = oldv | bit;
        } while (!word.compare_exchange_weak(oldv, newv, std::memory_order_relaxed, std::memory_order_relaxed));
    }
    else {
        do {
            newv = oldv & ~bit;
        } while (!word.compare_exchange_weak(oldv, newv, std::memory_order_relaxed, std::memory_order_relaxed));
    }
}

#endif // _WIN32
