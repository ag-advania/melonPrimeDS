#ifdef MELONPRIME_DS

#include "MelonPrimePatchDisableDoubleDamageMultiplier.h"
#include "Config.h"
#include "NDS.h"

namespace MelonPrime {
namespace {

struct PatchWord {
    uint32_t address;
    uint32_t applyVal;
    uint32_t revertVal;
};

static constexpr const char* kCfgDisableDoubleDamageMultiplier =
    "Metroid.GameFeature.DisableDoubleDamageMultiplier";

// Double Damage 2x -> 1x while keeping the status, timer, HUD, SFX and effects.
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchWord kPatchWords[7][8] = {
    { // JP1.0
        { 0x020173D4u, 0x11A00809u, 0x11A00889u },
        { 0x0200AFC8u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD4u, 0xE1A00000u, 0xE1A00080u },
        { 0x0202409Cu, 0xE1A00000u, 0xE1A00080u },
        { 0x020240A8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414B8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414C4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414D0u, 0xE1A00000u, 0xE1A00080u },
    },
    { // JP1.1
        { 0x020173D4u, 0x11A00809u, 0x11A00889u },
        { 0x0200AFC8u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD4u, 0xE1A00000u, 0xE1A00080u },
        { 0x0202409Cu, 0xE1A00000u, 0xE1A00080u },
        { 0x020240A8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414B8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414C4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020414D0u, 0xE1A00000u, 0xE1A00080u },
    },
    { // US1.0
        { 0x020173F4u, 0x11A00809u, 0x11A00889u },
        { 0x0200AFC8u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240C0u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240CCu, 0xE1A00000u, 0xE1A00080u },
        { 0x02041370u, 0xE1A00000u, 0xE1A00080u },
        { 0x0204137Cu, 0xE1A00000u, 0xE1A00080u },
        { 0x02041388u, 0xE1A00000u, 0xE1A00080u },
    },
    { // US1.1
        { 0x020173F8u, 0x11A00809u, 0x11A00889u },
        { 0x0200AFC8u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240C0u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240CCu, 0xE1A00000u, 0xE1A00080u },
        { 0x020412A0u, 0xE1A00000u, 0xE1A00080u },
        { 0x020412ACu, 0xE1A00000u, 0xE1A00080u },
        { 0x020412B8u, 0xE1A00000u, 0xE1A00080u },
    },
    { // EU1.0
        { 0x020173ECu, 0x11A00809u, 0x11A00889u },
        { 0x0200AFCCu, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240B8u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240C4u, 0xE1A00000u, 0xE1A00080u },
        { 0x02041298u, 0xE1A00000u, 0xE1A00080u },
        { 0x020412A4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020412B0u, 0xE1A00000u, 0xE1A00080u },
    },
    { // EU1.1
        { 0x020173F8u, 0x11A00809u, 0x11A00889u },
        { 0x0200AFC8u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200AFD4u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240C0u, 0xE1A00000u, 0xE1A00080u },
        { 0x020240CCu, 0xE1A00000u, 0xE1A00080u },
        { 0x020412A0u, 0xE1A00000u, 0xE1A00080u },
        { 0x020412ACu, 0xE1A00000u, 0xE1A00080u },
        { 0x020412B8u, 0xE1A00000u, 0xE1A00080u },
    },
    { // KR1.0
        { 0x02019990u, 0x11A00805u, 0x11A00885u },
        { 0x0200F614u, 0xE1A00000u, 0xE1A00080u },
        { 0x0200F620u, 0xE1A00000u, 0xE1A00080u },
        { 0x02027774u, 0xE1A00000u, 0xE1A00080u },
        { 0x02027780u, 0xE1A00000u, 0xE1A00080u },
        { 0x0203A6C0u, 0xE1A00000u, 0xE1A00080u },
        { 0x0203A6CCu, 0xE1A00000u, 0xE1A00080u },
        { 0x0203A6D8u, 0xE1A00000u, 0xE1A00080u },
    },
};

static bool s_applied = false;
static uint8_t s_appliedRomGroupIndex = 0xFFu;

[[nodiscard]] static bool IsValidRomGroup(uint8_t romGroupIndex) noexcept
{
    return romGroupIndex < 7;
}

[[nodiscard]] static bool CanWritePatch(melonDS::NDS* nds, uint8_t romGroupIndex, bool apply)
{
    if (!nds || !IsValidRomGroup(romGroupIndex))
        return false;

    const auto& words = kPatchWords[romGroupIndex];
    for (const auto& word : words)
    {
        const uint32_t current = nds->ARM9Read32(word.address);
        const uint32_t expected = apply ? word.revertVal : word.applyVal;
        const uint32_t already = apply ? word.applyVal : word.revertVal;
        if (current != expected && current != already)
            return false;
    }
    return true;
}

static void WritePatch(melonDS::NDS* nds, uint8_t romGroupIndex, bool apply)
{
    const auto& words = kPatchWords[romGroupIndex];
    for (const auto& word : words)
        nds->ARM9Write32(word.address, apply ? word.applyVal : word.revertVal);
}

} // namespace

void DisableDoubleDamageMultiplier_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (!cfg.GetBool(kCfgDisableDoubleDamageMultiplier))
    {
        DisableDoubleDamageMultiplier_RestoreOnce(nds, s_appliedRomGroupIndex);
        return;
    }

    if (!IsValidRomGroup(romGroupIndex))
        return;
    if (s_applied && s_appliedRomGroupIndex == romGroupIndex)
        return;
    if (s_applied)
        DisableDoubleDamageMultiplier_RestoreOnce(nds, s_appliedRomGroupIndex);
    if (!CanWritePatch(nds, romGroupIndex, true))
        return;

    WritePatch(nds, romGroupIndex, true);
    s_applied = true;
    s_appliedRomGroupIndex = romGroupIndex;
}

void DisableDoubleDamageMultiplier_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
{
    if (!s_applied || !nds)
        return;
    if (IsValidRomGroup(s_appliedRomGroupIndex))
        romGroupIndex = s_appliedRomGroupIndex;
    if (!IsValidRomGroup(romGroupIndex))
        return;
    if (!CanWritePatch(nds, romGroupIndex, false))
        return;

    WritePatch(nds, romGroupIndex, false);
    s_applied = false;
    s_appliedRomGroupIndex = 0xFFu;
}

void DisableDoubleDamageMultiplier_ResetPatchState()
{
    s_applied = false;
    s_appliedRomGroupIndex = 0xFFu;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
