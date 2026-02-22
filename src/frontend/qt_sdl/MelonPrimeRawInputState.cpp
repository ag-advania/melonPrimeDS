#ifdef _WIN32
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

namespace MelonPrime {

    std::array<InputState::BtnLutEntry, 1024> InputState::s_btnLut;
    std::array<InputState::VkRemapEntry, 256> InputState::s_vkRemap;
    std::array<uint16_t, 512> InputState::s_makeCodeLut;
    uint16_t InputState::s_scancodeLShift = 0;
    uint16_t InputState::s_scancodeRShift = 0;
    std::once_flag InputState::s_initFlag;
    NtUserGetRawInputBuffer_t InputState::s_fnBestGetRawInputBuffer = ::GetRawInputBuffer;

    InputState::InputState() noexcept {
        for (auto& vk : m_vkDown)
            vk.store(0, std::memory_order_relaxed);
        m_mouseButtons.store(0, std::memory_order_relaxed);

        m_accumMouseX.store(0, std::memory_order_relaxed);
        m_accumMouseY.store(0, std::memory_order_relaxed);
        m_lastReadMouseX = 0;
        m_lastReadMouseY = 0;

        std::memset(&m_hkMasks, 0, sizeof(HotkeyMasks));
        m_hkPrev = 0;
        m_boundHotkeys = 0;
    }

    void InputState::InitializeTables() noexcept {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();
            if (WinInternal::fnNtUserGetRawInputBuffer) {
                s_fnBestGetRawInputBuffer = WinInternal::fnNtUserGetRawInputBuffer;
            }

            for (int i = 0; i < 1024; ++i) {
                uint8_t d = 0, u = 0;
                if (i & RI_MOUSE_BUTTON_1_DOWN) d |= 0x01;
                if (i & RI_MOUSE_BUTTON_1_UP)   u |= 0x01;
                if (i & RI_MOUSE_BUTTON_2_DOWN) d |= 0x02;
                if (i & RI_MOUSE_BUTTON_2_UP)   u |= 0x02;
                if (i & RI_MOUSE_BUTTON_3_DOWN) d |= 0x04;
                if (i & RI_MOUSE_BUTTON_3_UP)   u |= 0x04;
                if (i & RI_MOUSE_BUTTON_4_DOWN) d |= 0x08;
                if (i & RI_MOUSE_BUTTON_4_UP)   u |= 0x08;
                if (i & RI_MOUSE_BUTTON_5_DOWN) d |= 0x10;
                if (i & RI_MOUSE_BUTTON_5_UP)   u |= 0x10;
                s_btnLut[i] = { d, u };
            }

            s_vkRemap.fill(VkRemapEntry{ 0, 0 });
            auto setRemap = [](int base, int l, int r) {
                s_vkRemap[base] = { static_cast<uint8_t>(l), static_cast<uint8_t>(r) };
                };
            setRemap(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
            setRemap(VK_MENU, VK_LMENU, VK_RMENU);
            setRemap(VK_SHIFT, VK_LSHIFT, VK_RSHIFT);

            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));

