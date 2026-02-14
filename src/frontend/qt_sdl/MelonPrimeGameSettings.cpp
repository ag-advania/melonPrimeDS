#include "MelonPrimeGameSettings.h"
#include "MelonPrimeDef.h"
#include "NDS.h"

namespace MelonPrime {

    uint16_t MelonPrimeGameSettings::SensiNumToSensiVal(double sensiNum)
    {
        constexpr double BASE_VAL = 2457.0; // 0x0999
        constexpr double STEP_VAL = 409.0;  // 0x0199

        // FMA-friendly: val + 0.5 then cast is a standard fast-round for positive numbers
        const double val = BASE_VAL + (sensiNum - 1.0) * STEP_VAL;
        return static_cast<uint16_t>(std::min(static_cast<uint32_t>(val + 0.5), 0xFFFFu));
    }

    bool MelonPrimeGameSettings::ApplyHeadphoneOnce(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
    {
        if (!nds || (flags & bit)) return false;
        if (!localCfg.GetBool(CfgKey::Headphone)) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        constexpr uint8_t kMask = 0x18;
        if ((oldVal & kMask) == kMask) { flags |= bit; return false; }

        nds->ARM9Write8(addr, oldVal | kMask);
        flags |= bit;
        return true;
    }

    bool MelonPrimeGameSettings::ApplyLicenseColorStrict(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
    {
        if (!nds || !localCfg.GetBool(CfgKey::LicColorApply)) return false;

        const int sel = localCfg.GetInt(CfgKey::LicColorSel);
        if (sel < 0 || sel > 2) return false;

        constexpr std::array<uint8_t, 3> kColorBits = { 0x00, 0x40, 0x80 };
        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & 0x3F) | kColorBits[sel];
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::ApplySelectedHunterStrict(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
    {
        if (!nds || !localCfg.GetBool(CfgKey::HunterApply)) return false;

        constexpr std::array<uint8_t, 7> kHunterBits = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 };
        const int sel = std::clamp(localCfg.GetInt(CfgKey::HunterSel), 0, 6);

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = (oldVal & ~0x78) | (kHunterBits[sel] & 0x78);
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::UseDsName(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr)
    {
        if (!nds || !localCfg.GetBool(CfgKey::UseFwName)) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t newVal = oldVal & ~0x01;
        if (newVal == oldVal) return false;

        nds->ARM9Write8(addr, newVal);
        return true;
    }

    bool MelonPrimeGameSettings::ApplySfxVolumeOnce(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
    {
        if (!nds || (flags & bit) || !localCfg.GetBool(CfgKey::SfxVolApply)) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt(CfgKey::SfxVol), 0, 9));
        const uint8_t newVal = (oldVal & 0xC0) | ((steps & 0x0F) << 2) | 0x03;

        if (newVal == oldVal) { flags |= bit; return false; }
        nds->ARM9Write8(addr, newVal);
        flags |= bit;
        return true;
    }

    bool MelonPrimeGameSettings::ApplyMusicVolumeOnce(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addr, uint8_t& flags, uint8_t bit)
    {
        if (!nds || (flags & bit) || !localCfg.GetBool(CfgKey::MusicVolApply)) return false;

        const uint8_t oldVal = nds->ARM9Read8(addr);
        const uint8_t steps = static_cast<uint8_t>(std::clamp(localCfg.GetInt(CfgKey::MusicVol), 0, 9));
        const uint8_t newVal = (oldVal & ~0x3C) | ((steps & 0x0F) << 2);

        if (newVal == oldVal) { flags |= bit; return false; }
        nds->ARM9Write8(addr, newVal);
        flags |= bit;
        return true;
    }

    void MelonPrimeGameSettings::ApplyMphSensitivity(
        melonDS::NDS* nds, Config::Table& localCfg, melonDS::u32 addrSensi, melonDS::u32 addrInGame, bool inGameInit)
    {
        const double mphSensi = localCfg.GetDouble(CfgKey::MphSens);
        const uint16_t sensiVal = SensiNumToSensiVal(mphSensi);
        nds->ARM9Write16(addrSensi, sensiVal);
        if (inGameInit) nds->ARM9Write16(addrInGame, sensiVal);
    }

    bool MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
        melonDS::NDS* nds, Config::Table& localCfg, uint8_t& flags, uint8_t bit,
        melonDS::u32 a1, melonDS::u32 a2, melonDS::u32 a3, melonDS::u32 a4, melonDS::u32 a5)
    {
        if ((flags & bit) || !localCfg.GetBool(CfgKey::DataUnlock)) return false;

        nds->ARM9Write8(a1, nds->ARM9Read8(a1) | 0x03);
        nds->ARM9Write32(a2, 0x07FFFFFF);
        nds->ARM9Write8(a3, 0x7F);
        nds->ARM9Write32(a4, 0xFFFFFFFF);
        nds->ARM9Write8(a5, 0x7F);

        flags |= bit;
        return true;
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
