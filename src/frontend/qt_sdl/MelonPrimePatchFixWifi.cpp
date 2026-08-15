#ifdef MELONPRIME_DS

#include "MelonPrimePatchFixWifi.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePatchState.h"
#include "NDS.h"

namespace MelonPrime {
namespace {

// Base address of the first patched word for each ROM group.
// JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
// JP1_1 / US1_1 / EU1_1 / KR1_0 already have the corrected behavior.
static constexpr uint32_t kBase[7] = {
    0x020662ECu, // JP1_0
    0xFFFFFFFFu, // JP1_1  (not applicable)
    0x020646BCu, // US1_0
    0xFFFFFFFFu, // US1_1  (not applicable)
    0x02064F38u, // EU1_0
    0xFFFFFFFFu, // EU1_1  (not applicable)
    0xFFFFFFFFu, // KR1_0  (not applicable)
};

struct PatchWord { uint32_t offset, applyVal, revertVal; };

// All three ROM versions share identical patch/revert values; only the base
// address differs.  51 entries covering 6 contiguous regions.
static constexpr PatchWord kWords[] = {
    // Region 1: set / clear write path (byte-indexed replacement)
    {0x000u, 0xE2052007u, 0xE3A01001u},
    {0x004u, 0xE3A01001u, 0xE1E01511u},
    {0x008u, 0xE7D031A5u, 0xE5903000u},
    {0x00Cu, 0xE1E02211u, 0xE5902004u},
    {0x010u, 0xE0033002u, 0xE0033001u},
    {0x014u, 0xE7C031A5u, 0xE0021FC1u},
    {0x018u, 0xE1A00000u, 0xE5803000u},
    {0x01Cu, 0xE1A00000u, 0xE5801004u},
    // Region 2: set path (second copy / OR side)
    {0x028u, 0xE2052007u, 0xE3A01001u},
    {0x02Cu, 0xE3A01001u, 0xE5904000u},
    {0x030u, 0xE7D031A5u, 0xE1A02511u},
    {0x034u, 0xE1833211u, 0xE5903004u},
    {0x038u, 0xE7C031A5u, 0xE1844511u},
    {0x03Cu, 0xE1A00000u, 0xE1831FC2u},
    {0x040u, 0xE1A00000u, 0xE5804000u},
    {0x044u, 0xE1A00000u, 0xE5801004u},
    // Region 3: test / read path
    {0x0ACu, 0xE7D021A5u, 0xE3A01001u},
    {0x0B0u, 0xE2051007u, 0xE5903000u},
    {0x0B4u, 0xE3A00001u, 0xE1A02511u},
    {0x0B8u, 0xE1A01110u, 0xE5900004u},
    {0x0BCu, 0xE0121001u, 0xE0033511u},
    {0x0C0u, 0x03A00000u, 0xE0001FC2u},
    {0x0C4u, 0x13A00001u, 0xE3A00000u},
    {0x0C8u, 0xE3500000u, 0xE1510000u},
    {0x0CCu, 0xE1A00000u, 0x01530000u},
    // Region 4: friend-slot test A
    {0x73Cu, 0xE7D021A9u, 0xE1A01914u},
    {0x740u, 0xE2091007u, 0xE5902000u},
    {0x744u, 0xE3A00001u, 0xE5900004u},
    {0x748u, 0xE1A01110u, 0xE0022914u},
    {0x74Cu, 0xE0121001u, 0xE0001FC1u},
    {0x750u, 0x03A00000u, 0xE3A00000u},
    {0x754u, 0x13A00001u, 0xE1510000u},
    {0x758u, 0xE3500000u, 0x01520000u},
    // Region 5: friend-slot test B
    {0x7D4u, 0xE7D021A9u, 0xE1A01914u},
    {0x7D8u, 0xE2091007u, 0xE5902000u},
    {0x7DCu, 0xE3A00001u, 0xE5900004u},
    {0x7E0u, 0xE1A01110u, 0xE0022914u},
    {0x7E4u, 0xE0121001u, 0xE0001FC1u},
    {0x7E8u, 0x03A00000u, 0xE3A00000u},
    {0x7ECu, 0x13A00001u, 0xE1510000u},
    {0x7F0u, 0xE3500000u, 0x01520000u},
    // Region 6: rival-slot test
    {0x8CCu, 0xE7D021AAu, 0xE5901000u},
    {0x8D0u, 0xE20A1007u, 0xE59D0008u},
    {0x8D4u, 0xE3A00001u, 0xE1A02A10u},
    {0x8D8u, 0xE1A01110u, 0xE0010A10u},
    {0x8DCu, 0xE0121001u, 0xE59F1088u},
    {0x8E0u, 0x03A00000u, 0xE5913004u},
    {0x8E4u, 0x13A00001u, 0xE3A01000u},
    {0x8E8u, 0xE3500000u, 0xE0032FC2u},
    {0x8ECu, 0xE1A00000u, 0xE1520001u},
    {0x8F0u, 0xE1A00000u, 0x01500001u},
};

// The ROM group selects the patch layout, but it is not a complete code
// signature.  Require every target word to be either the known original or
// the known patched instruction before writing anything.  This prevents a
// different dump/layout with the same detected group from being corrupted.
static bool CanApplyPatch(melonDS::NDS* nds, uint32_t base, bool* alreadyApplied)
{
    bool isApplied = true;
    for (const auto& w : kWords)
    {
        const uint32_t current = nds->ARM9Read32(base + w.offset);
        if (current != w.applyVal && current != w.revertVal)
            return false;
        if (current != w.applyVal)
            isApplied = false;
    }

    if (alreadyApplied)
        *alreadyApplied = isApplied;
    return true;
}

} // namespace

void FixWifi_ApplyOnce(
    MelonPrimePatchState& state,
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex)
{
    if (!cfg.GetBool(MelonPrime::CfgKey::WifiBitset)) return;

    auto& patchState = state.fixWifi;
    if (patchState.romGroupIndex != romGroupIndex)
    {
        patchState = {};
        patchState.romGroupIndex = romGroupIndex;
    }
    if (patchState.status != MelonPrimePatchState::FixWifiPatchState::Status::Unchecked)
        return;

    if (!nds)
        return;

    if (romGroupIndex >= 7 || kBase[romGroupIndex] == 0xFFFFFFFFu)
    {
        patchState.status = MelonPrimePatchState::FixWifiPatchState::Status::Unsupported;
        return;
    }

    const uint32_t base = kBase[romGroupIndex];

    bool alreadyApplied = false;
    if (!CanApplyPatch(nds, base, &alreadyApplied))
    {
        patchState.status = MelonPrimePatchState::FixWifiPatchState::Status::Rejected;
        return;
    }

    if (alreadyApplied)
    {
        patchState.status = MelonPrimePatchState::FixWifiPatchState::Status::Applied;
        return;
    }

    for (const auto& w : kWords)
        nds->ARM9Write32(base + w.offset, w.applyVal);

    patchState.status = MelonPrimePatchState::FixWifiPatchState::Status::Applied;
}

void FixWifi_ResetPatchState(MelonPrimePatchState& state)
{
    state.fixWifi = {};
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
