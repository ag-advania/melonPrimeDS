#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"

RawInputWinFilter::RawInputWinFilter()
{
    // RawInput 登録（mouse / keyboard）
    m_rid[0] = { 0x01, 0x02, 0, nullptr }; // Mouse
    m_rid[1] = { 0x01, 0x06, 0, nullptr }; // Keyboard
    RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE));

    // 初期化
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);
    for (auto& w : m_hkPrevAll)    w.store(0, std::memory_order_relaxed);

    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
}

RawInputWinFilter::~RawInputWinFilter()
{
    // 登録解除
    m_rid[0].dwFlags = RIDEV_REMOVE; m_rid[0].hwndTarget = nullptr;
    m_rid[1].dwFlags = RIDEV_REMOVE; m_rid[1].hwndTarget = nullptr;
    RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE));
}

bool RawInputWinFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    UINT size = sizeof(m_rawBuf);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
        RID_INPUT, m_rawBuf, &size,
        sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        return false;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    const DWORD type = raw->header.dwType;

    // ---------- Mouse ----------
    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;

        // 相対移動
        const LONG dx_ = m.lLastX;
        const LONG dy_ = m.lLastY;
        if ((dx_ | dy_) != 0) {
            dx.fetch_add((int)dx_, std::memory_order_relaxed);
            dy.fetch_add((int)dy_, std::memory_order_relaxed);
        }

        // ボタン
        const USHORT f = m.usButtonFlags;
        if ((f & kAllMouseBtnMask) == 0) return false;

        const uint8_t downMask =
            ((f & 0x0001) ? 0x01 : 0) | ((f & 0x0004) ? 0x02 : 0) |
            ((f & 0x0010) ? 0x04 : 0) | ((f & 0x0040) ? 0x08 : 0) |
            ((f & 0x0100) ? 0x10 : 0);

        const uint8_t upMask =
            ((f & 0x0002) ? 0x01 : 0) | ((f & 0x0008) ? 0x02 : 0) |
            ((f & 0x0020) ? 0x04 : 0) | ((f & 0x0080) ? 0x08 : 0) |
            ((f & 0x0200) ? 0x10 : 0);

        const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
        m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

        // 互換
        if (downMask | upMask) {
            if (downMask & 0x01) m_mbCompat[kMB_Left].store(1, std::memory_order_relaxed);
            if (upMask & 0x01) m_mbCompat[kMB_Left].store(0, std::memory_order_relaxed);
            if (downMask & 0x02) m_mbCompat[kMB_Right].store(1, std::memory_order_relaxed);
            if (upMask & 0x02) m_mbCompat[kMB_Right].store(0, std::memory_order_relaxed);
            if (downMask & 0x04) m_mbCompat[kMB_Middle].store(1, std::memory_order_relaxed);
            if (upMask & 0x04) m_mbCompat[kMB_Middle].store(0, std::memory_order_relaxed);
            if (downMask & 0x08) m_mbCompat[kMB_X1].store(1, std::memory_order_relaxed);
            if (upMask & 0x08) m_mbCompat[kMB_X1].store(0, std::memory_order_relaxed);
            if (downMask & 0x10) m_mbCompat[kMB_X2].store(1, std::memory_order_relaxed);
            if (upMask & 0x10) m_mbCompat[kMB_X2].store(0, std::memory_order_relaxed);
        }

        return false;
    }

    // ---------- Keyboard ----------
    if (type == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;
        const bool isUp = (flags & RI_KEY_BREAK) != 0;

        // 特殊キー正規化
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
        default:
            break;
        }

        setVkBit(vk, !isUp);

        if (vk < m_vkDownCompat.size())
            m_vkDownCompat[vk].store(!isUp, std::memory_order_relaxed);

        return false;
    }

    return false;
}

void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    outDx = dx.exchange(0, std::memory_order_relaxed);
    outDy = dy.exchange(0, std::memory_order_relaxed);
}

void RawInputWinFilter::discardDeltas()
{
    (void)dx.exchange(0, std::memory_order_relaxed);
    (void)dy.exchange(0, std::memory_order_relaxed);
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

FORCE_INLINE void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    if (vk < 8) {
        const uint8_t b = kMouseButtonLUT[vk];
        if (b < 5) { m.mouseMask |= (1u << b); m.hasMask = 1; }
    }
    else if (vk < 256) {
        m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
        m.hasMask = 1;
    }
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    if ((unsigned)hk < kMaxHotkeyId) {
        HotkeyMask& m = m_hkMask[(size_t)hk];
        std::memset(&m, 0, sizeof(m));

        // v2 の「vkマッピング最適化」：8個制限を撤廃し、maskを完全に構築する
        for (UINT vk : vks) {
            if (vk == 0) continue;
            addVkToMask(m, vk);
        }

        // 念のため：同じhkがdyn側に居たら消す
        auto itD = m_hkMaskDyn.find(hk);
        if (itD != m_hkMaskDyn.end())
            m_hkMaskDyn.erase(itD);

        return;
    }

    // 旧互換も残す（vector）
    m_hkToVk[hk] = vks;

    // hk>=kMaxHotkeyId でも mask を生成して低サイクル化（v2 の取り込み）
    HotkeyMask dm{};
    std::memset(&dm, 0, sizeof(dm));

    for (UINT vk : vks) {
        if (vk == 0) continue;
        addVkToMask(dm, vk);
    }

    if (dm.hasMask)
        m_hkMaskDyn[hk] = dm;
    else
        m_hkMaskDyn.erase(hk);
}


bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    // mask判定（hk<kMax と hk>=kMax の両方で使う）
    auto maskDown = [&](const HotkeyMask& m) noexcept -> bool
    {
        const uint64_t x0 = m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0];
        const uint64_t x1 = m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1];
        const uint64_t x2 = m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2];
        const uint64_t x3 = m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3];

        if ((x0 | x1 | x2 | x3) != 0ULL)
            return true;

        const uint8_t mb = m_state.mouseButtons.load(std::memory_order_relaxed);
        if ((m.mouseMask & mb) != 0u)
            return true;

        return false;
    };

    // 1) 既存：固定hotkey（配列mask）
    if ((unsigned)hk < (unsigned)kMaxHotkeyId) {
        const HotkeyMask& m = m_hkMask[(size_t)hk];
        if (m.hasMask)
            return maskDown(m);
    }

    // 2) 追加：動的hotkey（unordered_mapのmask）
    {
        auto itM = m_hkMaskDyn.find(hk);
        if (itM != m_hkMaskDyn.end() && itM->second.hasMask)
            return maskDown(itM->second);
    }

    // 3) 最後の保険：旧vector互換
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
    const uint32_t idx = (uint32_t)hk & 1023u;
    const uint32_t prev = m_hkPrevAll[idx].exchange(uint32_t(now), std::memory_order_acq_rel);
    return (!prev & uint32_t(now)) != 0u;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    const bool now = hotkeyDown(hk);
    const uint32_t idx = (uint32_t)hk & 1023u;
    const uint32_t prev = m_hkPrevAll[idx].exchange(uint32_t(now), std::memory_order_acq_rel);
    return (prev & uint32_t(!now)) != 0u;
}


#endif // _WIN32
