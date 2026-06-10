#ifdef MELONPRIME_DS

#include "MelonPrimePatchInstantAimFollow.h"
#include "MelonPrimePatchCommon.h"
#include "Config.h"
#include "MelonPrimeDef.h"

namespace MelonPrime {
namespace {

// ROM group order:
// JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchWord kPatchWords[][5] = {
    { // JP1.0
        { 0x02028070u, 0xE284009Cu, 0x1284009Cu },
        { 0x02028074u, 0xE284304Cu, 0x1284304Cu },
        { 0x02028078u, 0xE8900007u, 0x18900007u },
        { 0x0202807Cu, 0xE8830007u, 0x18830007u },
        { 0x02028080u, 0xEA00008Du, 0x1A00008Du },
    },
    { // JP1.1
        { 0x02028070u, 0xE284009Cu, 0x1284009Cu },
        { 0x02028074u, 0xE284304Cu, 0x1284304Cu },
        { 0x02028078u, 0xE8900007u, 0x18900007u },
        { 0x0202807Cu, 0xE8830007u, 0x18830007u },
        { 0x02028080u, 0xEA00008Du, 0x1A00008Du },
    },
    { // US1.0
        { 0x02028094u, 0xE284009Cu, 0x1284009Cu },
        { 0x02028098u, 0xE284304Cu, 0x1284304Cu },
        { 0x0202809Cu, 0xE8900007u, 0x18900007u },
        { 0x020280A0u, 0xE8830007u, 0x18830007u },
        { 0x020280A4u, 0xEA00008Du, 0x1A00008Du },
    },
    { // US1.1
        { 0x02028094u, 0xE284009Cu, 0x1284009Cu },
        { 0x02028098u, 0xE284304Cu, 0x1284304Cu },
        { 0x0202809Cu, 0xE8900007u, 0x18900007u },
        { 0x020280A0u, 0xE8830007u, 0x18830007u },
        { 0x020280A4u, 0xEA00008Du, 0x1A00008Du },
    },
    { // EU1.0
        { 0x0202808Cu, 0xE284009Cu, 0x1284009Cu },
        { 0x02028090u, 0xE284304Cu, 0x1284304Cu },
        { 0x02028094u, 0xE8900007u, 0x18900007u },
        { 0x02028098u, 0xE8830007u, 0x18830007u },
        { 0x0202809Cu, 0xEA00008Du, 0x1A00008Du },
    },
    { // EU1.1
        { 0x02028094u, 0xE284009Cu, 0x1284009Cu },
        { 0x02028098u, 0xE284304Cu, 0x1284304Cu },
        { 0x0202809Cu, 0xE8900007u, 0x18900007u },
        { 0x020280A0u, 0xE8830007u, 0x18830007u },
        { 0x020280A4u, 0xEA00008Du, 0x1A00008Du },
    },
    { // KR1.0
        { 0x0200B200u, 0xE1A00000u, 0x0A000003u },
        { 0u, 0u, 0u },
        { 0u, 0u, 0u },
        { 0u, 0u, 0u },
        { 0u, 0u, 0u },
    },
};

static constexpr RomPatchSpan kPatchSpans[7] = {
    { &kPatchWords[0][0], 5 },
    { &kPatchWords[1][0], 5 },
    { &kPatchWords[2][0], 5 },
    { &kPatchWords[3][0], 5 },
    { &kPatchWords[4][0], 5 },
    { &kPatchWords[5][0], 5 },
    { &kPatchWords[6][0], 1 },
};

static StaticWordPatch s_patch(kPatchSpans);

} // namespace

void InstantAimFollow_ApplyOnce(
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex)
{
    const bool disableAimSmoothing = cfg.GetBool(CfgKey::DisableMphAimSmoothing);
    const int lowLatencyAimMode = cfg.GetInt(CfgKey::LowLatencyAimMode);
    const bool shouldApply =
        disableAimSmoothing
        && (lowLatencyAimMode == LowLatencyAimMode::InstantAimFollow
            || (lowLatencyAimMode == LowLatencyAimMode::Off
                && cfg.GetBool(CfgKey::InstantAimFollow)));

    if (!shouldApply)
    {
        s_patch.RestoreOnce(nds, romGroupIndex);
        return;
    }

    s_patch.ApplyOnce(nds, romGroupIndex);
}

void InstantAimFollow_RestoreOnce(
    melonDS::NDS* nds,
    uint8_t romGroupIndex)
{
    s_patch.RestoreOnce(nds, romGroupIndex);
}

void InstantAimFollow_ResetPatchState()
{
    s_patch.ResetState();
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
