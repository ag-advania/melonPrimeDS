#ifndef MELON_PRIME_HOTKEY_VK_BINDING_H
#define MELON_PRIME_HOTKEY_VK_BINDING_H

#ifdef _WIN32
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <windows.h>

namespace MelonPrime {

    class RawInputWinFilter;
    struct RawInputSubscription;

    // =========================================================================
    // SmallVkList: stack-allocated VK code container (max 4 entries).
    //
    // Replaces std::vector<UINT> in the hotkey binding path.
    // Most Qt key mappings produce 1-2 VK codes (e.g. VK_LSHIFT + VK_RSHIFT).
    // The maximum observed is 2 (Shift/Ctrl/Alt produce L+R variants).
    //
    // Eliminates 28 heap allocations per BindMetroidHotkeysFromConfig() call
    // (one per hotkey binding). At unpause + config-change time this runs
    // on the emu thread, so avoiding allocator contention is beneficial.
    // =========================================================================
    struct SmallVkList {
        static constexpr size_t kCapacity = 4;
        std::array<UINT, kCapacity> data{};
        uint8_t count = 0;

        void push_back(UINT vk) {
            if (count < kCapacity) data[count++] = vk;
        }
        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] size_t size() const { return count; }
        [[nodiscard]] const UINT* begin() const { return data.data(); }
        [[nodiscard]] const UINT* end() const { return data.data() + count; }
    };

    // Qt key code -> VK code(s), stack-allocated
    SmallVkList MapQtKeyIntToVks(int qtKey);

    // Cold-compiled ownership for the Windows gameplay hotkey source. A
    // binding is either represented exactly by InputState's existing
    // single-predicate VK mask, stays with the canonical Qt producer, or is a
    // wheel impulse handled by the separate wheel source. The masks are
    // disjoint and are consumed as fixed bitsets in the frame path.
    enum class GameplayBindingSource : uint8_t {
        RawExact,
        QtFallback,
        WheelImpulse,
    };

    struct RawHotkeyOwnership {
        uint64_t rawOwnedGameplayMask = 0;
        uint64_t qtFallbackGameplayMask = 0;
        uint64_t wheelImpulseMask = 0;
    };

    // Single hotkey bind
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, RawInputSubscription* subscription, int instance,
        const std::string& hkPath, int hkId);

    // Batch bind all Metroid hotkeys
    RawHotkeyOwnership BindMetroidHotkeysFromConfig(
        RawInputWinFilter* filter, RawInputSubscription* subscription, int instance);

} // namespace MelonPrime
#endif // _WIN32
#endif // MELON_PRIME_HOTKEY_VK_BINDING_H
