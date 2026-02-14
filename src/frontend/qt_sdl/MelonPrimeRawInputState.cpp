#ifdef _WIN32
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawWinInternal.h"
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#include <intrin.h>
#endif

// Required by NEXTRAWINPUTBLOCK macro on MinGW (not defined in its headers)
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
        for (auto& vk : m_vkDown)
            vk.store(0, std::memory_order_relaxed);
        m_mouseButtons.store(0, std::memory_order_relaxed);

        m_accumMouseX.store(0, std::memory_order_relaxed);
        m_accumMouseY.store(0, std::memory_order_relaxed);
        m_lastReadMouseX = 0;
        m_lastReadMouseY = 0;

        m_hkMask.fill(HotkeyMask{});
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

            // Mouse Button LUT: maps RI_MOUSE_BUTTON_*_DOWN/UP flags to compact bit masks
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

            // VK remap table: Ctrl/Alt/Shift â†’ Left/Right variants
            s_vkRemap.fill(VkRemapEntry{ 0, 0 });
            auto setRemap = [](int base, int l, int r) {
                s_vkRemap[base] = { static_cast<uint8_t>(l), static_cast<uint8_t>(r) };
                };
            setRemap(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
            setRemap(VK_MENU, VK_LMENU, VK_RMENU);
            setRemap(VK_SHIFT, VK_LSHIFT, VK_RSHIFT);

            s_scancodeLShift = static_cast<uint16_t>(MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC));
            s_scancodeRShift = static_cast<uint16_t>(MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC));
            });
    }

    // =========================================================================
    // processRawInput â€” Joy2Key ON mode (per-message from Qt event filter)
    // =========================================================================
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
            if (UNLIKELY(vk == 0)) vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            if (LIKELY(vk > 0 && vk < 255)) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);
                setVkBit(vk, !(kb.Flags & RI_KEY_BREAK));
            }
            break;
        }
        }
    }

    // =========================================================================
    // processRawInputBatched â€” Joy2Key OFF mode (batch from hidden window)
    // =========================================================================
    // =========================================================================
    // processRawInputBatched -- Joy2Key OFF mode (batch from hidden window)
    //
    // OPT-T: hasMouseDelta / hasButtonChanges bools removed from inner loop.
    //   Old: Per mouse-event `hasMouseDelta = true` store (redundant after 1st).
    //        Per button-event `hasButtonChanges = true` store.
    //   New: Commit guards use direct value checks:
    //        Mouse: `if (localAccX | localAccY)` -- zero iff no relative movement.
    //        Buttons: `if (finalBtnState != initialBtnState)` -- compare to snapshot.
    //   Eliminates ~1 store/event from the hottest inner loop (~133 events/frame).
    // =========================================================================
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
                        // OPT-T: hasMouseDelta removed -- localAccX|Y checked at commit.
                    }
                    const USHORT flags = m.usButtonFlags & 0x03FF;
                    if (flags) {
                        const auto& lut = s_btnLut[flags];
                        finalBtnState = (finalBtnState & ~lut.upBits) | lut.downBits;
                        // OPT-T: hasButtonChanges removed -- finalBtnState compared at commit.
                    }
                }
                else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& kb = raw->data.keyboard;
                    UINT vk = kb.VKey;
                    if (UNLIKELY(vk == 0)) vk = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
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

        // --- Commit phase (single-writer, wait-free) ---
        // OPT-T: Direct value checks replace bool guards.
        if (localAccX) {
            const int64_t cur = m_accumMouseX.load(std::memory_order_relaxed);
            m_accumMouseX.store(cur + localAccX, std::memory_order_release);
        }
        if (localAccY) {
            const int64_t cur = m_accumMouseY.load(std::memory_order_relaxed);
            m_accumMouseY.store(cur + localAccY, std::memory_order_release);
        }

        if (finalBtnState != initialBtnState) {
            m_mouseButtons.store(finalBtnState, std::memory_order_release);
        }

        if (hasKeyChanges) {
            for (int i = 0; i < 4; ++i) {
                if (localKeyDeltaDown[i] | localKeyDeltaUp[i]) {
                    const uint64_t cur = m_vkDown[i].load(std::memory_order_relaxed);
                    m_vkDown[i].store(
                        (cur | localKeyDeltaDown[i]) & ~localKeyDeltaUp[i],
                        std::memory_order_release);
                }
            }
        }
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
        for (auto& vk : m_vkDown) vk.store(0, std::memory_order_release);
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

    // =========================================================================
    // pollHotkeys
    //
    // OPT 1: Fence coalescing â€” 5 acquire loads â†’ 5 relaxed loads + 1 fence.
    //
    // OPT 2: Bulk edge detection â€” per-bit branching eliminated.
    //   Old: 2 branches per hotkey (isDown!=prev â†’ isDown â†’ set/clear m_hkPrev)
    //        â†’ ~58 conditional branches for 29 hotkeys
    //   New: After bit-scan loop, compute pressed = newDown & ~prev (bitwise AND).
    //        Update prev = newDown (single store). Zero branches for edge logic.
    //
    //   The inner loop body is now: testHotkeyMask + conditional OR.
    //   m_hkPrev is not touched until after the loop completes.
    // =========================================================================
    void InputState::pollHotkeys(FrameHotkeyState& out) noexcept {
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_relaxed);
        const uint8_t snapMouse = m_mouseButtons.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        out = {};
        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            uint64_t newDown = 0;

            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);

                if (testHotkeyMask(m_hkMask[id], snapVk, snapMouse))
                    newDown |= 1ULL << bitPos;

                bound &= bound - 1;
            }

            // OPT 2: Bulk edge detection â€” pure bitwise, no per-hotkey branches.
            //   pressed = rising edges = down now AND was not down before.
            out.down[w]    = newDown;
            out.pressed[w] = newDown & ~m_hkPrev[w];
            m_hkPrev[w]    = newDown;
        }
    }

    // =========================================================================
    // snapshotInputFrame
    //
    // OPT-S: Fused pollHotkeys + fetchMouseDelta in one call.
    //
    //   Old path (two separate calls):
    //     pollHotkeys:    4 relaxed VK loads + 1 relaxed mouse load + 1 fence
    //     fetchMouseDelta: 2 acquire loads (each is relaxed+fence on ARM)
    //     = 7 atomic loads + 2-3 fences
    //
    //   New path (single call):
    //     7 relaxed loads + 1 fence
    //     = 7 atomic loads + 1 fence
    //
    //   Saves: 1-2 fences (significant on ARM), 1 function call overhead,
    //          1 m_rawFilter→m_state indirection in the caller.
    // =========================================================================
    void InputState::snapshotInputFrame(FrameHotkeyState& outHk, int& outMouseX, int& outMouseY) noexcept {
        // --- Single snapshot of all atomics ---
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_relaxed);
        const uint8_t snapMouse = m_mouseButtons.load(std::memory_order_relaxed);
        const int64_t curX = m_accumMouseX.load(std::memory_order_relaxed);
        const int64_t curY = m_accumMouseY.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        // --- Mouse delta (was fetchMouseDelta) ---
        outMouseX = static_cast<int>(curX - m_lastReadMouseX);
        outMouseY = static_cast<int>(curY - m_lastReadMouseY);
        m_lastReadMouseX = curX;
        m_lastReadMouseY = curY;

        // --- Hotkey scan (was pollHotkeys) ---
        outHk = {};
        for (int w = 0; w < 2; ++w) {
            uint64_t bound = m_boundHotkeys[w];
            uint64_t newDown = 0;

            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                const int id = (w << 6) | static_cast<int>(bitPos);
                if (testHotkeyMask(m_hkMask[id], snapVk, snapMouse))
                    newDown |= 1ULL << bitPos;
                bound &= bound - 1;
            }

            outHk.down[w]    = newDown;
            outHk.pressed[w] = newDown & ~m_hkPrev[w];
            m_hkPrev[w]      = newDown;
        }
    }

    // =========================================================================
    // hotkeyDown
    //
    // OPT: Fence coalescing + testHotkeyMask reuse.
    //   Old: Up to 5 individual acquire loads with per-word early exit.
    //   New: 5 relaxed loads + 1 fence + single testHotkeyMask call.
    // =========================================================================
    bool InputState::hotkeyDown(int id) const noexcept {
        if (UNLIKELY(static_cast<unsigned>(id) >= kMaxHotkeyId)) return false;
        const HotkeyMask& mask = m_hkMask[id];
        if (!mask.hasMask) return false;

        const uint8_t buttons = m_mouseButtons.load(std::memory_order_relaxed);
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        return testHotkeyMask(mask, snapVk, buttons);
    }

    // =========================================================================
    // resetHotkeyEdges
    //
    // OPT: Snapshot atomics once, reuse for all hotkey tests.
    //   Old: Called hotkeyDown(id) per bound hotkey. Each hotkeyDown did
    //        up to 5 acquire loads â†’ ~145 acquire loads for 29 hotkeys.
    //   New: 5 relaxed loads + 1 fence (total), then testHotkeyMask (pure ALU)
    //        per hotkey. ~96% reduction in atomic load operations.
    // =========================================================================
    void InputState::resetHotkeyEdges() noexcept {
        uint64_t snapVk[4];
        for (int i = 0; i < 4; ++i)
            snapVk[i] = m_vkDown[i].load(std::memory_order_relaxed);
        const uint8_t snapMouse = m_mouseButtons.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        uint64_t newPrev[2] = {};
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
                if (testHotkeyMask(m_hkMask[id], snapVk, snapMouse))
                    newPrev[w] |= (1ULL << bitPos);
                bound &= bound - 1;
            }
        }
        m_hkPrev[0] = newPrev[0];
        m_hkPrev[1] = newPrev[1];
    }

} // namespace MelonPrime
#endif
