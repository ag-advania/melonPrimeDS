#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include <cstring>

namespace {
    static constexpr USHORT kAllMouseBtnMask =
        0x0001 | 0x0002 |
        0x0004 | 0x0008 |
        0x0010 | 0x0020 |
        0x0040 | 0x0080 |
        0x0100 | 0x0200;

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF, 0, 1, 0xFF, 2, 3, 4, 0xFF
    };

    FORCE_INLINE uint8_t mask_down_from_flags(USHORT f) noexcept {
        uint8_t m = 0;
        m |= (f & 0x0001) ? 0x01 : 0;
        m |= (f & 0x0004) ? 0x02 : 0;
        m |= (f & 0x0010) ? 0x04 : 0;
        m |= (f & 0x0040) ? 0x08 : 0;
        m |= (f & 0x0100) ? 0x10 : 0;
        return m;
    }
    FORCE_INLINE uint8_t mask_up_from_flags(USHORT f) noexcept {
        uint8_t m = 0;
        m |= (f & 0x0002) ? 0x01 : 0;
        m |= (f & 0x0008) ? 0x02 : 0;
        m |= (f & 0x0020) ? 0x04 : 0;
        m |= (f & 0x0080) ? 0x08 : 0;
        m |= (f & 0x0200) ? 0x10 : 0;
        return m;
    }
} // namespace

//------------------------------------------------------------------------------
RawInputWinFilter::RawInputWinFilter()
{
    m_rid[0] = { 0x01, 0x02, 0, nullptr }; // mouse
    m_rid[1] = { 0x01, 0x06, 0, nullptr }; // keyboard
    RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE));

#if RAWFILTER_ENABLE_COMPAT_MIRRORS
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& a : m_mbCompat)     a.store(0, std::memory_order_relaxed);
#endif
    for (auto& a : m_hkPrev)       a.store(0, std::memory_order_relaxed);
}

bool RawInputWinFilter::registerRawInput(HWND hwnd)
{
    m_rid[0] = { 0x01, 0x02, 0, hwnd };
    m_rid[1] = { 0x01, 0x06, 0, hwnd };
    return RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}

bool RawInputWinFilter::registerRawInputEx(HWND hwnd, bool inputSink)
{
    const DWORD flags = inputSink ? RIDEV_INPUTSINK : 0;
    m_rid[0] = { 0x01, 0x02, flags, hwnd };
    m_rid[1] = { 0x01, 0x06, flags, hwnd };
    return RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}

bool RawInputWinFilter::registerRawInputEx2(HWND hwnd, DWORD flags)
{
    m_rid[0] = { 0x01, 0x02, flags, hwnd };
    m_rid[1] = { 0x01, 0x06, flags, hwnd };
    return RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}

