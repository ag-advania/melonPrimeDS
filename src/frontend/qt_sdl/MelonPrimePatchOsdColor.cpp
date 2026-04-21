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
//  Patches the BGR555 color literals used by MPH's OSD message system.
//  Each of the 11 OSD message categories has an independent color setting.
//
//  s_osdConfigDirty: set on settings save or game leave/reset.
//  OsdColor_ApplyOnce: no-op when clean; when dirty: restore previous state
//  then re-apply from current config. Called every in-game frame (cheap
//  no-op path: single bool check).
//
//  H211 "node stolen" is a separate section: it originally uses a hardcoded
//  ARM immediate rather than a patchable literal, so it gets its own 10-
//  instruction color-encoding shim, independently toggled and defaulting red.
// =========================================================================

static constexpr const char* kCfgOsdColorEnable      = "Metroid.Visual.OsdColor";
static constexpr const char* kCfgOsdColorApplyGlobal  = "Metroid.Visual.OsdColorApplyGlobal";
static constexpr const char* kCfgOsdColorH211Enable   = "Metroid.Visual.OsdColorH211";
static constexpr const char* kCfgOsdColorH211R        = "Metroid.Visual.OsdColorH211R";
static constexpr const char* kCfgOsdColorH211G        = "Metroid.Visual.OsdColorH211G";
static constexpr const char* kCfgOsdColorH211B        = "Metroid.Visual.OsdColorH211B";

static bool s_osdColorPatchApplied = false;
static int  s_osdH211PatchMode     = 0; // 0=none, 1=4-instr shim, 2=10-instr sep color
static bool s_osdConfigDirty       = true; // true = needs (re-)apply

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
        uint32_t globalColor = 0;
        if (applyGlobal) {
            globalColor = ToBgr555(
                localCfg.GetInt("Metroid.Visual.OsdColorR"),
                localCfg.GetInt("Metroid.Visual.OsdColorG"),
                localCfg.GetInt("Metroid.Visual.OsdColorB"));
        }
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
