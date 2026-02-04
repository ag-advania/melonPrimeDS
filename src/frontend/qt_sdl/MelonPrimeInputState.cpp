#ifdef _WIN32
#include "MelonPrimeInputState.h"
#include "MelonPrimeWinInternal.h"
#include <cstring>
#include <algorithm>

// MinGWëŒçÙ
#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

namespace MelonPrime {

    std::array<InputState::BtnLutEntry, 1024> InputState::s_btnLut;
    std::array<InputState::VkRemapEntry, 256> InputState::s_vkRemap;
    uint16_t InputState::s_scancodeLShift = 0;
    uint16_t InputState::s_scancodeRShift = 0;
    std::once_flag InputState::s_initFlag;

    InputState::InputState() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_relaxed);
        }
        m_mouseButtons.store(0, std::memory_order_relaxed);
        m_mouseDeltaCombined.store(0, std::memory_order_relaxed);

        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
    }

    void InputState::InitializeTables() noexcept {
        std::call_once(s_initFlag, []() {
            WinInternal::ResolveNtApis();

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

            std::fill(s_vkRemap.begin(), s_vkRemap.end(), VkRemapEntry{ 0, 0 });
            s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
            s_vkRemap[VK_MENU] = { VK_LMENU,    VK_RMENU };
            s_vkRemap[VK_SHIFT] = { VK_LSHIFT,   VK_RSHIFT };

            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));
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

        const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(rawBuf);

        switch (raw->header.dwType) {
        case RIM_TYPEMOUSE: {
            const RAWMOUSE& m = raw->data.mouse;
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                const LONG dx = m.lLastX;
                const LONG dy = m.lLastY;
                if (dx | dy) {
                    MouseDeltaPack cur, nxt;
                    cur.combined = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                    do {
                        nxt.s.x = cur.s.x + dx;
                        nxt.s.y = cur.s.y + dy;
                    } while (UNLIKELY(!m_mouseDeltaCombined.compare_exchange_weak(
                        cur.combined, nxt.combined, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
            const USHORT flags = m.usButtonFlags & 0x03FF;
            if (flags) {
                const auto& lut = s_btnLut[flags];
                if (lut.downBits | lut.upBits) {
                    uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
                    uint8_t nxt;
                    do {
                        nxt = (cur | lut.downBits) & ~lut.upBits;
                    } while (cur != nxt && UNLIKELY(!m_mouseButtons.compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
            break;
        }
        case RIM_TYPEKEYBOARD: {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                const bool isDown = !(kb.Flags & RI_KEY_BREAK);
                setVkBit(vk, isDown);
            }
            break;
        }
        }
    }

    void InputState::processRawInputBatched() noexcept {
        alignas(16) static thread_local uint8_t buffer[16384];

        for (;;) {
            UINT size = sizeof(buffer);
            UINT count;

            if (LIKELY(WinInternal::fnNtUserGetRawInputBuffer != nullptr)) {
                count = WinInternal::fnNtUserGetRawInputBuffer(reinterpret_cast<PRAWINPUT>(buffer), &size, sizeof(RAWINPUTHEADER));
            }
            else {
                count = GetRawInputBuffer(reinterpret_cast<PRAWINPUT>(buffer), &size, sizeof(RAWINPUTHEADER));
            }

            if (count == 0 || count == UINT(-1)) break;

            int32_t accX = 0, accY = 0;
            uint8_t btnDown = 0, btnUp = 0;
            struct KeyDelta { uint64_t down = 0; uint64_t up = 0; } keyDelta[4];
            bool hasKeyChanges = false;

            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);

            for (UINT i = 0; i < count; ++i) {
                switch (raw->header.dwType) {
                case RIM_TYPEMOUSE: {
                    const RAWMOUSE& m = raw->data.mouse;
                    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        accX += m.lLastX;
                        accY += m.lLastY;
                    }
                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        btnDown = (btnDown & ~lut.upBits) | lut.downBits;
                        btnUp = (btnUp & ~lut.downBits) | lut.upBits;
                    }
                    break;
                }
                case RIM_TYPEKEYBOARD: {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;
                    if (LIKELY(vk > 0 && vk < 255)) {
                        vk = remapVk(vk, kb.MakeCode, kb.Flags);
                        const int idx = vk >> 6;
                        const uint64_t bit = 1ULL << (vk & 63);
                        if (!(kb.Flags & RI_KEY_BREAK)) {
                            keyDelta[idx].down |= bit;
                            keyDelta[idx].up &= ~bit;
                        }
                        else {
                            keyDelta[idx].up |= bit;
                            keyDelta[idx].down &= ~bit;
                        }
                        hasKeyChanges = true;
                    }
                    break;
                }
                }
                raw = NEXTRAWINPUTBLOCK(raw);
            }

            if (accX | accY) {
                MouseDeltaPack cur, nxt;
                cur.combined = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                do {
                    nxt.s.x = cur.s.x + accX;
                    nxt.s.y = cur.s.y + accY;
                } while (UNLIKELY(!m_mouseDeltaCombined.compare_exchange_weak(cur.combined, nxt.combined, std::memory_order_release, std::memory_order_relaxed)));
            }

            if (btnDown | btnUp) {
                uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt;
                do {
                    nxt = (cur | btnDown) & ~btnUp;
                } while (cur != nxt && UNLIKELY(!m_mouseButtons.compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
            }

            if (hasKeyChanges) {
                for (int i = 0; i < 4; ++i) {
                    if (keyDelta[i].down | keyDelta[i].up) {
                        uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                        uint64_t nxt;
                        do {
                            nxt = (cur | keyDelta[i].down) & ~keyDelta[i].up;
                        } while (cur != nxt && UNLIKELY(!m_vkDown[i].compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                    }
                }
            }
        }
    }

    void InputState::fetchMouseDelta(int& outX, int& outY) noexcept {
        const uint64_t val = m_mouseDeltaCombined.exchange(0, std::memory_order_acquire);
        MouseDeltaPack p;
        p.combined = val;
        outX = p.s.x;
        outY = p.s.y;
    }

    void InputState::discardDeltas() noexcept {
        m_mouseDeltaCombined.store(0, std::memory_order_relaxed);
    }

    void InputState::resetAllKeys() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_release);
        }
        m_mouseButtons.store(0, std::memory_order_release);
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
    }

    void InputState::resetMouseButtons() noexcept {
        m_mouseButtons.store(0, std::memory_order_release);
    }

    void InputState::clearAllBindings() noexcept {
        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
    }

    void InputState::setHotkeyVks(int id, const std::vector<UINT>& vks) {
        if (UNLIKELY(id < 0 || static_cast<size_t>(id) >= kMaxHotkeyId)) return;
        HotkeyMask& mask = m_hkMask[id];
        std::memset(&mask, 0, sizeof(HotkeyMask));
        if (vks.empty()) return;

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
                if (bit >= 0) mask.mouseMask |= static_cast<uint8_t>(1 << bit);
            }
            else if (vk < 256) {
                mask.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
        mask.hasMask = true;
    }

    bool InputState::hotkeyDown(int id) const noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const HotkeyMask& mask = m_hkMask[id];
        if (!mask.hasMask) return false;
        if (mask.mouseMask) {
            const uint8_t buttons = m_mouseButtons.load(std::memory_order_acquire);
            if (buttons & mask.mouseMask) return true;
        }
        for (int i = 0; i < 4; ++i) {
            if (mask.vkMask[i]) {
                const uint64_t keys = m_vkDown[i].load(std::memory_order_acquire);
                if (keys & mask.vkMask[i]) return true;
            }
        }
        return false;
    }

    bool InputState::hotkeyPressed(int id) noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const bool current = hotkeyDown(id);
        const int word = id >> 6;
        const uint64_t bit = 1ULL << (id & 63);
        const bool previous = (m_hkPrev[word] & bit) != 0;
        if (current != previous) {
            if (current) m_hkPrev[word] |= bit;
            else         m_hkPrev[word] &= ~bit;
        }
        return current && !previous;
    }

    bool InputState::hotkeyReleased(int id) noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const bool current = hotkeyDown(id);
        const int word = id >> 6;
        const uint64_t bit = 1ULL << (id & 63);
        const bool previous = (m_hkPrev[word] & bit) != 0;
        if (current != previous) {
            if (current) m_hkPrev[word] |= bit;
            else         m_hkPrev[word] &= ~bit;
        }
        return !current && previous;
    }

    void InputState::resetHotkeyEdges() noexcept {
        uint64_t newPrev[2] = { 0, 0 };
        for (size_t i = 0; i < kMaxHotkeyId; ++i) {
            if (hotkeyDown(static_cast<int>(i))) {
                newPrev[i >> 6] |= (1ULL << (i & 63));
            }
        }
        m_hkPrev[0] = newPrev[0];
        m_hkPrev[1] = newPrev[1];
    }

} // namespace MelonPrime
#endif // _WIN32