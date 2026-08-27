#ifndef MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
#define MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace melonDS { class NDS; }

namespace MelonPrime {

    // Resolve the firmware language once at a cold config/firmware boundary.
    uint8_t UseFirmwareLanguage_ResolveTarget(melonDS::NDS* nds) noexcept;
    // isInAdventureAddr: m_currentRom.isInAdventure — skips JP ROMs when byte == 0x02
    void UseFirmwareLanguage_ApplyOnce(
        melonDS::NDS* nds,
        bool enabled,
        uint8_t romGroupIndex,
        uint32_t isInAdventureAddr,
        uint8_t gameLanguageBits);
    void UseFirmwareLanguage_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_USE_FIRMWARE_LANGUAGE_H