//------------------------------------------------------------------------------
bool RawInputWinFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    UINT size = sizeof(RAWINPUT);
    if (GetRawInputData((HRAWINPUT)msg->lParam, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;

    handleRawInput(*reinterpret_cast<const RAWINPUT*>(m_rawBuf));
    return false; // Qtにも流す（が、ここで事実上処理は完了）
}

//------------------------------------------------------------------------------
// リアルタイム化のキモ：フレーム先頭でWM_INPUTを即時ドレイン
//------------------------------------------------------------------------------
bool RawInputWinFilter::drainRawInputNow(int maxLoops)
{
    MSG m;
    bool any = false;
    int  cnt = 0;

    // UIスレッド（このウィンドウキュー）に残っているWM_INPUTを全部抜く
    while (cnt < maxLoops && PeekMessageW(&m, nullptr, WM_INPUT, WM_INPUT, PM_REMOVE)) {
        if (m.message == WM_INPUT) {
            UINT size = sizeof(RAWINPUT);
            if (GetRawInputData((HRAWINPUT)m.lParam, RID_INPUT, m_rawBuf, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                handleRawInput(*reinterpret_cast<const RAWINPUT*>(m_rawBuf));
                any = true;
            }
        }
        else {
            // ここに来ることは通常ないが、保険として戻す
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        ++cnt;
    }
    return any;
}

//------------------------------------------------------------------------------
FORCE_INLINE void RawInputWinFilter::handleRawInput(const RAWINPUT& ri) noexcept
{
    if (ri.header.dwType == RIM_TYPEMOUSE) {
        handleRawMouse(ri.data.mouse);
    }
    else if (ri.header.dwType == RIM_TYPEKEYBOARD) {
        handleRawKeyboard(ri.data.keyboard);
    }
}

FORCE_INLINE void RawInputWinFilter::handleRawMouse(const RAWMOUSE& m) noexcept
{
    if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        const LONG dx = (LONG)m.lLastX;
        const LONG dy = (LONG)m.lLastY;
        if ((dx | dy) != 0) accumMouseDelta(dx, dy);
    }

    const USHORT f = m.usButtonFlags;
    if ((f & kAllMouseBtnMask) != 0) {
        const uint8_t downMask = mask_down_from_flags(f);
        const uint8_t upMask = mask_up_from_flags(f);

        const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
        m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

#if RAWFILTER_ENABLE_COMPAT_MIRRORS
        const uint8_t chg = (uint8_t)(cur ^ nxt);
        if (chg) {
            if (chg & 0x01) m_mbCompat[0].store((nxt & 0x01) ? 1u : 0u, std::memory_order_relaxed);
            if (chg & 0x02) m_mbCompat[1].store((nxt & 0x02) ? 1u : 0u, std::memory_order_relaxed);
            if (chg & 0x04) m_mbCompat[2].store((nxt & 0x04) ? 1u : 0u, std::memory_order_relaxed);
            if (chg & 0x08) m_mbCompat[3].store((nxt & 0x08) ? 1u : 0u, std::memory_order_relaxed);
            if (chg & 0x10) m_mbCompat[4].store((nxt & 0x10) ? 1u : 0u, std::memory_order_relaxed);
        }
#endif
    }
}

FORCE_INLINE void RawInputWinFilter::handleRawKeyboard(const RAWKEYBOARD& k) noexcept
{
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
#if RAWFILTER_ENABLE_COMPAT_MIRRORS
    if (vk < 256u) m_vkDownCompat[vk].store(!breakFlag ? 1u : 0u, std::memory_order_relaxed);
#endif
}

//------------------------------------------------------------------------------
// Mouse delta（ダブルバッファ＋fetch_add）
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
// Reset / Query / Hotkey（同前）
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
#if RAWFILTER_ENABLE_COMPAT_MIRRORS
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
#endif
}

void RawInputWinFilter::resetMouseButtons()
{
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
#if RAWFILTER_ENABLE_COMPAT_MIRRORS
    for (auto& a : m_mbCompat) a.store(0, std::memory_order_relaxed);
#endif
}

void RawInputWinFilter::resetHotkeyEdges()
{
    for (auto& a : m_hkPrev) a.store(0, std::memory_order_relaxed);
}

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

void RawInputWinFilter::setHotkeyVksRaw(int hk, const UINT* vks, size_t count) noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[hk];
    std::memset(&m, 0, sizeof(m));
    for (size_t i = 0; i < count; ++i) addVkToMask(m, vks[i]);
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks) noexcept
{
    setHotkeyVksRaw(hk, vks.data(), vks.size());
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

    const uint64_t old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
    const bool prev = (old & bit) != 0;

    if (cur) {
        if (!prev) {
            m_hkPrev[wordIdx].fetch_or(bit, std::memory_order_relaxed);
            return true;
        }
    }
    else {
        if (prev) m_hkPrev[wordIdx].fetch_and(~bit, std::memory_order_relaxed);
    }
    return false;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    if (hk < 0 || hk >= kMaxHotkeyId) return false;
    const bool cur = hotkeyDown(hk);
    const int wordIdx = hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);

    const uint64_t old = m_hkPrev[wordIdx].load(std::memory_order_relaxed);
    const bool prev = (old & bit) != 0;

    if (!cur) {
        if (prev) {
            m_hkPrev[wordIdx].fetch_and(~bit, std::memory_order_relaxed);
            return true;
        }
    }
    else {
        if (!prev) m_hkPrev[wordIdx].fetch_or(bit, std::memory_order_relaxed);
    }
    return false;
}

FORCE_INLINE void RawInputWinFilter::setVkBit(UINT vk, bool down) noexcept
{
    if (vk >= 256u) return;
    auto& word = m_state.vkDown[vk >> 6];
    const uint64_t bit = 1ULL << (vk & 63u);
    if (down) {
        (void)word.fetch_or(bit, std::memory_order_relaxed);
    }
    else {
        (void)word.fetch_and(~bit, std::memory_order_relaxed);
    }
}

#endif // _WIN32
