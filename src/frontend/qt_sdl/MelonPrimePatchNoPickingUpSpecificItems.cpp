#ifdef MELONPRIME_DS

#include "MelonPrimePatchNoPickingUpSpecificItems.h"
#include "MelonPrimePatchState.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "NDS.h"

namespace MelonPrime {
namespace {

enum PickupPatchBit : uint8_t {
    PICKUP_DOUBLE_DAMAGE = 1u << 0,
    PICKUP_CLOAK         = 1u << 1,
    PICKUP_DEATHALT      = 1u << 2,
};

enum PickupItemType : uint8_t {
    ITEM_DOUBLE_DAMAGE = 3,
    ITEM_CLOAK         = 17,
    ITEM_DEATHALT      = 20,
};

struct PatchWord {
    uint8_t itemType;
    uint32_t applyVal;
    uint32_t revertVal;
    uint32_t legacySkipVal;
};

struct PatchSet {
    uint32_t cmpAddress;
    uint32_t addLsAddress;
    PatchWord words[3];
};

static constexpr const char* kCfgPowerUpPickupNoEffect =
    MelonPrime::CfgKey::PowerUpPickupNoEffectPowerUps;
static constexpr const char* kCfgPowerUpPickupNoEffectDoubleDamage =
    MelonPrime::CfgKey::PowerUpPickupNoEffectDoubleDamage;
static constexpr const char* kCfgPowerUpPickupNoEffectCloak =
    MelonPrime::CfgKey::PowerUpPickupNoEffectCloak;
static constexpr const char* kCfgPowerUpPickupNoEffectDeathalt =
    MelonPrime::CfgKey::PowerUpPickupNoEffectDeathalt;

static constexpr uint32_t kCmpItemTypeMaxWord = 0xE3500015u; // cmp r0,#0x15
static constexpr uint32_t kAddLsPcPcItemTypeWord = 0x908FF100u; // addls pc,pc,r0,lsl #2
static constexpr uint32_t kSwitchTablePcBias = 8u;

static constexpr PatchWord kJpUsEuWords[3] = {
    { ITEM_DOUBLE_DAMAGE, 0xEA0001BCu, 0xEA000139u, 0xEA0001C1u },
    { ITEM_CLOAK,         0xEA0001AEu, 0xEA00013Cu, 0xEA0001B3u },
    { ITEM_DEATHALT,      0xEA0001ABu, 0xEA00014Au, 0xEA0001B0u },
};

static constexpr PatchWord kKrWords[3] = {
    { ITEM_DOUBLE_DAMAGE, 0xEA0001C2u, 0xEA000140u, 0xEA0001C7u },
    { ITEM_CLOAK,         0xEA0001B4u, 0xEA000142u, 0xEA0001B9u },
    { ITEM_DEATHALT,      0xEA0001B1u, 0xEA00014Fu, 0xEA0001B6u },
};

// Item pickup switch entries for item type 3/17/20. Applying a word branches
// directly to the pickedUp=1 consume/delete exit. The item disappears, while
// the power-up timer/flag/HUD/effect handler is skipped.
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchSet kPatchSets[7] = {
    { 0x02019CC8u, 0x02019CCCu, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02019CC8u, 0x02019CCCu, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02019CECu, 0x02019CF0u, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02019CECu, 0x02019CF0u, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02019CE4u, 0x02019CE8u, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02019CECu, 0x02019CF0u, { kJpUsEuWords[0], kJpUsEuWords[1], kJpUsEuWords[2] } },
    { 0x02018C20u, 0x02018C24u, { kKrWords[0], kKrWords[1], kKrWords[2] } },
};

[[nodiscard]] static bool IsValidRomGroup(uint8_t romGroupIndex) noexcept
{
    return romGroupIndex < sizeof(kPatchSets) / sizeof(kPatchSets[0]);
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

[[nodiscard]] static constexpr uint32_t EntryAddress(const PatchSet& set, const PatchWord& word) noexcept
{
    return set.addLsAddress + kSwitchTablePcBias + static_cast<uint32_t>(word.itemType) * sizeof(uint32_t);
}

static_assert(EntryAddress(kPatchSets[0], kPatchSets[0].words[0]) == 0x02019CE0u, "JP1.0 Double Damage slot");
static_assert(EntryAddress(kPatchSets[0], kPatchSets[0].words[1]) == 0x02019D18u, "JP1.0 Cloak slot");
static_assert(EntryAddress(kPatchSets[0], kPatchSets[0].words[2]) == 0x02019D24u, "JP1.0 Deathalt slot");
static_assert(EntryAddress(kPatchSets[1], kPatchSets[1].words[0]) == 0x02019CE0u, "JP1.1 Double Damage slot");
static_assert(EntryAddress(kPatchSets[1], kPatchSets[1].words[1]) == 0x02019D18u, "JP1.1 Cloak slot");
static_assert(EntryAddress(kPatchSets[1], kPatchSets[1].words[2]) == 0x02019D24u, "JP1.1 Deathalt slot");
static_assert(EntryAddress(kPatchSets[2], kPatchSets[2].words[0]) == 0x02019D04u, "US1.0 Double Damage slot");
static_assert(EntryAddress(kPatchSets[2], kPatchSets[2].words[1]) == 0x02019D3Cu, "US1.0 Cloak slot");
static_assert(EntryAddress(kPatchSets[2], kPatchSets[2].words[2]) == 0x02019D48u, "US1.0 Deathalt slot");
static_assert(EntryAddress(kPatchSets[3], kPatchSets[3].words[0]) == 0x02019D04u, "US1.1 Double Damage slot");
static_assert(EntryAddress(kPatchSets[3], kPatchSets[3].words[1]) == 0x02019D3Cu, "US1.1 Cloak slot");
static_assert(EntryAddress(kPatchSets[3], kPatchSets[3].words[2]) == 0x02019D48u, "US1.1 Deathalt slot");
static_assert(EntryAddress(kPatchSets[4], kPatchSets[4].words[0]) == 0x02019CFCu, "EU1.0 Double Damage slot");
static_assert(EntryAddress(kPatchSets[4], kPatchSets[4].words[1]) == 0x02019D34u, "EU1.0 Cloak slot");
static_assert(EntryAddress(kPatchSets[4], kPatchSets[4].words[2]) == 0x02019D40u, "EU1.0 Deathalt slot");
static_assert(EntryAddress(kPatchSets[5], kPatchSets[5].words[0]) == 0x02019D04u, "EU1.1 Double Damage slot");
static_assert(EntryAddress(kPatchSets[5], kPatchSets[5].words[1]) == 0x02019D3Cu, "EU1.1 Cloak slot");
static_assert(EntryAddress(kPatchSets[5], kPatchSets[5].words[2]) == 0x02019D48u, "EU1.1 Deathalt slot");
static_assert(EntryAddress(kPatchSets[6], kPatchSets[6].words[0]) == 0x02018C38u, "KR Double Damage slot");
static_assert(EntryAddress(kPatchSets[6], kPatchSets[6].words[1]) == 0x02018C70u, "KR Cloak slot");
static_assert(EntryAddress(kPatchSets[6], kPatchSets[6].words[2]) == 0x02018C7Cu, "KR Deathalt slot");

[[nodiscard]] static bool ValidateSwitchLayout(melonDS::NDS* nds, const PatchSet& set)
{
    return nds
        && nds->ARM9Read32(set.cmpAddress) == kCmpItemTypeMaxWord
        && nds->ARM9Read32(set.addLsAddress) == kAddLsPcPcItemTypeWord;
}

[[nodiscard]] static bool CanTransitionWord(uint32_t current, const PatchWord& word, bool apply) noexcept
{
    const uint32_t desired = apply ? word.applyVal : word.revertVal;
    if (current == desired)
        return true;

    const uint32_t alternate = apply ? word.revertVal : word.applyVal;
    return current == alternate || current == word.legacySkipVal;
}

[[nodiscard]] static bool SetWordDesired(
    melonDS::NDS* nds,
    const PatchSet& set,
    const PatchWord& word,
    bool apply)
{
    const uint32_t address = EntryAddress(set, word);
    const uint32_t desired = apply ? word.applyVal : word.revertVal;
    const uint32_t current = nds->ARM9Read32(address);
    if (current == desired)
        return true;
    if (!CanTransitionWord(current, word, apply))
        return false;

    nds->ARM9Write32(address, desired);
    return true;
}

static void ApplyMask(MelonPrimePatchState& owner, melonDS::NDS* nds, uint8_t romGroupIndex, uint8_t desiredMask)
{
    auto& state = owner.noSpecificItemPickup;
    if (!nds || !IsValidRomGroup(romGroupIndex))
        return;

    if (state.hasAppliedRomGroup && state.appliedRomGroupIndex != romGroupIndex)
        NoPickingUpSpecificItems_RestoreOnce(owner, nds, state.appliedRomGroupIndex);

    const auto& set = kPatchSets[romGroupIndex];
    if (!ValidateSwitchLayout(nds, set))
        return;

    uint8_t newAppliedMask =
        (state.hasAppliedRomGroup && state.appliedRomGroupIndex == romGroupIndex) ? state.appliedMask : 0;
    for (uint8_t i = 0; i < 3; ++i)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        const bool shouldApply = (desiredMask & bit) != 0;
        if (!SetWordDesired(nds, set, set.words[i], shouldApply))
            continue;

        if (shouldApply)
            newAppliedMask |= bit;
        else
            newAppliedMask &= static_cast<uint8_t>(~bit);
    }

    state.hasAppliedRomGroup = newAppliedMask != 0;
    state.appliedRomGroupIndex = state.hasAppliedRomGroup ? romGroupIndex : 0xFFu;
    state.appliedMask = newAppliedMask;
}

} // namespace

void NoPickingUpSpecificItems_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    const uint8_t desiredMask = DesiredMask(cfg);
    // Always run through ApplyMask, even when disabled. Patch state can be
    // reset independently of RAM contents, and this heals any stale entries.
    ApplyMask(state, nds, romGroupIndex, desiredMask);
}

void NoPickingUpSpecificItems_RestoreOnce(MelonPrimePatchState& owner, melonDS::NDS* nds, uint8_t romGroupIndex)
{
    auto& state = owner.noSpecificItemPickup;
    if (!nds)
        return;
    if (IsValidRomGroup(state.appliedRomGroupIndex))
        romGroupIndex = state.appliedRomGroupIndex;
    if (!IsValidRomGroup(romGroupIndex))
        return;

    ApplyMask(owner, nds, romGroupIndex, 0);
}

void NoPickingUpSpecificItems_ResetPatchState(MelonPrimePatchState& owner)
{
    owner.noSpecificItemPickup = {};
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
