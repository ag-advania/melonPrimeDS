#ifndef MELON_PRIME_INPUT_STATE_H
#define MELON_PRIME_INPUT_STATE_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <mutex>

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

        static constexpr size_t kMaxHotkeyId = 128;
        void clearAllBindings() noexcept;
        void setHotkeyVks(int id, const std::vector<UINT>& vks);

        [[nodiscard]] bool hotkeyDown(int id) const noexcept;
        [[nodiscard]] bool hotkeyPressed(int id) noexcept;
        [[nodiscard]] bool hotkeyReleased(int id) noexcept;

        void resetHotkeyEdges() noexcept;

    private:
        alignas(64) std::atomic<uint64_t> m_vkDown[4];
        std::atomic<uint8_t> m_mouseButtons{ 0 };
        char m_cachePad[31];
        alignas(64) std::atomic<uint64_t> m_mouseDeltaCombined{ 0 };

        struct alignas(8) HotkeyMask {
            uint64_t vkMask[4];
            uint8_t  mouseMask;
            bool     hasMask;
            uint8_t  _pad[6];
        };
        static_assert(sizeof(HotkeyMask) == 40, "HotkeyMask size mismatch");

        alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask;
        uint64_t m_hkPrev[2];

        struct BtnLutEntry {
            uint8_t downBits;
            uint8_t upBits;
        };
        static std::array<BtnLutEntry, 1024> s_btnLut;

        struct VkRemapEntry {
            uint8_t normal;
            uint8_t extended;
        };
        static std::array<VkRemapEntry, 256> s_vkRemap;

        static uint16_t s_scancodeLShift;
        static uint16_t s_scancodeRShift;
        static std::once_flag s_initFlag;

        FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
            if (UNLIKELY(vk >= 256)) return;
            const uint32_t widx = vk >> 6;
            const uint64_t bit = 1ULL << (vk & 63);
            if (down) {
                m_vkDown[widx].fetch_or(bit, std::memory_order_release);
            }
            else {
                m_vkDown[widx].fetch_and(~bit, std::memory_order_release);
            }
        }

        FORCE_INLINE uint32_t remapVk(uint32_t vk, USHORT makeCode, USHORT flags) const noexcept {
            if (UNLIKELY(vk == VK_SHIFT)) {
                return (makeCode == s_scancodeLShift) ? VK_LSHIFT : VK_RSHIFT;
            }
            const VkRemapEntry& entry = s_vkRemap[vk];
            if (entry.normal != 0) {
                return (flags & RI_KEY_E0) ? entry.extended : entry.normal;
            }
            return vk;
        }

        union MouseDeltaPack {
            struct { int32_t x; int32_t y; } s;
            uint64_t combined;
        };
    };

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_INPUT_STATE_H