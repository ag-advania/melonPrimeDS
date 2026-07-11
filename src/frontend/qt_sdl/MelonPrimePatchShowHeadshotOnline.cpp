#ifdef MELONPRIME_DS

#include "MelonPrimePatchShowHeadshotOnline.h"
#include "MelonPrimePatchCommon.h"
#include "MelonPrimePatchState.h"
#include "Config.h"
#include "MelonPrimeDef.h"

namespace MelonPrime {
namespace {

static constexpr const char* kCfgShowHeadshotOnline = MelonPrime::CfgKey::ShowHeadshotOnline;

// H228 HEADSHOT WiFi Force Standalone Display
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchWord kPatchWords[7] = {
    { 0x0201748Cu, 0xE1A00000u, 0x1A000016u }, // JP1.0
    { 0x0201748Cu, 0xE1A00000u, 0x1A000016u }, // JP1.1
    { 0x020174ACu, 0xE1A00000u, 0x1A000016u }, // US1.0
    { 0x020174B0u, 0xE1A00000u, 0x1A000016u }, // US1.1
    { 0x020174A4u, 0xE1A00000u, 0x1A000016u }, // EU1.0
    { 0x020174B0u, 0xE1A00000u, 0x1A000016u }, // EU1.1
    { 0x02019A44u, 0xE1A00000u, 0x1A000016u }, // KR1.0
};

static constexpr RomPatchSpan kPatchSpans[7] = {
    { &kPatchWords[0], 1 },
    { &kPatchWords[1], 1 },
    { &kPatchWords[2], 1 },
    { &kPatchWords[3], 1 },
    { &kPatchWords[4], 1 },
    { &kPatchWords[5], 1 },
    { &kPatchWords[6], 1 },
};

static const StaticWordPatch s_patch(kPatchSpans);

} // namespace

void ShowHeadshotOnline_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (!cfg.GetBool(kCfgShowHeadshotOnline))
    {
        s_patch.RestoreOnce(state.showHeadshotOnline, nds, romGroupIndex);
        return;
    }

    s_patch.ApplyOnce(state.showHeadshotOnline, nds, romGroupIndex);
}

void ShowHeadshotOnline_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex)
{
    s_patch.RestoreOnce(state.showHeadshotOnline, nds, romGroupIndex);
}

void ShowHeadshotOnline_ResetPatchState(MelonPrimePatchState& state)
{
    StaticWordPatch::ResetState(state.showHeadshotOnline);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
