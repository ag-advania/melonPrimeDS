#ifndef MELON_PRIME_INPUT_STATE_H
#define MELON_PRIME_INPUT_STATE_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <mutex>
#include "MelonPrimeRawWinInternal.h"

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  elif defined(__GNUC__) || defined(__clang__)
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  else
#    define FORCE_INLINE inline
#  endif
#endif

#ifndef UNLIKELY
#  if defined(__GNUC__) || defined(__clang__)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#    define LIKELY(x)   __builtin_expect(!!(x), 1)
#  else
#    define UNLIKELY(x) (x)
#    define LIKELY(x)   (x)
#  endif
#endif

namespace MelonPrime {

    struct FrameHotkeyState {
        uint64_t down[2]{};
        uint64_t pressed[2]{};

        [[nodiscard]] FORCE_INLINE bool isDown(int id) const noexcept {
            return (down[id >> 6] >> (id & 63)) & 1;
        }
        [[nodiscard]] FORCE_INLINE bool isPressed(int id) const noexcept {
            return (pressed[id >> 6] >> (id & 63)) & 1;
        }
    };

    class InputState {
    public:
        InputState() noexcept;
        ~InputState() = default;

        InputState(const InputState&) = delete;
        InputState& operator=(const InputState&) = delete;

        static void InitializeTables() noexcept;

        // Joy2Key ON: process single WM_INPUT message
        void processRawInput(HRAWINPUT hRaw) noexcept;
        // Joy2Key OFF: batch process via GetRawInputBuffer
        void processRawInputBatched() noexcept;

        void fetchMouseDelta(int& outX, int& outY) noexcept;
        void discardDeltas() noexcept;

        void resetAllKeys() noexcept;
        void resetMouseButtons() noexcept;

        static constexpr size_t kMaxHotkeyId = 128;
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        void pollHotkeys(FrameHotkeyState& out) noexcept;
        [[nodiscard]] bool hotkeyDown(int id) const noexcept;
        void resetHotkeyEdges() noexcept;

    private:
        // =================================================================
        // Cache Line 0: Write-Heavy (Input Thread)
        //   Atomics written by the input producer at high frequency.
        //   Mouse coordinates use monotonic 64-bit counters to avoid CAS.
        // =================================================================
        alignas(64) std::atomic<uint64_t> m_vkDown[4];

        std::atomic<int64_t>  m_accumMouseX{ 0 };
        std::atomic<int64_t>  m_accumMouseY{ 0 };
        std::atomic<uint8_t>  m_mouseButtons{ 0 };

        // =================================================================
        // Cache Line 1+: Read-Mostly / Consumer State
        //   Hotkey masks and state managed by the main thread.
        //   Separated from write-heavy line to avoid false sharing.
        // =================================================================
        struct alignas(64) HotkeyMask {
            uint64_t vkMask[4];
            uint8_t  mouseMask;
            bool     hasMask;
            uint8_t  _pad[6];
        };
        std::array<HotkeyMask, kMaxHotkeyId> m_hkMask;

        uint64_t m_hkPrev[2];
        uint64_t m_boundHotkeys[2]{ 0, 0 };

        // Consumer thread's last-read position cache
        int64_t m_lastReadMouseX{ 0 };
        int64_t m_lastReadMouseY{ 0 };

        // =================================================================
        // Static Tables (shared across all instances)
        // =================================================================
        struct BtnLutEntry { uint8_t downBits; uint8_t upBits; };
        static std::array<BtnLutEntry, 1024> s_btnLut;

        struct VkRemapEntry { uint8_t normal; uint8_t extended; };
        static std::array<VkRemapEntry, 256> s_vkRemap;

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
            const uint64_t bit  = 1ULL << (vk & 63);
            if (down) m_vkDown[widx].fetch_or(bit, std::memory_order_release);
            else      m_vkDown[widx].fetch_and(~bit, std::memory_order_release);
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
            const HotkeyMask& mask, const uint64_t snapVk[4], uint8_t snapMouse) const noexcept
        {
            // OR mouse and all 4 VK words together — single branch
            const uint64_t keyHit =
                (mask.vkMask[0] & snapVk[0]) | (mask.vkMask[1] & snapVk[1]) |
                (mask.vkMask[2] & snapVk[2]) | (mask.vkMask[3] & snapVk[3]);
            return ((mask.mouseMask & snapMouse) | keyHit) != 0;
        }
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_INPUT_STATE_H
