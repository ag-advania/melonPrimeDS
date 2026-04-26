#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimePatchNoHud.h"
#include "NDS.h"

namespace MelonPrime {

// =========================================================================
//  Per-element default-HUD hide patches.
//
//  Each element targets a single ARM instruction that the game executes to
//  draw one HUD piece. The patch value is what we write to suppress that
//  draw; the restoreValue is the original instruction read from the ROM.
//
//  Order of NoHudElement enum (must match):
//     0 NOHUD_HELMET
//     1 NOHUD_AMMO
//     2 NOHUD_WEAPONICON
//     3 NOHUD_HP
//     4 NOHUD_CROSSHAIR
//     5 NOHUD_SCORE_BATTLE        (mode 2)
//     6 NOHUD_SCORE_SURVIVAL      (mode 3)
//     7 NOHUD_SCORE_PRIMEHUNTER   (mode 4)
//     8 NOHUD_SCORE_BOUNTY        (mode 5)
//     9 NOHUD_SCORE_CAPTURE       (mode 6)
//    10 NOHUD_SCORE_DEFENDER      (mode 7)
//    11 NOHUD_SCORE_NODE          (mode 8)
//    12 NOHUD_BOMB                (bomb count HUD; boost ball HUD untouched)
//
//  Outer index matches RomGroup enum: JP1.0=0, JP1.1=1, US1.0=2, US1.1=3,
//                                     EU1.0=4, EU1.1=5, KR1.0=6.
// =========================================================================

struct NoHudElemEntry {
    uint32_t addr;
    uint32_t patchValue;
    uint32_t restoreValue;
};

static constexpr NoHudElemEntry kHudPatch[7][NOHUD_ELEMENT_COUNT] = {
    // JP1.0
    { {0x0202F934u, 0xE3C3300Eu, 0xE383300Eu},   // Helmet
      {0x0202F944u, 0xE1A00000u, 0xEB002AB7u},   // Ammo
      {0x0202F948u, 0xE1A00000u, 0xEB002A79u},   // WeaponIcon
      {0x0202F990u, 0xE1A00000u, 0xEB002BFBu},   // HP
      {0x020395B4u, 0xEA000004u, 0x1A000004u},   // Crosshair
      {0x02030564u, 0xE1A00000u, 0xEB0000B0u},   // Score: Battle
      {0x020302F0u, 0xE1A00000u, 0xEB00014Du},   // Score: Survival
      {0x020301F8u, 0xE1A00000u, 0xEB00018Bu},   // Score: PrimeHunter
      {0x0202FF44u, 0xE1A00000u, 0xEB000238u},   // Score: Bounty
      {0x0202FC84u, 0xE1A00000u, 0xEB0002E8u},   // Score: Capture
      {0x0202FB58u, 0xE1A00000u, 0xEB000333u},   // Score: Defender
      {0x02031818u, 0xE1A00000u, 0xEBFFFC03u},   // Score: Node
      {0x0203B2C4u, 0xEA000030u, 0x0A000030u} }, // Bomb
    // JP1.1 (identical to JP1.0)
    { {0x0202F934u, 0xE3C3300Eu, 0xE383300Eu},
      {0x0202F944u, 0xE1A00000u, 0xEB002AB7u},
      {0x0202F948u, 0xE1A00000u, 0xEB002A79u},
      {0x0202F990u, 0xE1A00000u, 0xEB002BFBu},
      {0x020395B4u, 0xEA000004u, 0x1A000004u},
      {0x02030564u, 0xE1A00000u, 0xEB0000B0u},
      {0x020302F0u, 0xE1A00000u, 0xEB00014Du},
      {0x020301F8u, 0xE1A00000u, 0xEB00018Bu},
      {0x0202FF44u, 0xE1A00000u, 0xEB000238u},
      {0x0202FC84u, 0xE1A00000u, 0xEB0002E8u},
      {0x0202FB58u, 0xE1A00000u, 0xEB000333u},
      {0x02031818u, 0xE1A00000u, 0xEBFFFC03u},
      {0x0203B2C4u, 0xEA000030u, 0x0A000030u} },
    // US1.0
    { {0x0202F91Cu, 0xE3C3300Eu, 0xE383300Eu},
      {0x0202F92Cu, 0xE1A00000u, 0xEB002A7Fu},
      {0x0202F930u, 0xE1A00000u, 0xEB002A41u},
      {0x0202F978u, 0xE1A00000u, 0xEB002BC4u},
      {0x020394A8u, 0xEA000004u, 0x1A000004u},
      {0x0203054Cu, 0xE1A00000u, 0xEB0000B0u},
      {0x020302D8u, 0xE1A00000u, 0xEB00014Du},
      {0x020301E0u, 0xE1A00000u, 0xEB00018Bu},
      {0x0202FF2Cu, 0xE1A00000u, 0xEB000238u},
      {0x0202FC6Cu, 0xE1A00000u, 0xEB0002E8u},
      {0x0202FB40u, 0xE1A00000u, 0xEB000333u},
      {0x02031700u, 0xE1A00000u, 0xEBFFFC43u},
      {0x0203B1D8u, 0xEA000030u, 0x0A000030u} },
    // US1.1
    { {0x0202F8ECu, 0xE3C3300Eu, 0xE383300Eu},
      {0x0202F8FCu, 0xE1A00000u, 0xEB002A5Cu},
      {0x0202F900u, 0xE1A00000u, 0xEB002A1Eu},
      {0x0202F948u, 0xE1A00000u, 0xEB002BA0u},
      {0x0203940Cu, 0xEA000004u, 0x1A000004u},
      {0x02030518u, 0xE1A00000u, 0xEB0000B0u},
      {0x020302A4u, 0xE1A00000u, 0xEB00014Du},
      {0x020301ACu, 0xE1A00000u, 0xEB00018Bu},
      {0x0202FEF8u, 0xE1A00000u, 0xEB000238u},
      {0x0202FC38u, 0xE1A00000u, 0xEB0002E8u},
      {0x0202FB0Cu, 0xE1A00000u, 0xEB000333u},
      {0x020316B4u, 0xE1A00000u, 0xEBFFFC49u},
      {0x0203B110u, 0xEA000030u, 0x0A000030u} },
    // EU1.0
    { {0x0202F8E4u, 0xE3C3300Eu, 0xE383300Eu},
      {0x0202F8F4u, 0xE1A00000u, 0xEB002A5Cu},
      {0x0202F8F8u, 0xE1A00000u, 0xEB002A1Eu},
      {0x0202F940u, 0xE1A00000u, 0xEB002BA0u},
      {0x02039404u, 0xEA000004u, 0x1A000004u},
      {0x02030510u, 0xE1A00000u, 0xEB0000B0u},
      {0x0203029Cu, 0xE1A00000u, 0xEB00014Du},
      {0x020301A4u, 0xE1A00000u, 0xEB00018Bu},
      {0x0202FEF0u, 0xE1A00000u, 0xEB000238u},
      {0x0202FC30u, 0xE1A00000u, 0xEB0002E8u},
      {0x0202FB04u, 0xE1A00000u, 0xEB000333u},
      {0x020316ACu, 0xE1A00000u, 0xEBFFFC49u},
      {0x0203B108u, 0xEA000030u, 0x0A000030u} },
    // EU1.1 (identical to US1.1)
    { {0x0202F8ECu, 0xE3C3300Eu, 0xE383300Eu},
      {0x0202F8FCu, 0xE1A00000u, 0xEB002A5Cu},
      {0x0202F900u, 0xE1A00000u, 0xEB002A1Eu},
      {0x0202F948u, 0xE1A00000u, 0xEB002BA0u},
      {0x0203940Cu, 0xEA000004u, 0x1A000004u},
      {0x02030518u, 0xE1A00000u, 0xEB0000B0u},
      {0x020302A4u, 0xE1A00000u, 0xEB00014Du},
      {0x020301ACu, 0xE1A00000u, 0xEB00018Bu},
      {0x0202FEF8u, 0xE1A00000u, 0xEB000238u},
      {0x0202FC38u, 0xE1A00000u, 0xEB0002E8u},
      {0x0202FB0Cu, 0xE1A00000u, 0xEB000333u},
      {0x020316B4u, 0xE1A00000u, 0xEBFFFC49u},
      {0x0203B110u, 0xEA000030u, 0x0A000030u} },
    // KR1.0
    { {0x02034898u, 0xE3C3300Eu, 0xE383300Eu},
      {0x020348A8u, 0xE1A00000u, 0xEBFFD87Fu},
      {0x020348ACu, 0xE1A00000u, 0xEBFFD8E7u},
      {0x020348F4u, 0xE1A00000u, 0xEBFFD70Cu},
      {0x0202B8E8u, 0xEA000004u, 0x1A000004u},
      {0x02033B48u, 0xE1A00000u, 0xEBFFFF08u},
      {0x02033B88u, 0xE1A00000u, 0xEBFFFEF8u},
      {0x02033DC8u, 0xE1A00000u, 0xEBFFFE68u},
      {0x02033EACu, 0xE1A00000u, 0xEBFFFE2Fu},
      {0x02034148u, 0xE1A00000u, 0xEBFFFD88u},
      {0x020343C8u, 0xE1A00000u, 0xEBFFFCE8u},
      {0x0203288Cu, 0xE1A00000u, 0xEB0003B7u},
      {0x02029D40u, 0xEA000031u, 0x0A000031u} },
};

static uint16_t s_appliedMask = 0;

void NoHudPatch_ResetState()
{
    s_appliedMask = 0;
}

uint16_t NoHudPatch_GetAppliedMask()
{
    return s_appliedMask;
}

void NoHudPatch_Sync(melonDS::NDS* nds, uint8_t romGroup, uint16_t desiredMask)
{
    desiredMask &= NOHUD_MASK_ALL;
    const uint16_t diff = static_cast<uint16_t>(s_appliedMask ^ desiredMask);
    if (diff == 0) return;

    const auto& entries = kHudPatch[romGroup];
    for (uint8_t i = 0; i < NOHUD_ELEMENT_COUNT; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if (!(diff & bit)) continue;
        const NoHudElemEntry& e = entries[i];
        const bool wantApply = (desiredMask & bit) != 0;
        nds->ARM9Write32(e.addr, wantApply ? e.patchValue : e.restoreValue);
    }
    s_appliedMask = desiredMask;
}

void NoHudPatch_RestoreAll(melonDS::NDS* nds, uint8_t romGroup)
{
    const auto& entries = kHudPatch[romGroup];
    for (uint8_t i = 0; i < NOHUD_ELEMENT_COUNT; ++i)
        nds->ARM9Write32(entries[i].addr, entries[i].restoreValue);
    s_appliedMask = 0;
}

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
