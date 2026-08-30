#include "MelonPrimeGameSettings.h"
#include "MelonPrimeRuntimeConfig.h"
#include "NDS.h"

namespace MelonPrime {

    bool MelonPrimeGameSettings::ApplyHeadphone(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.headphoneEnabled) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        constexpr uint8_t kMask = 0x18;
        if ((oldVal & kMask) == kMask) return false;

        nds->ARM9Write8(addr, oldVal | kMask);
        return true;
    }

    bool MelonPrimeGameSettings::ApplyLicenseColorStrict(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.licenseColorApply) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & 0x3F) | settings.licenseColorBits;
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::ApplySelectedHunterStrict(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.hunterApply) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & ~0x78) | (settings.hunterBits & 0x78);
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::UseDsName(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.useFirmwareName) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = oldVal & ~0x01;
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::ApplySfxVolume(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.sfxApply) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & 0xC0)
            | ((settings.sfxSteps & 0x0F) << 2) | 0x03;

        if (newVal == oldVal) return false;
        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::ApplyMusicVolume(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addr)
    {
        if (!nds || !settings.musicApply) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & ~0x3C)
            | ((settings.musicSteps & 0x0F) << 2);

        if (newVal == oldVal) return false;
        nds->ARM9Write8(addr, newVal);
        return true;
    }

    void MelonPrimeGameSettings::ApplyMphSensitivity(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit)
    {
        const uint16_t sensiVal = settings.mphSensitivityValue;
        if (nds->ARM9Read16(addrSensi) != sensiVal)
            nds->ARM9Write16(addrSensi, sensiVal);
        if (inGameInit && nds->ARM9Read16(addrInGame) != sensiVal)
            nds->ARM9Write16(addrInGame, sensiVal);
    }

    bool MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
        melonDS::NDS* nds, const MenuGameSettingsSnapshot& settings,
        melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5)
    {
        if (!nds || !settings.dataUnlockEnabled) return false;

        // Read-before-write: only write if the value was reset by the game.
        // On most frames nothing has changed so this is effectively a no-op.
        bool changed = false;
        const uint8_t v1 = nds->ARM9Read8(a1);
        if ((v1 & 0x03) != 0x03)               { nds->ARM9Write8(a1, v1 | 0x03);  changed = true; }
        if (nds->ARM9Read32(a2) != 0x07FFFFFFu) { nds->ARM9Write32(a2, 0x07FFFFFFu); changed = true; }
        if (nds->ARM9Read8(a3)  != 0x7F)        { nds->ARM9Write8(a3, 0x7F);       changed = true; }
        if (nds->ARM9Read32(a4) != 0xFFFFFFFFu) { nds->ARM9Write32(a4, 0xFFFFFFFFu); changed = true; }
        if (nds->ARM9Read8(a5)  != 0x7F)        { nds->ARM9Write8(a5, 0x7F);       changed = true; }
        return changed;
    }

    melonDS::u32 MelonPrimeGameSettings::CalculatePlayerAddress(
        melonDS::u32 base, melonDS::u8 pos, int32_t inc)
    {
        if (pos == 0) return base;
        const int64_t result = static_cast<int64_t>(base) + (static_cast<int64_t>(pos) * inc);
        if (result < 0 || result > UINT32_MAX) return base;
        return static_cast<melonDS::u32>(result);
    }

} // namespace MelonPrime
