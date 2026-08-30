#ifndef MELONPRIME_SETTINGS_H
#define MELONPRIME_SETTINGS_H

#include <cstdint>

#include "types.h"

namespace melonDS { class NDS; }

namespace MelonPrime {

    struct MenuGameSettingsSnapshot;

    class MelonPrimeGameSettings
    {
    public:
        MelonPrimeGameSettings() = delete;

        static bool ApplyHeadphone(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static bool ApplySfxVolume(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static bool ApplyMusicVolume(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static bool ApplyLicenseColorStrict(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static bool ApplySelectedHunterStrict(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static bool UseDsName(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr);
        static void ApplyMphSensitivity(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit);
        static bool ApplyUnlockHuntersMaps(melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings,
            melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5);
        static melonDS::u32 CalculatePlayerAddress(melonDS::u32 base, melonDS::u8 pos, int32_t inc);
    };

} // namespace MelonPrime

#endif // MELONPRIME_SETTINGS_H
