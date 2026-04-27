#ifndef MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
#define MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    void UseFirmwareLanguage_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
    void UseFirmwareLanguage_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
