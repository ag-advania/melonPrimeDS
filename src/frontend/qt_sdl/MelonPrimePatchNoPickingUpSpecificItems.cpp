#ifdef MELONPRIME_DS

#include "MelonPrimePatchNoPickingUpSpecificItems.h"
#include "Config.h"
#include "NDS.h"

namespace MelonPrime {
namespace {

enum PickupPatchBit : uint8_t {
    PICKUP_DOUBLE_DAMAGE = 1u << 0,
    PICKUP_CLOAK         = 1u << 1,
    PICKUP_DEATHALT      = 1u << 2,
};

struct PatchWord {
    uint32_t address;
    uint32_t applyVal;
    uint32_t revertVal;
};

static constexpr const char* kCfgPowerUpPickupNoEffect =
    "Metroid.GameFeature.PowerUpPickupNoEffectPowerUps";
static constexpr const char* kCfgPowerUpPickupNoEffectDoubleDamage =
    "Metroid.GameFeature.PowerUpPickupNoEffectDoubleDamage";
static constexpr const char* kCfgPowerUpPickupNoEffectCloak =
    "Metroid.GameFeature.PowerUpPickupNoEffectCloak";
static constexpr const char* kCfgPowerUpPickupNoEffectDeathalt =
    "Metroid.GameFeature.PowerUpPickupNoEffectDeathalt";

// Item pickup switch entries for item type 3/17/20. Applying a word branches
// directly to the pickedUp=1 consume/delete exit. The item disappears, while
// the power-up timer/flag/HUD/effect handler is skipped.
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
// Entry order: Double Damage, Cloak, Deathalt.
static constexpr PatchWord kPatchWords[7][3] = {
    { // JP1.0
        { 0x02019CE0u, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D18u, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D24u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // JP1.1
        { 0x02019CE0u, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D18u, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D24u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // US1.0
        { 0x02019D04u, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D3Cu, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D48u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // US1.1
        { 0x02019D04u, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D3Cu, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D48u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // EU1.0
        { 0x02019CFCu, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D34u, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D40u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // EU1.1
        { 0x02019D04u, 0xEA0001BCu, 0xEA000139u },
        { 0x02019D3Cu, 0xEA0001AEu, 0xEA00013Cu },
        { 0x02019D48u, 0xEA0001ABu, 0xEA00014Au },
    },
    { // KR1.0
        { 0x02018C38u, 0xEA0001C2u, 0xEA000140u },
        { 0x02018C70u, 0xEA0001B4u, 0xEA000142u },
        { 0x02018C7Cu, 0xEA0001B1u, 0xEA00014Fu },
    },
};

static bool s_hasAppliedRomGroup = false;
static uint8_t s_appliedRomGroupIndex = 0xFFu;
static uint8_t s_appliedMask = 0;

[[nodiscard]] static bool IsValidRomGroup(uint8_t romGroupIndex) noexcept
{
    return romGroupIndex < 7;
}

[[nodiscard]] static uint8_t DesiredMask(Config::Table& cfg)
{
    if (cfg.GetBool(kCfgPowerUpPickupNoEffect)) {
        return static_cast<uint8_t>(PICKUP_DOUBLE_DAMAGE | PICKUP_CLOAK | PICKUP_DEATHALT);
    }

    uint8_t mask = 0;
    if (cfg.GetBool(kCfgPowerUpPickupNoEffectDoubleDamage))
        mask |= PICKUP_DOUBLE_DAMAGE;
    if (cfg.GetBool(kCfgPowerUpPickupNoEffectCloak))
        mask |= PICKUP_CLOAK;
    if (cfg.GetBool(kCfgPowerUpPickupNoEffectDeathalt))
        mask |= PICKUP_DEATHALT;
    return mask;
}

[[nodiscard]] static bool IsArmUnconditionalBranch(uint32_t opcode) noexcept
{
    return (opcode & 0xFF000000u) == 0xEA000000u;
}

[[nodiscard]] static bool SetWordDesired(melonDS::NDS* nds, const PatchWord& word, bool apply)
{
    if (!nds)
        return false;

    const uint32_t desired = apply ? word.applyVal : word.revertVal;
    const uint32_t alternate = apply ? word.revertVal : word.applyVal;
    const uint32_t current = nds->ARM9Read32(word.address);
    if (current == desired)
        return true;
    if (current != alternate) {
        // These switch entries are ARM B instructions in every supported ROM.
        // Accept any branch here so live reload can migrate from the previous
        // skip-to-next implementation and restore from stale patched variants.
        if (!IsArmUnconditionalBranch(current))
            return false;
    }

    nds->ARM9Write32(word.address, desired);
    return true;
}

static void ApplyMask(melonDS::NDS* nds, uint8_t romGroupIndex, uint8_t desiredMask)
{
    if (!nds || !IsValidRomGroup(romGroupIndex))
        return;

    if (s_hasAppliedRomGroup && s_appliedRomGroupIndex != romGroupIndex)
        NoPickingUpSpecificItems_RestoreOnce(nds, s_appliedRomGroupIndex);

    uint8_t newAppliedMask = s_appliedMask;
    const auto& words = kPatchWords[romGroupIndex];
    for (uint8_t i = 0; i < 3; ++i)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        const bool shouldApply = (desiredMask & bit) != 0;
        if (!SetWordDesired(nds, words[i], shouldApply))
            continue;

        if (shouldApply)
            newAppliedMask |= bit;
        else
            newAppliedMask &= static_cast<uint8_t>(~bit);
    }

    s_hasAppliedRomGroup = newAppliedMask != 0;
    s_appliedRomGroupIndex = s_hasAppliedRomGroup ? romGroupIndex : 0xFFu;
    s_appliedMask = newAppliedMask;
}

} // namespace

void NoPickingUpSpecificItems_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    const uint8_t desiredMask = DesiredMask(cfg);
    // Always run through ApplyMask, even when disabled. Patch state can be
    // reset independently of RAM contents, and this heals any stale entries.
    ApplyMask(nds, romGroupIndex, desiredMask);
}

void NoPickingUpSpecificItems_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
{
    if (!nds)
        return;
    if (IsValidRomGroup(s_appliedRomGroupIndex))
        romGroupIndex = s_appliedRomGroupIndex;
    if (!IsValidRomGroup(romGroupIndex))
        return;

    ApplyMask(nds, romGroupIndex, 0);
}

void NoPickingUpSpecificItems_ResetPatchState()
{
    s_hasAppliedRomGroup = false;
    s_appliedRomGroupIndex = 0xFFu;
    s_appliedMask = 0;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
