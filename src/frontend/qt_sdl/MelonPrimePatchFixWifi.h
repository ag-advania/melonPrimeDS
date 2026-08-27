#ifndef MELON_PRIME_PATCH_FIX_WIFI_H
#define MELON_PRIME_PATCH_FIX_WIFI_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct MelonPrimePatchState;

    void FixWifi_ApplyOnce(
        MelonPrimePatchState& state,
        melonDS::NDS* nds,
        bool enabled,
        uint8_t romGroupIndex);
    void FixWifi_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_FIX_WIFI_H
