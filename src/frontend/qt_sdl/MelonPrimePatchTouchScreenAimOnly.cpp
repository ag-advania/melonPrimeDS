#ifdef MELONPRIME_DS

#include "MelonPrimePatchTouchScreenAimOnly.h"
#include "MelonPrimePatchCommon.h"
#include "MelonPrimePatchState.h"
#include "Config.h"
#include "MelonPrimeDef.h"

namespace MelonPrime {
namespace {

static constexpr const char* kCfgTouchScreenAimOnly = MelonPrime::CfgKey::TouchScreenAimOnly;

// Battle spatial aim-only (in-match touch screen).
//
// The in-match Battle touch handler hit-tests three HUD rectangles on the
// bottom screen -- Morph Ball (id 4), the weapon quick slots (ids 0..2) and the
// weapon menu (id 3). Each hit-test call is followed by `cmp r0,#0`, so
// replacing the call with `mov r0,#0` takes the normal "no hit" path: the
// touch neither sets NoAimInput nor launches the HUD action, and the whole
// bottom screen stays available as aim input.
//
// Deliberately out of scope (kept working): the double-tap jump gesture and the
// touch boost gesture. Those live in different handlers.
static constexpr uint32_t kMovR0Zero = 0xE3A00000u; // mov r0,#0

// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
// Word order per group: Morph Ball, weapon quick slots, weapon menu.
static constexpr PatchWord kWordsJp[3] = {
    { 0x02026C70u, kMovR0Zero, 0xEB004943u },
    { 0x02026E40u, kMovR0Zero, 0xEB0048CFu },
    { 0x02026F70u, kMovR0Zero, 0xEB004883u },
};

static constexpr PatchWord kWordsUs10[3] = {
    { 0x02026C94u, kMovR0Zero, 0xEB0048F7u },
    { 0x02026E64u, kMovR0Zero, 0xEB004883u },
    { 0x02026F94u, kMovR0Zero, 0xEB004837u },
};

// US1_1 and EU1_1 share both addresses and call words.
static constexpr PatchWord kWordsUs11Eu11[3] = {
    { 0x02026C94u, kMovR0Zero, 0xEB0048D0u },
    { 0x02026E64u, kMovR0Zero, 0xEB00485Cu },
    { 0x02026F94u, kMovR0Zero, 0xEB004810u },
};

static constexpr PatchWord kWordsEu10[3] = {
    { 0x02026C8Cu, kMovR0Zero, 0xEB0048D0u },
    { 0x02026E5Cu, kMovR0Zero, 0xEB00485Cu },
    { 0x02026F8Cu, kMovR0Zero, 0xEB004810u },
};

static constexpr PatchWord kWordsKr[3] = {
    { 0x0200C308u, kMovR0Zero, 0xEB007E20u },
    { 0x0200C4C8u, kMovR0Zero, 0xEB007DB0u },
    { 0x0200C5E8u, kMovR0Zero, 0xEB007D68u },
};

static constexpr RomPatchSpan kPatchSpans[7] = {
    { kWordsJp,       3 }, // JP1.0
    { kWordsJp,       3 }, // JP1.1
    { kWordsUs10,     3 }, // US1.0
    { kWordsUs11Eu11, 3 }, // US1.1
    { kWordsEu10,     3 }, // EU1.0
    { kWordsUs11Eu11, 3 }, // EU1.1
    { kWordsKr,       3 }, // KR1.0
};

static const StaticWordPatch s_patch(kPatchSpans);

} // namespace

void TouchScreenAimOnly_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (!cfg.GetBool(kCfgTouchScreenAimOnly))
    {
        s_patch.RestoreOnce(state.touchScreenAimOnly, nds, romGroupIndex);
        return;
    }

    s_patch.ApplyOnce(state.touchScreenAimOnly, nds, romGroupIndex);
}

void TouchScreenAimOnly_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex)
{
    s_patch.RestoreOnce(state.touchScreenAimOnly, nds, romGroupIndex);
}

void TouchScreenAimOnly_ResetPatchState(MelonPrimePatchState& state)
{
    StaticWordPatch::ResetState(state.touchScreenAimOnly);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
