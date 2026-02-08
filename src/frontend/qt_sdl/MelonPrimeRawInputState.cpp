#ifdef _WIN32
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <cstring>
#include <algorithm>

// Intrinsic for BitScan
#ifdef _MSC_VER
#include <intrin.h>
#endif

// MinGW対策
#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

namespace MelonPrime {

    // (静的メンバの定義は変更なし。省略せずに元のコードを使用してください)
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
        m_mouseDeltaCombined.store(0, std::memory_order_relaxed);

        std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
        std::fill(std::begin(m_hkPrev), std::end(m_hkPrev), 0);
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
    }

    void InputState::InitializeTables() noexcept {
        std::call_once(s_initFlag, []() {
            // 1. NT APIの解決を試みる
            WinInternal::ResolveNtApis();

            // 2. 最適な関数を選択
            // NtUserGetRawInputBuffer が使えるならカーネルバッファ直読みAPIを採用
            if (WinInternal::fnNtUserGetRawInputBuffer) {
                s_fnBestGetRawInputBuffer = WinInternal::fnNtUserGetRawInputBuffer;
            }
            // そうでなければ初期値(::GetRawInputBuffer)のまま

            // 3. テーブル初期化
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

    // Joy2Key ON時
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
                        // Use casting to avoid signed overflow UB, though mostly safe on x86
                        nxt.s.x = static_cast<int32_t>(static_cast<uint32_t>(cur.s.x) + static_cast<uint32_t>(dx));
                        nxt.s.y = static_cast<int32_t>(static_cast<uint32_t>(cur.s.y) + static_cast<uint32_t>(dy));
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

            // ★ FIX: VKeyが0の場合はスキャンコードから復元
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

    // Joy2Key OFF時 (Threaded)
    void InputState::processRawInputBatched() noexcept {
        alignas(16) static thread_local uint8_t buffer[16384];

        // ローカルアキュムレータ
        int32_t localAccX = 0;
        int32_t localAccY = 0;

        // キーボードの状態変化マスク
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
                    // Move logic
                    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                        // Integer overflow is undefined for signed types, use unsigned arithmetic
                        localAccX = static_cast<int32_t>(static_cast<uint32_t>(localAccX) + static_cast<uint32_t>(m.lLastX));
                        localAccY = static_cast<int32_t>(static_cast<uint32_t>(localAccY) + static_cast<uint32_t>(m.lLastY));
                        hasMouseDelta = true;
                    }
                    // Button logic
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

                    // ★ FIX: VKeyが0の場合はスキャンコードから復元
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

        // --- Batch Commit ---

        // 1. Mouse Delta Update
        if (hasMouseDelta) {
            // CASループの代わりに fetch_add 的なロジックが必要だが、
            // X/Yパック構造なので CASループは避けられない。
            // ただし、ループ回数は「バッファ処理回数」ではなく「1回」になるため高速。
            MouseDeltaPack cur, nxt;
            cur.combined = m_mouseDeltaCombined.load(std::memory_order_relaxed);
            do {
                nxt.s.x = static_cast<int32_t>(static_cast<uint32_t>(cur.s.x) + static_cast<uint32_t>(localAccX));
                nxt.s.y = static_cast<int32_t>(static_cast<uint32_t>(cur.s.y) + static_cast<uint32_t>(localAccY));
            } while (UNLIKELY(!m_mouseDeltaCombined.compare_exchange_weak(
                cur.combined, nxt.combined, std::memory_order_release, std::memory_order_relaxed)));
        }

        // 2. Mouse Button Update
        if (hasButtonChanges) {
            uint8_t cur = m_mouseButtons.load(std::memory_order_relaxed);
            // Even though we are likely the only writer in this mode, use CAS for safety against 'resetAllKeys'
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
        m_boundHotkeys[0] = 0;
        m_boundHotkeys[1] = 0;
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

                // Optimization: Inlined branchless test
                const bool isDown = testHotkeyMask(mask, snapVk, snapMouse);

                if (isDown) newDown |= hbit;

                // Edge detection
                const bool prev = (m_hkPrev[w] & hbit) != 0;
                // If isDown is true and prev is false -> Pressed
                // Using XOR and AND: (isDown ^ prev) & isDown
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

        // Using load acquire for reading from atomic store in batched thread
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