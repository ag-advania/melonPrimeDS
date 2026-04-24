#ifdef MELONPRIME_DS

#include "MelonPrimePatchOsdColor.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "EmuInstance.h"
#include "NDS.h"

namespace MelonPrime {

// =========================================================================
//  OSD color patch
//
//  Two complementary layers applied together on each dirty cycle:
//
//  1. Literal pool patch:
//     Rewrites the BGR555 color literals loaded by the game's OSD renderer,
//     so every NEW message display reads the patched color. Also installs the
//     H211 "node stolen" shim.
//
//  2. Runtime slot override:
//     Writes the global color directly into the 20 in-RAM OSD slot structs
//     (entry+0x10, halfword). Immediately updates CURRENTLY DISPLAYED messages.
//     Active only when globalEnabled && applyGlobal.
//
//  s_osdConfigDirty: set on settings save or game leave/reset.
//  OsdColor_ApplyOnce: no-op when clean (single bool check).
//
//  H211 "node stolen" uses a hardcoded ARM immediate rather than a patchable
//  literal, so it gets its own 10-instruction color-encoding shim.
// =========================================================================

static constexpr const char* kCfgOsdColorEnable      = "Metroid.Visual.OsdColor";
static constexpr const char* kCfgOsdColorApplyGlobal  = "Metroid.Visual.OsdColorApplyGlobal";
static constexpr const char* kCfgOsdColorH211Enable   = "Metroid.Visual.OsdColorH211";
static constexpr const char* kCfgOsdColorH211R        = "Metroid.Visual.OsdColorH211R";
static constexpr const char* kCfgOsdColorH211G        = "Metroid.Visual.OsdColorH211G";
static constexpr const char* kCfgOsdColorH211B        = "Metroid.Visual.OsdColorH211B";

// -------------------------------------------------------------------------
//  Runtime slot addresses — entry+0x10, halfword (BGR555)
//  Order: JP1.0, JP1.1, US1.0, US1.1, EU1.0, EU1.1, KR1.0
// -------------------------------------------------------------------------
static constexpr int OSD_SLOT_COUNT = 20;

static constexpr uint32_t kOsdSlotAddr[7][OSD_SLOT_COUNT] = {
    // JP1.0
    {0x020E5B48,0x020E5BE4,0x020E5C80,0x020E5D1C,0x020E5DB8,
     0x020E5E54,0x020E5EF0,0x020E5F8C,0x020E6028,0x020E60C4,
     0x020E6160,0x020E61FC,0x020E6298,0x020E6334,0x020E63D0,
     0x020E646C,0x020E6508,0x020E65A4,0x020E6640,0x020E66DC},
    // JP1.1
    {0x020E5B08,0x020E5BA4,0x020E5C40,0x020E5CDC,0x020E5D78,
     0x020E5E14,0x020E5EB0,0x020E5F4C,0x020E5FE8,0x020E6084,
     0x020E6120,0x020E61BC,0x020E6258,0x020E62F4,0x020E6390,
     0x020E642C,0x020E64C8,0x020E6564,0x020E6600,0x020E669C},
    // US1.0
    {0x020E3C4C,0x020E3CCC,0x020E3D4C,0x020E3DCC,0x020E3E4C,
     0x020E3ECC,0x020E3F4C,0x020E3FCC,0x020E404C,0x020E40CC,
     0x020E414C,0x020E41CC,0x020E424C,0x020E42CC,0x020E434C,
     0x020E43CC,0x020E444C,0x020E44CC,0x020E454C,0x020E45CC},
    // US1.1
    {0x020E44E4,0x020E4580,0x020E461C,0x020E46B8,0x020E4754,
     0x020E47F0,0x020E488C,0x020E4928,0x020E49C4,0x020E4A60,
     0x020E4AFC,0x020E4B98,0x020E4C34,0x020E4CD0,0x020E4D6C,
     0x020E4E08,0x020E4EA4,0x020E4F40,0x020E4FDC,0x020E5078},
    // EU1.0
    {0x020E4504,0x020E45A0,0x020E463C,0x020E46D8,0x020E4774,
     0x020E4810,0x020E48AC,0x020E4948,0x020E49E4,0x020E4A80,
     0x020E4B1C,0x020E4BB8,0x020E4C54,0x020E4CF0,0x020E4D8C,
     0x020E4E28,0x020E4EC4,0x020E4F60,0x020E4FFC,0x020E5098},
    // EU1.1
    {0x020E4584,0x020E4620,0x020E46BC,0x020E4758,0x020E47F4,
     0x020E4890,0x020E492C,0x020E49C8,0x020E4A64,0x020E4B00,
     0x020E4B9C,0x020E4C38,0x020E4CD4,0x020E4D70,0x020E4E0C,
     0x020E4EA8,0x020E4F44,0x020E4FE0,0x020E507C,0x020E5118},
    // KR1.0
    {0x020DCD44,0x020DCDE0,0x020DCE7C,0x020DCF18,0x020DCFB4,
     0x020DD050,0x020DD0EC,0x020DD188,0x020DD224,0x020DD2C0,
     0x020DD35C,0x020DD3F8,0x020DD494,0x020DD530,0x020DD5CC,
     0x020DD668,0x020DD704,0x020DD7A0,0x020DD83C,0x020DD8D8},
};

// -------------------------------------------------------------------------
//  Static state
// -------------------------------------------------------------------------
static bool s_osdColorPatchApplied = false;
static int  s_osdH211PatchMode     = 0; // 0=none, 1=4-instr shim, 2=10-instr sep color
static bool s_osdConfigDirty       = true;

// -------------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------------
static uint32_t ToBgr555(int r8, int g8, int b8) noexcept
{
    const uint32_t r5 = (static_cast<uint32_t>(r8) >> 3) & 0x1Fu;
    const uint32_t g5 = (static_cast<uint32_t>(g8) >> 3) & 0x1Fu;
    const uint32_t b5 = (static_cast<uint32_t>(b8) >> 3) & 0x1Fu;
    return (b5 << 10) | (g5 << 5) | r5;
}

static uint32_t CatColor(Config::Table& cfg, const char* rk, const char* gk, const char* bk) noexcept
{
    return ToBgr555(cfg.GetInt(rk), cfg.GetInt(gk), cfg.GetInt(bk));
}

// Write one color to every slot unconditionally (used when applyGlobal=true).
static void ApplySlotOverrideAll(melonDS::u8* ram, size_t idx, uint16_t color) noexcept
{
    for (int i = 0; i < OSD_SLOT_COUNT; ++i)
        Write16(ram, kOsdSlotAddr[idx][i], color);
}

// Write per-flag-category colors to active slots (used when applyGlobal=false).
// Reads entry+0x12 (timer) and entry+0x15 (flags) from each slot.
// Flag dispatch:
//   bit4 set (0x10) → Node  (acquiring node / node stolen via H211, flags=0x11)
//   bit1 set (0x02) → KillDeath (HEADSHOT, YOU KILLED, 5-kill, flags=0x02)
//   bit0 set (0x01) → Objective (AMMO DEPLETED, return to base, bounty, octolith, flags=0x01)
//   none set (0x00) → System   (FACE OFF, RETURN TO BATTLE, COWARD DETECTED, turret, flags=0x00)
static void ApplySlotOverridePerFlag(melonDS::u8* ram, size_t idx,
                                     uint16_t colorKillDeath, uint16_t colorNode,
                                     uint16_t colorObjective, uint16_t colorSystem) noexcept
{
    for (int i = 0; i < OSD_SLOT_COUNT; ++i) {
        const uint32_t slotAddr = kOsdSlotAddr[idx][i];
        const uint16_t timer = Read16(ram, slotAddr + 2u); // entry+0x12
        if (timer == 0) continue;                          // inactive slot, skip
        const uint8_t flags = Read8(ram, slotAddr + 5u);   // entry+0x15
        uint16_t color;
        if      (flags & 0x10u) color = colorNode;
        else if (flags & 0x02u) color = colorKillDeath;
        else if (flags & 0x01u) color = colorObjective;
        else                    color = colorSystem;
        Write16(ram, slotAddr, color);
    }
}

// -------------------------------------------------------------------------
//  Public API
// -------------------------------------------------------------------------
void OsdColor_ResetPatchState()
{
    s_osdColorPatchApplied = false;
    s_osdH211PatchMode     = 0;
    s_osdConfigDirty       = true;
}

void OsdColor_InvalidatePatch()
{
    s_osdConfigDirty = true;
}

void OsdColor_RestoreOnce(melonDS::NDS* nds, const RomAddresses& rom)
{
    if (!s_osdColorPatchApplied && s_osdH211PatchMode == 0) {
        s_osdConfigDirty = true;
        return;
    }

    melonDS::u8* ram = nds->MainRAM;
    const size_t idx = rom.romGroupIndex;

    if (s_osdColorPatchApplied) {
        Write32(ram, LIST_OsdLiteral_LostLives[idx],    kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_KillDeath[idx],    kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_ReturnBase[idx],   kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_NoAmmo[idx],       kOsdOrigLitNoAmmo);
        Write32(ram, LIST_OsdLiteral_CowardDetect[idx], kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_AcquiringNode[idx],kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_Turret[idx],       kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_OctoReset[idx],    kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_OctoDrop[idx],     kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_OctoCond[idx],     kOsdOrigLit);
        Write32(ram, LIST_OsdLiteral_OctoMissing[idx],  kOsdOrigLit);
        s_osdColorPatchApplied = false;
    }

    if (s_osdH211PatchMode != 0) {
        const uint32_t shimBase = LIST_OsdH211ShimAddr[idx];
        Write32(ram, shimBase + 0x00u, kH211OrigInstr0);
        Write32(ram, shimBase + 0x04u, kH211OrigInstr1);
        Write32(ram, shimBase + 0x08u, LIST_OsdH211OrigShimInstr2[idx]);
        Write32(ram, shimBase + 0x0Cu, kH211OrigInstr3);
        if (s_osdH211PatchMode == 2) {
            Write32(ram, shimBase + 0x10u, LIST_OsdH211SepOrigInstr4[idx]);
            Write32(ram, shimBase + 0x14u, kH211SepOrigInstr5);
            Write32(ram, shimBase + 0x18u, kH211SepOrigInstr6);
            Write32(ram, shimBase + 0x1Cu, kH211SepOrigInstr7);
            Write32(ram, shimBase + 0x20u, kH211SepOrigInstr8);
            Write32(ram, shimBase + 0x24u, kH211SepOrigInstr9);
        }
        s_osdH211PatchMode = 0;
    }

    s_osdConfigDirty = true; // restored state needs re-apply on next join
}

void OsdColor_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                         const RomAddresses& rom)
{
    if (!s_osdConfigDirty) return;
    s_osdConfigDirty = false;

    const bool globalEnabled = localCfg.GetBool(kCfgOsdColorEnable);
    const bool h211Enabled   = localCfg.GetBool(kCfgOsdColorH211Enable);

    // Restore any previously applied state so mode changes are handled cleanly
    OsdColor_RestoreOnce(emu->getNDS(), rom);
    s_osdConfigDirty = false; // RestoreOnce sets it true; we're about to re-apply

    if (!globalEnabled && !h211Enabled) return;

    melonDS::u8* ram = emu->getNDS()->MainRAM;
    const size_t idx = rom.romGroupIndex;

    if (globalEnabled) {
        const bool applyGlobal = localCfg.GetBool(kCfgOsdColorApplyGlobal);
        const uint32_t globalColor = ToBgr555(
            localCfg.GetInt("Metroid.Visual.OsdColorR"),
            localCfg.GetInt("Metroid.Visual.OsdColorG"),
            localCfg.GetInt("Metroid.Visual.OsdColorB"));
        auto litColor = [&](const char* rk, const char* gk, const char* bk) -> uint32_t {
            return applyGlobal ? globalColor : CatColor(localCfg, rk, gk, bk);
        };

        Write32(ram, LIST_OsdLiteral_LostLives[idx],    litColor("Metroid.Visual.OsdColorLostLivesR",    "Metroid.Visual.OsdColorLostLivesG",    "Metroid.Visual.OsdColorLostLivesB"));
        Write32(ram, LIST_OsdLiteral_KillDeath[idx],    litColor("Metroid.Visual.OsdColorKillDeathR",    "Metroid.Visual.OsdColorKillDeathG",    "Metroid.Visual.OsdColorKillDeathB"));
        Write32(ram, LIST_OsdLiteral_ReturnBase[idx],   litColor("Metroid.Visual.OsdColorReturnBaseR",   "Metroid.Visual.OsdColorReturnBaseG",   "Metroid.Visual.OsdColorReturnBaseB"));
        Write32(ram, LIST_OsdLiteral_NoAmmo[idx],       litColor("Metroid.Visual.OsdColorNoAmmoR",       "Metroid.Visual.OsdColorNoAmmoG",       "Metroid.Visual.OsdColorNoAmmoB"));
        Write32(ram, LIST_OsdLiteral_CowardDetect[idx], litColor("Metroid.Visual.OsdColorCowardDetectR", "Metroid.Visual.OsdColorCowardDetectG", "Metroid.Visual.OsdColorCowardDetectB"));
        Write32(ram, LIST_OsdLiteral_AcquiringNode[idx],litColor("Metroid.Visual.OsdColorAcquiringNodeR","Metroid.Visual.OsdColorAcquiringNodeG","Metroid.Visual.OsdColorAcquiringNodeB"));
        Write32(ram, LIST_OsdLiteral_Turret[idx],       litColor("Metroid.Visual.OsdColorTurretR",       "Metroid.Visual.OsdColorTurretG",       "Metroid.Visual.OsdColorTurretB"));
        Write32(ram, LIST_OsdLiteral_OctoReset[idx],    litColor("Metroid.Visual.OsdColorOctoResetR",    "Metroid.Visual.OsdColorOctoResetG",    "Metroid.Visual.OsdColorOctoResetB"));
        Write32(ram, LIST_OsdLiteral_OctoDrop[idx],     litColor("Metroid.Visual.OsdColorOctoDropR",     "Metroid.Visual.OsdColorOctoDropG",     "Metroid.Visual.OsdColorOctoDropB"));
        Write32(ram, LIST_OsdLiteral_OctoCond[idx],     litColor("Metroid.Visual.OsdColorOctoCondR",     "Metroid.Visual.OsdColorOctoCondG",     "Metroid.Visual.OsdColorOctoCondB"));
        Write32(ram, LIST_OsdLiteral_OctoMissing[idx],  litColor("Metroid.Visual.OsdColorOctoMissingR",  "Metroid.Visual.OsdColorOctoMissingG",  "Metroid.Visual.OsdColorOctoMissingB"));
        s_osdColorPatchApplied = true;

        // Immediately update currently displayed OSD slots.
        // applyGlobal=true  → write globalColor to every slot.
        // applyGlobal=false → dispatch by flags byte (entry+0x15) to per-category colors.
        if (applyGlobal) {
            ApplySlotOverrideAll(ram, idx, static_cast<uint16_t>(globalColor));
        } else {
            const uint16_t colKD  = static_cast<uint16_t>(ToBgr555(
                localCfg.GetInt("Metroid.Visual.OsdColorSlotKillDeathR"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotKillDeathG"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotKillDeathB")));
            const uint16_t colNode = static_cast<uint16_t>(ToBgr555(
                localCfg.GetInt("Metroid.Visual.OsdColorSlotNodeR"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotNodeG"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotNodeB")));
            const uint16_t colObj  = static_cast<uint16_t>(ToBgr555(
                localCfg.GetInt("Metroid.Visual.OsdColorSlotObjectiveR"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotObjectiveG"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotObjectiveB")));
            const uint16_t colSys  = static_cast<uint16_t>(ToBgr555(
                localCfg.GetInt("Metroid.Visual.OsdColorSlotSystemR"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotSystemG"),
                localCfg.GetInt("Metroid.Visual.OsdColorSlotSystemB")));
            ApplySlotOverridePerFlag(ram, idx, colKD, colNode, colObj, colSys);
        }
    }

    if (h211Enabled) {
        // H211 Separate Color (10 instructions): encode BGR555 directly in ARM immediates
        const uint32_t h211Color = ToBgr555(
            localCfg.GetInt(kCfgOsdColorH211R),
            localCfg.GetInt(kCfgOsdColorH211G),
            localCfg.GetInt(kCfgOsdColorH211B));
        const uint32_t ll = h211Color & 0xFFu;
        const uint32_t hh = (h211Color >> 8) & 0xFFu;

        const uint32_t shimBase = LIST_OsdH211ShimAddr[idx];
        Write32(ram, shimBase + 0x00u, 0xE3A02000u | ll);
        Write32(ram, shimBase + 0x04u, 0xE3822C00u | hh);
        Write32(ram, shimBase + 0x08u, 0xE3A0301Fu);
        Write32(ram, shimBase + 0x0Cu, 0xE98D000Cu);
        Write32(ram, shimBase + 0x10u, LIST_OsdH211SepInstr4[idx]);
        Write32(ram, shimBase + 0x14u, LIST_OsdH211SepInstr5[idx]);
        Write32(ram, shimBase + 0x18u, 0xE3A0205Au);
        Write32(ram, shimBase + 0x1Cu, 0xE3A03011u);
        Write32(ram, shimBase + 0x20u, 0xE28DC00Cu);
        Write32(ram, shimBase + 0x24u, 0xE88C000Eu);
        s_osdH211PatchMode = 2;
    } else if (globalEnabled) {
        // H211 shim (4 instructions): redirect H211 to the ReturnBase literal
        const uint32_t shimBase = LIST_OsdH211ShimAddr[idx];
        Write32(ram, shimBase + 0x00u, LIST_OsdH211ShimInstr0[idx]);
        Write32(ram, shimBase + 0x04u, 0xE3A0301Fu);
        Write32(ram, shimBase + 0x08u, 0xE98D000Cu);
        Write32(ram, shimBase + 0x0Cu, LIST_OsdH211ShimInstr3[idx]);
        s_osdH211PatchMode = 1;
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
