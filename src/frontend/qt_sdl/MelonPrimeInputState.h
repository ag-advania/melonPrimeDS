#ifndef MELON_PRIME_INPUT_STATE_H
#define MELON_PRIME_INPUT_STATE_H

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>

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

class InputState {
public:
    InputState() noexcept;
    ~InputState() = default;

    // Non-copyable
    InputState(const InputState&) = delete;
    InputState& operator=(const InputState&) = delete;

    static void InitializeTables() noexcept;

    // Raw input processing
    void processRawInput(HRAWINPUT hRaw) noexcept;
    void processRawInputBatched() noexcept;

    // Mouse state
    void fetchMouseDelta(int& outX, int& outY) noexcept;
    void discardDeltas() noexcept;

    // Reset functions
    void resetAllKeys() noexcept;
    void resetMouseButtons() noexcept;

    // Hotkey system
    static constexpr size_t kMaxHotkeyId = 128;
    void clearAllBindings() noexcept;
    void setHotkeyVks(int id, const std::vector<UINT>& vks);
    [[nodiscard]] bool hotkeyDown(int id) const noexcept;
    [[nodiscard]] bool hotkeyPressed(int id) noexcept;
    [[nodiscard]] bool hotkeyReleased(int id) noexcept;
    void resetHotkeyEdges() noexcept;

private:
    // =========================================================================
    // Hot data - frequently accessed, cache-line aligned
    // =========================================================================

    // Virtual key state: 256 keys = 4 x 64-bit words (32 bytes)
    // Aligned to cache line for optimal access
    alignas(64) std::atomic<uint64_t> m_vkDown[4];

    // Mouse button state (1 byte)
    // Offset: 32 bytes
    std::atomic<uint8_t> m_mouseButtons{ 0 };

    // Padding to ensure m_mouseDeltaCombined starts on a fresh cache line.
    // m_vkDown(32) + m_mouseButtons(1) = 33 bytes used.
    // 64 - 33 = 31 bytes padding needed to finish this line.
    char m_cachePad[31];

    // Mouse delta accumulator - frequently updated from input thread
    // This MUST be on its own cache line to prevent false sharing with m_vkDown
    alignas(64) std::atomic<uint64_t> m_mouseDeltaCombined{ 0 };

    // =========================================================================
    // Cold data - less frequently accessed
    // =========================================================================

    // Hotkey masks
    struct HotkeyMask {
        uint64_t vkMask[4];   // Which VKs trigger this hotkey
        uint8_t  mouseMask;   // Which mouse buttons trigger this hotkey
        bool     hasMask;     // Is this hotkey configured?
        uint8_t  _pad[6];
    };
    static_assert(sizeof(HotkeyMask) == 40, "HotkeyMask size mismatch");

    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask;

    // Previous hotkey states for edge detection (128 bits = 2 words)
    uint64_t m_hkPrev[2];

    // =========================================================================
    // Static lookup tables
    // =========================================================================

    // Mouse button flag -> down/up bits mapping
    struct BtnLutEntry {
        uint8_t downBits;
        uint8_t upBits;
    };
    static BtnLutEntry s_btnLut[1024];

    // VK remapping for extended keys (Ctrl/Alt/Shift L/R)
    struct VkRemapEntry {
        uint8_t normal;
        uint8_t extended;
    };
    static VkRemapEntry s_vkRemap[256];

    // Cached scancodes for shift key disambiguation
    static uint16_t s_scancodeLShift;
    static uint16_t s_scancodeRShift;

    static std::atomic<bool> s_tablesInitialized;

    // =========================================================================
    // Helper functions - inlined for performance
    // =========================================================================

    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (UNLIKELY(vk >= 256)) return;

        const uint32_t widx = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);

        if (down) {
            // Use fetch_or for atomic set - avoid CAS loop
            m_vkDown[widx].fetch_or(bit, std::memory_order_relaxed);
        }
        else {
            m_vkDown[widx].fetch_and(~bit, std::memory_order_relaxed);
        }
    }

    FORCE_INLINE uint32_t remapVk(uint32_t vk, USHORT makeCode, USHORT flags) const noexcept {
        // Optimization: Removed MapVirtualKeyW call from hot path.
        // Uses cached scan codes for comparison.
        if (UNLIKELY(vk == VK_SHIFT)) {
            return (makeCode == s_scancodeLShift) ? VK_LSHIFT : VK_RSHIFT;
        }

        const VkRemapEntry& entry = s_vkRemap[vk];
        if (entry.normal != 0) {
            return (flags & RI_KEY_E0) ? entry.extended : entry.normal;
        }
        return vk;
    }

    // Union for efficient mouse delta packing/unpacking
    union MouseDeltaPack {
        struct { int32_t x; int32_t y; } s;
        uint64_t combined;
    };
};

#endif // _WIN32
#endif // MELON_PRIME_INPUT_STATE_H