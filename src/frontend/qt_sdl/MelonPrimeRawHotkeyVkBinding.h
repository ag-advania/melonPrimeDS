#ifndef MELON_PRIME_HOTKEY_VK_BINDING_H
#define MELON_PRIME_HOTKEY_VK_BINDING_H

#ifdef _WIN32
#include <vector>
#include <string>
#include <cstdint>
#include <windows.h>

#include "MelonPrimeRawHotkeyVkMapping.h"

namespace MelonPrime {

    class RawInputWinFilter;
    struct RawInputSubscription;

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
