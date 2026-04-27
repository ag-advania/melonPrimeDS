#ifndef MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
#define MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    // isInAdventureAddr: m_currentRom.isInAdventure — skips non-EU ROMs when byte == 0x02
    void UseFirmwareLanguage_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex, uint32_t isInAdventureAddr);
    void UseFirmwareLanguage_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
