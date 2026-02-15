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
        // Cache Line 0: Write-Heavy (Input Thread)
        // =================================================================
        alignas(64) std::atomic<uint64_t> m_vkDown[4];

        std::atomic<int64_t>  m_accumMouseX{ 0 };
        std::atomic<int64_t>  m_accumMouseY{ 0 };
        std::atomic<uint8_t>  m_mouseButtons{ 0 };

        // =================================================================
        // Cache Line 1+: Read-Mostly / Consumer State
        // =================================================================

        // OPT-S: SoA (Structure of Arrays) layout for Hotkeys.
        //   高密度な独立配列に変更し、BSFスキャンループ時のSIMD的アクセスと
        //   キャッシュヒット率を最大化。
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

        // OPT-L: MapVirtualKeyのキャッシュLUT (Kernel-Transition Elimination)
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
            // OPT-F: Store is relaxed here, synchronizes via explicit fence later
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