#ifdef MELONPRIME_DS

#include "MelonPrimePatchUseFirmwareLanguage.h"
#include "Config.h"
#include "NDS.h"

namespace MelonPrime {

// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
//
// This patch installs a code cave at 0x02003E00–0x02003E3C that intercepts the
// language-slot initialisation function and maps DS firmware language bytes to
// game-internal language slots (EU-style), with DS Japanese → slot 5 instead
// of the EU version's default of mapping Japanese to English.
//
// Each ROM has a unique branch hook address and three ROM-specific words inside
// the cave.  hookRevert is also the cave's epilogue "add sp, sp, #N" word
// (the hook site's original instruction, borrowed as the cave's own epilogue).

struct RomPatchData {
    uint32_t hookAddr;    // address of the instruction replaced by the cave branch
    uint32_t hookApply;   // branch instruction to write at hookAddr
    uint32_t hookRevert;  // original instruction (== cave word at 0x2C)
    uint32_t cave30;      // ROM-specific epilogue (ldm sp!, {...})
    uint32_t cave38;      // ROM-specific data pointer stored in cave
};

// All ROMs supported.  EU1_1 data also covers the Russian localisation which
// shares the same ROM binary.
static constexpr RomPatchData kRomData[7] = {
    // JP1_0
    { 0x0205D89Cu, 0xEAFE9957u, 0xE28DD054u, 0xE8BD40F0u, 0x020E98E8u },
    // JP1_1
    { 0x0205D89Cu, 0xEAFE9957u, 0xE28DD054u, 0xE8BD40F0u, 0x020E98A8u },
    // US1_0
    { 0x0205BBACu, 0xEAFEA093u, 0xE28DD050u, 0xE8BD41F0u, 0x020E77A8u },
    // US1_1
    { 0x0205C3C0u, 0xEAFE9E8Eu, 0xE28DD050u, 0xE8BD41F0u, 0x020E8268u },
    // EU1_0
    { 0x0205C420u, 0xEAFE9E76u, 0xE28DD054u, 0xE8BD4030u, 0x020E8288u },
    // EU1_1
    { 0x0205C46Cu, 0xEAFE9E63u, 0xE28DD054u, 0xE8BD4030u, 0x020E8308u },
    // KR1_0
    { 0x020567FCu, 0xEAFEB57Fu, 0xE28DD050u, 0xE8BD8008u, 0x020E10AAu },
};

// Fixed code-cave region in NDS main RAM (0x02003E00–0x02003E3C).
// Words at offsets 0x2C, 0x30, and 0x38 are ROM-specific; all others are shared.
static constexpr uint32_t kCaveBase = 0x02003E00u;

static bool s_applied = false;

void UseFirmwareLanguage_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (s_applied) return;
    if (!cfg.GetBool("Metroid.BugFix.UseFirmwareLanguage")) return;
    if (romGroupIndex >= 7) return;

    const RomPatchData& rom = kRomData[romGroupIndex];

    // Write the shared cave instructions
    nds->ARM9Write32(kCaveBase + 0x00u, 0xE59F0030u);
    nds->ARM9Write32(kCaveBase + 0x04u, 0xE5DD1000u);
    nds->ARM9Write32(kCaveBase + 0x08u, 0xE5D02000u);
    nds->ARM9Write32(kCaveBase + 0x0Cu, 0xE3C2203Fu);
    nds->ARM9Write32(kCaveBase + 0x10u, 0xE3510000u);
    nds->ARM9Write32(kCaveBase + 0x14u, 0x03822005u);
    nds->ARM9Write32(kCaveBase + 0x18u, 0x13510001u);
    nds->ARM9Write32(kCaveBase + 0x1Cu, 0x82411001u);
    nds->ARM9Write32(kCaveBase + 0x20u, 0x81822001u);
    nds->ARM9Write32(kCaveBase + 0x24u, 0x83822020u);
    nds->ARM9Write32(kCaveBase + 0x28u, 0xE5C02000u);
    // ROM-specific epilogue (original instruction at hook site, borrowed as cave epilogue)
    nds->ARM9Write32(kCaveBase + 0x2Cu, rom.hookRevert);
    nds->ARM9Write32(kCaveBase + 0x30u, rom.cave30);
    nds->ARM9Write32(kCaveBase + 0x34u, 0xE12FFF1Eu);
    nds->ARM9Write32(kCaveBase + 0x38u, rom.cave38);
    nds->ARM9Write32(kCaveBase + 0x3Cu, 0x00000000u);

    // Install the branch hook last so the cave is fully written before the
    // game can execute through it
    nds->ARM9Write32(rom.hookAddr, rom.hookApply);

    s_applied = true;
}

void UseFirmwareLanguage_ResetPatchState()
{
    s_applied = false;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
