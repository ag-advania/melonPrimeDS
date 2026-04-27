#ifndef MELON_PRIME_PATCH_FIX_WIFI_H
#define MELON_PRIME_PATCH_FIX_WIFI_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    void FixWifi_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
    void FixWifi_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_FIX_WIFI_H
