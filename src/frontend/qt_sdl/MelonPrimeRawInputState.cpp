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
    uint16_t InputState::s_scancodeLShift = 0;
    uint16_t InputState::s_scancodeRShift = 0;
    std::once_flag InputState::s_initFlag;
    NtUserGetRawInputBuffer_t InputState::s_fnBestGetRawInputBuffer = ::GetRawInputBuffer;

    InputState::InputState() noexcept {
        for (auto& vk : m_vkDown) {
            vk.store(0, std::memory_order_relaxed);
        }
        m_mouseButtons.store(0, std::memory_order_relaxed);

        // カウンタ初期化
        m_accumMouseX.store(0, std::memory_order_relaxed);
        m_accumMouseY.store(0, std::memory_order_relaxed);
        m_lastReadMouseX = 0;
        m_lastReadMouseY = 0;

        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
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
            std::fill(s_vkRemap.begin(), s_vkRemap.end(), VkRemapEntry{ 0, 0 });
            s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
            s_vkRemap[VK_MENU] = { VK_LMENU,    VK_RMENU };
            s_vkRemap[VK_SHIFT] = { VK_LSHIFT,   VK_RSHIFT };
            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));
            });
    }

    // Joy2Key ON時 (Process Raw Input directly)
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
                // Wait-Free Update (fetch_add)
                if (m.lLastX) m_accumMouseX.fetch_add(m.lLastX, std::memory_order_release);
                if (m.lLastY) m_accumMouseY.fetch_add(m.lLastY, std::memory_order_release);
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
            if (UNLIKELY(vk == 0)) {
                vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            }
            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                const bool isDown = !(kb.Flags & RI_KEY_BREAK);
                setVkBit(vk, isDown);
            }
            break;
        }
        }
    }

    // Joy2Key OFF時 (Threaded Batch Processing)
    void InputState::processRawInputBatched() noexcept {
        // alignas(64) でキャッシュライン汚染を防ぐ
        alignas(64) static thread_local uint8_t buffer[16384];

        int32_t localAccX = 0;
        int32_t localAccY = 0;

        uint64_t localKeyDeltaDown[4] = { 0 };
        uint64_t localKeyDeltaUp[4] = { 0 };
        bool hasMouseDelta = false;
        bool hasKeyChanges = false;
        bool hasButtonChanges = false;
        uint8_t finalBtnState = m_mouseButtons.load(std::memory_order_relaxed);

        for (;;) {
            UINT size = sizeof(buffer);
            UINT count = s_fnBestGetRawInputBuffer(reinterpret_cast<PRAWINPUT>(buffer), &size, sizeof(RAWINPUTHEADER));
            if (count == 0 || count == UINT(-1)) break;

            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);

            for (UINT i = 0; i < count; ++i) {
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& m = raw->data.mouse;

                    // 分岐予測ヒント: Absoluteは稀
                    if (UNLIKELY(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        // ignore absolute mouse in this optimized path
                    }
                    else {
                        // 符号付きオーバーフローは未定義だが、unsignedキャストでラップアラウンド加算として安全に処理
                        localAccX = static_cast<int32_t>(static_cast<uint32_t>(localAccX) + static_cast<uint32_t>(m.lLastX));
                        localAccY = static_cast<int32_t>(static_cast<uint32_t>(localAccY) + static_cast<uint32_t>(m.lLastY));
                        hasMouseDelta = true;
                    }

                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        finalBtnState = (finalBtnState & ~lut.upBits) | lut.downBits;
                        hasButtonChanges = true;
                    }
                }
                else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;
                    if (UNLIKELY(vk == 0)) {
                        vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                    }
                    if (LIKELY(vk > 0 && vk < 255)) {
                        vk = remapVk(vk, kb.MakeCode, kb.Flags);
                        const int idx = vk >> 6;
                        const uint64_t bit = 1ULL << (vk & 63);
                        if (!(kb.Flags & RI_KEY_BREAK)) {
                            localKeyDeltaDown[idx] |= bit;
                            localKeyDeltaUp[idx] &= ~bit;
                        }
                        else {
                            localKeyDeltaUp[idx] |= bit;
                            localKeyDeltaDown[idx] &= ~bit;
                        }
                        hasKeyChanges = true;
                    }
                }
                raw = NEXTRAWINPUTBLOCK(raw);
            }
        }

        // --- Batch Commit (Wait-Free for Mouse) ---

        // 1. Mouse Delta Update
        if (hasMouseDelta) {
            // LOCK XADD (Wait-Free)
            if (localAccX) m_accumMouseX.fetch_add(localAccX, std::memory_order_release);
            if (localAccY) m_accumMouseY.fetch_add(localAccY, std::memory_order_release);
        }

        // 2. Mouse Button Update
        if (hasButtonChanges) {
            uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
            while (!m_mouseButtons.compare_exchange_weak(
                cur, finalBtnState, std::memory_order_release, std::memory_order_relaxed));
        }

        // 3. Keyboard Update
        if (hasKeyChanges) {
            for (int i = 0; i < 4; ++i) {
                if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
                    uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                    uint64_t nxt;
                    do {
                        nxt = (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i];
                    } while (cur != nxt && UNLIKELY(!m_vkDown[i].compare_exchange_weak(
                        cur, nxt, std::memory_order_release, std::memory_order_relaxed)));
                }
            }
        }
    }

    void InputState::fetchMouseDelta(int& outX, int& outY) noexcept {
        // Snapshot方式: 現在の累積値を取得
        const int64_t curX = m_accumMouseX.load(std::memory_order_acquire);
        const int64_t curY = m_accumMouseY.load(std::memory_order_acquire);

        // 前回との差分を計算 (2の補数表現によりオーバーフローしても差分は正しくなる)
        outX = static_cast<int>(curX - m_lastReadMouseX);
        outY = static_cast<int>(curY - m_lastReadMouseY);

        // キャッシュ更新
        m_lastReadMouseX = curX;
        m_lastReadMouseY = curY;
    }

    void InputState::discardDeltas() noexcept {
        // 現在値を読み込んで「読んだこと」にする
        m_lastReadMouseX = m_accumMouseX.load(std::memory_order_relaxed);
        m_lastReadMouseY = m_accumMouseY.load(std::memory_order_relaxed);
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

    void InputState::setHotkeyVks(int id, const std::vector<UINT>& vks) {
        if (UNLIKELY(id < 0 || static_cast<size_t>(id) >= kMaxHotkeyId)) return;
        HotkeyMask& mask = m_hkMask[id];
        std::memset(&mask, 0, sizeof(HotkeyMask));
        const int bword = id >> 6;
        const uint64_t bbit = 1ULL << (id & 63);

        if (vks.empty()) {
            m_boundHotkeys[bword] &= ~bbit;
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
                if (bit >= 0) mask.mouseMask |= static_cast<uint8_t>(1 << bit);
            }
            else if (vk < 256) {
                mask.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            }
        }
        mask.hasMask = true;
        m_boundHotkeys[bword] |= bbit;
    }

    void InputState::pollHotkeys(FrameHotkeyState& out) noexcept {
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_acquire);
        const uint8_t snapMouse = m_mouseButtons.load(std::memory_order_acquire);

        out = {};
        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            uint64_t newDown = 0;
            uint64_t newPressed = 0;
            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);
                const uint64_t hbit = 1ULL << bitPos;
                const HotkeyMask& mask = m_hkMask[id];
                const bool isDown = testHotkeyMask(mask, snapVk, snapMouse);
                if (isDown) newDown |= hbit;
                const bool prev = (m_hkPrev[w] & hbit) != 0;
                if (isDown != prev) {
                    if (isDown) {
                        newPressed |= hbit;
                        m_hkPrev[w] |= hbit;
                    }
                    else {
                        m_hkPrev[w] &= ~hbit;
                    }
                }
                bound &= bound - 1;
            }
            out.down[w] = newDown;
            out.pressed[w] = newPressed;
        }
    }

    bool InputState::hotkeyDown(int id) const noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const HotkeyMask& mask = m_hkMask[id];
        if (!mask.hasMask) return false;
        const uint8_t buttons = m_mouseButtons.load(std::memory_order_acquire);
        if (mask.mouseMask && (buttons & mask.mouseMask)) return true;
        for (int i = 0; i < 4; ++i) {
            if (mask.vkMask[i]) {
                const uint64_t keys = m_vkDown[i].load(std::memory_order_acquire);
                if (keys & mask.vkMask[i]) return true;
            }
        }
        return false;
    }

    void InputState::resetHotkeyEdges() noexcept {
        uint64_t newPrev[2] = { 0, 0 };
        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);
                if (hotkeyDown(id)) {
                    newPrev[w] |= (1ULL << bitPos);
                }
                bound &= bound - 1;
            }
        }
        m_hkPrev[0] = newPrev[0];
        m_hkPrev[1] = newPrev[1];
    }

} // namespace MelonPrime
#endif