#ifndef MELON_PRIME_INPUT_STATE_H
#define MELON_PRIME_INPUT_STATE_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <mutex>
#include "MelonPrimeCompilerHints.h"   // Shared macros (was duplicated inline)
#include "MelonPrimeRawWinInternal.h"

namespace MelonPrime {

    struct FrameHotkeyState {
        uint64_t down{};
        uint64_t pressed{};

        [[nodiscard]] FORCE_INLINE bool isDown(int id) const noexcept {
            return (down >> id) & 1;
        }
        [[nodiscard]] FORCE_INLINE bool isPressed(int id) const noexcept {
            return (pressed >> id) & 1;
        }
    };

    class InputState {
    public:
        InputState() noexcept;
        ~InputState() = default;

        InputState(const InputState&) = delete;
        InputState& operator=(const InputState&) = delete;

        static void InitializeTables() noexcept;

        void processRawInput(HRAWINPUT hRaw) noexcept;
        void processRawInputBatched() noexcept;

        void fetchMouseDelta(int& outX, int& outY) noexcept;
        void discardDeltas() noexcept;

        void resetAllKeys() noexcept;
        void resetMouseButtons() noexcept;

        static constexpr size_t kMaxHotkeyId = 64;
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        void pollHotkeys(FrameHotkeyState& out) noexcept;
        void snapshotInputFrame(FrameHotkeyState& outHk,
            int& outMouseX, int& outMouseY) noexcept;
        [[nodiscard]] bool hotkeyDown(int id) const noexcept;
        void resetHotkeyEdges() noexcept;

    private:
        // =================================================================
        // VK Snapshot — captured once, reused by hotkey scan + mouse delta.
        // Eliminates 4x duplication of the load-4-atomics-then-fence pattern.
        // =================================================================
        struct VkSnapshot {
            uint64_t vk[4];
            uint8_t  mouse;
        };

        /// Take a consistent snapshot of all VK + mouse button state (acquire fence).
        [[nodiscard]] FORCE_INLINE VkSnapshot takeSnapshot() const noexcept {
            VkSnapshot snap;
            for (int i = 0; i < 4; ++i)
                snap.vk[i] = m_vkDown[i].load(std::memory_order_relaxed);
            snap.mouse = m_mouseButtons.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            return snap;
        }

        /// Scan all bound hotkeys against a VkSnapshot and return the combined bitmask.
        /// Extracted from pollHotkeys / snapshotInputFrame / resetHotkeyEdges / hotkeyDown.
        [[nodiscard]] FORCE_INLINE uint64_t scanBoundHotkeys(const VkSnapshot& snap) const noexcept {
            uint64_t bound = m_boundHotkeys;
            uint64_t result = 0;
            while (bound) {
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bitPos;
                _BitScanForward64(&bitPos, bound);
#else
                const int bitPos = __builtin_ctzll(bound);
#endif
                if (testHotkeyMask(bitPos, snap.vk, snap.mouse))
                    result |= 1ULL << bitPos;
                bound &= bound - 1;
            }
            return result;
        }

        // =================================================================
        // Cache Line 0: Write-Heavy (Input Thread)
        // =================================================================
        alignas(64) std::atomic<uint64_t> m_vkDown[4];

        std::atomic<int64_t>  m_accumMouseX{ 0 };
        std::atomic<int64_t>  m_accumMouseY{ 0 };
        std::atomic<uint8_t>  m_mouseButtons{ 0 };

        // =================================================================
        // Cache Line 1+: Read-Mostly / Consumer State
        // =================================================================
        struct HotkeyMasks {
            alignas(32) uint64_t vkMask[kMaxHotkeyId][4];
            uint8_t  mouseMask[kMaxHotkeyId];
            bool     hasMask[kMaxHotkeyId];
        } m_hkMasks;

        uint64_t m_hkPrev{};
        uint64_t m_boundHotkeys{};

        int64_t m_lastReadMouseX{ 0 };
        int64_t m_lastReadMouseY{ 0 };

        // =================================================================
        // Static Tables
        // =================================================================
        struct BtnLutEntry { uint8_t downBits; uint8_t upBits; };
        static std::array<BtnLutEntry, 1024> s_btnLut;

        struct VkRemapEntry { uint8_t normal; uint8_t extended; };
        static std::array<VkRemapEntry, 256> s_vkRemap;

        static std::array<uint16_t, 512> s_makeCodeLut;

        static uint16_t s_scancodeLShift;
        static uint16_t s_scancodeRShift;
        static std::once_flag s_initFlag;
        static NtUserGetRawInputBuffer_t s_fnBestGetRawInputBuffer;

        // =================================================================
        // Inline Helpers
        // =================================================================
        FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
            if (UNLIKELY(vk >= 256)) return;
            const uint32_t widx = vk >> 6;
            const uint64_t bit = 1ULL << (vk & 63);
            const uint64_t cur = m_vkDown[widx].load(std::memory_order_relaxed);
            m_vkDown[widx].store(
                down ? (cur | bit) : (cur & ~bit),
                std::memory_order_relaxed);
        }

        FORCE_INLINE uint32_t remapVk(uint32_t vk, USHORT makeCode, USHORT flags) const noexcept {
            if (UNLIKELY(vk == VK_SHIFT)) {
                return (makeCode == s_scancodeLShift) ? VK_LSHIFT : VK_RSHIFT;
            }
            const auto& entry = s_vkRemap[vk];
            if (entry.normal != 0) {
                return (flags & RI_KEY_E0) ? entry.extended : entry.normal;
            }
            return vk;
        }

        FORCE_INLINE bool testHotkeyMask(
            int id, const uint64_t snapVk[4], uint8_t snapMouse) const noexcept
        {
            const uint64_t keyHit =
                (m_hkMasks.vkMask[id][0] & snapVk[0]) | (m_hkMasks.vkMask[id][1] & snapVk[1]) |
                (m_hkMasks.vkMask[id][2] & snapVk[2]) | (m_hkMasks.vkMask[id][3] & snapVk[3]);
            return ((m_hkMasks.mouseMask[id] & snapMouse) | keyHit) != 0;
        }
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_INPUT_STATE_H