            s_makeCodeLut.fill(0);
            for (UINT i = 1; i < 512; ++i) {
                s_makeCodeLut[i] = static_cast<uint16_t>(MapVirtualKeyW(i, MAPVK_VSC_TO_VK_EX));
            }
            });
    }

    void InputState::processRawInput(HRAWINPUT hRaw) noexcept {
        alignas(16) uint8_t rawBuf[sizeof(RAWINPUT)];
        UINT size = sizeof(rawBuf);
        UINT result;

        if (LIKELY(WinInternal::fnNtUserGetRawInputData != nullptr)) {
            result = WinInternal::fnNtUserGetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
        }
        else {
            result = GetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
        }

        if (UNLIKELY(result == UINT(-1) || result == 0)) return;
        const auto* raw = reinterpret_cast<const RAWINPUT*>(rawBuf);

        switch (raw->header.dwType) {
        case RIM_TYPEMOUSE: {
            const RAWMOUSE& m = raw->data.mouse;
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                if (m.lLastX) {
                    const int64_t cur = m_accumMouseX.load(std::memory_order_relaxed);
                    m_accumMouseX.store(cur + m.lLastX, std::memory_order_release);
                }
                if (m.lLastY) {
                    const int64_t cur = m_accumMouseY.load(std::memory_order_relaxed);
                    m_accumMouseY.store(cur + m.lLastY, std::memory_order_release);
                }
            }
            const USHORT flags = m.usButtonFlags & 0x03FF;
            if (flags) {
                const auto& lut = s_btnLut[flags];
                if (lut.downBits | lut.upBits) {
                    const uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
                    m_mouseButtons.store(
                        (cur | lut.downBits) & ~lut.upBits,
                        std::memory_order_release);
                }
            }
            break;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;

            if (UNLIKELY(vk == 0)) {
                vk = LIKELY(kb.MakeCode < 512) ? s_makeCodeLut[kb.MakeCode]
                    : MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            }

            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                setVkBit(vk, !(kb.Flags & RI_KEY_BREAK));
                std::atomic_thread_fence(std::memory_order_release);
            }
            break;
        }
        }
    }

    void InputState::processRawInputBatched() noexcept {
        alignas(64) static thread_local uint8_t buffer[16384];

        int32_t localAccX = 0, localAccY = 0;
        uint64_t localKeyDeltaDown[4] = {};
        uint64_t localKeyDeltaUp[4] = {};
        bool hasKeyChanges = false;

        const uint8_t initialBtnState = m_mouseButtons.load(std::memory_order_relaxed);
        uint8_t finalBtnState = initialBtnState;

        for (;;) {
            UINT size = sizeof(buffer);
            UINT count = s_fnBestGetRawInputBuffer(
                reinterpret_cast<PRAWINPUT>(buffer), &size, sizeof(RAWINPUTHEADER));
            if (count == 0 || count == UINT(-1)) break;

            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);
            for (UINT i = 0; i < count; ++i) {
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& m = raw->data.mouse;
                    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        localAccX += m.lLastX;
                        localAccY += m.lLastY;
                    }
                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        finalBtnState = (finalBtnState & ~lut.upBits) | lut.downBits;
                    }
                }
                else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;

                    if (UNLIKELY(vk == 0)) {
                        vk = LIKELY(kb.MakeCode < 512) ? s_makeCodeLut[kb.MakeCode]
                            : MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                    }

                    if (LIKELY(vk > 0 && vk < 255)) {
                        vk = remapVk(vk, kb.MakeCode, kb.Flags);
                        const int idx = vk >> 6;
                        const uint64_t bit = 1ULL << (vk & 63);

                        const uint64_t isUpMask = (kb.Flags & RI_KEY_BREAK) ? ~0ULL : 0ULL;
                        const uint64_t maskDown = ~isUpMask & bit;
                        const uint64_t maskUp = isUpMask & bit;

                        localKeyDeltaDown[idx] = (localKeyDeltaDown[idx] & ~bit) | maskDown;
                        localKeyDeltaUp[idx] = (localKeyDeltaUp[idx] & ~bit) | maskUp;

                        hasKeyChanges = true;
                    }
                }
                raw = NEXTRAWINPUTBLOCK(raw);
            }
        }

        // --- Commit phase (single-writer, wait-free) ---
        if (localAccX) {
            const int64_t cur = m_accumMouseX.load(std::memory_order_relaxed);
            m_accumMouseX.store(cur + localAccX, std::memory_order_relaxed);
        }
        if (localAccY) {
            const int64_t cur = m_accumMouseY.load(std::memory_order_relaxed);
            m_accumMouseY.store(cur + localAccY, std::memory_order_relaxed);
        }

        if (finalBtnState != initialBtnState) {
            m_mouseButtons.store(finalBtnState, std::memory_order_relaxed);
        }

        if (hasKeyChanges) {
            for (int i = 0; i < 4; ++i) {
                if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
                    const uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                    m_vkDown[i].store(
                        (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i],
                        std::memory_order_relaxed);
                }
            }
        }

        std::atomic_thread_fence(std::memory_order_release);
    }

    void InputState::fetchMouseDelta(int& outX, int& outY) noexcept {
        const int64_t curX = m_accumMouseX.load(std::memory_order_acquire);
        const int64_t curY = m_accumMouseY.load(std::memory_order_acquire);
        outX = static_cast<int>(curX - m_lastReadMouseX);
        outY = static_cast<int>(curY - m_lastReadMouseY);
        m_lastReadMouseX = curX;
        m_lastReadMouseY = curY;
    }

    void InputState::discardDeltas() noexcept {
        m_lastReadMouseX = m_accumMouseX.load(std::memory_order_relaxed);
        m_lastReadMouseY = m_accumMouseY.load(std::memory_order_relaxed);
    }

    void InputState::resetAllKeys() noexcept {
        for (auto& vk : m_vkDown) vk.store(0, std::memory_order_relaxed);
        m_mouseButtons.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        m_hkPrev = 0;
    }

    void InputState::resetMouseButtons() noexcept {
        m_mouseButtons.store(0, std::memory_order_release);
    }

    void InputState::setHotkeyVks(int id, const std::vector<UINT>& vks) {
        if (UNLIKELY(id < 0 || static_cast<size_t>(id) >= kMaxHotkeyId)) return;

        std::memset(m_hkMasks.vkMask[id], 0, sizeof(uint64_t) * 4);
        m_hkMasks.mouseMask[id] = 0;
        m_hkMasks.hasMask[id] = false;

        const uint64_t bbit = 1ULL << id;

        if (vks.empty()) {
            m_boundHotkeys &= ~bbit;
            return;
        }

        for (const UINT vk : vks) {
            if (vk >= VK_LBUTTON && vk <= VK_XBUTTON2) {
                int bit = -1;
                switch (vk) {
                case VK_LBUTTON:  bit = 0; break;
                case VK_RBUTTON:  bit = 1; break;
                case VK_MBUTTON:  bit = 2; break;
                case VK_XBUTTON1: bit = 3; break;
                case VK_XBUTTON2: bit = 4; break;
                }
                if (bit >= 0) m_hkMasks.mouseMask[id] |= static_cast<uint8_t>(1 << bit);
            }
            else if (vk < 256) {
                m_hkMasks.vkMask[id][vk >> 6] |= (1ULL << (vk & 63));
            }
        }
        m_hkMasks.hasMask[id] = true;
        m_boundHotkeys |= bbit;
    }

    // =========================================================================
    // REFACTORED: pollHotkeys / snapshotInputFrame / resetHotkeyEdges / hotkeyDown
    //
    // Previously each function duplicated the full VK snapshot load (4 atomic
    // loads + 1 mouse load + acquire fence) and the BSF scan loop (~15 lines).
    // Now uses takeSnapshot() + scanBoundHotkeys() from the header.
    //
    // Code reduction: ~60 lines -> ~25 lines (4 functions combined).
    // Performance: identical — inlined helpers produce the same machine code.
    // =========================================================================

    void InputState::pollHotkeys(FrameHotkeyState& out) noexcept {
        const auto snap = takeSnapshot();
        const uint64_t newDown = scanBoundHotkeys(snap);
        out.down = newDown;
        out.pressed = newDown & ~m_hkPrev;
        m_hkPrev = newDown;
    }

    void InputState::snapshotInputFrame(FrameHotkeyState& outHk,
        int& outMouseX, int& outMouseY) noexcept
    {
        // Load mouse accumulators within the same snapshot window.
        // The acquire fence inside takeSnapshot() covers these loads as well,
        // since they are sequenced before it returns.
        const int64_t curX = m_accumMouseX.load(std::memory_order_relaxed);
        const int64_t curY = m_accumMouseY.load(std::memory_order_relaxed);
        const auto snap = takeSnapshot();

        outMouseX = static_cast<int>(curX - m_lastReadMouseX);
        outMouseY = static_cast<int>(curY - m_lastReadMouseY);
        m_lastReadMouseX = curX;
        m_lastReadMouseY = curY;

        const uint64_t newDown = scanBoundHotkeys(snap);
        outHk.down = newDown;
        outHk.pressed = newDown & ~m_hkPrev;
        m_hkPrev = newDown;
    }

    bool InputState::hotkeyDown(int id) const noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        if (!m_hkMasks.hasMask[id]) return false;

        const auto snap = takeSnapshot();
        return testHotkeyMask(id, snap.vk, snap.mouse);
    }

    void InputState::resetHotkeyEdges() noexcept {
        const auto snap = takeSnapshot();
        m_hkPrev = scanBoundHotkeys(snap);
    }

} // namespace MelonPrime
#endif
