#ifdef MELONPRIME_DS

#include "MelonPrimePatch.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "EmuInstance.h"
#include "Window.h"
#include "NDS.h"

namespace MelonPrime {

// =========================================================================
//  In-game aspect ratio scaling patch
//
//  Patches the game's 3D projection matrix setup to use a wider FOV,
//  matching the emulator's widescreen aspect ratio setting.
//  Derived from Action Replay cheat codes for MPH widescreen hacks.
// =========================================================================

// Config keys
static constexpr const char* kCfgAspectRatioEnabled = "Metroid.Visual.InGameAspectRatio";
static constexpr const char* kCfgAspectRatioMode    = "Metroid.Visual.InGameAspectRatioMode";
// Combo: 0=Auto, 1=5:3(3DS), 2=16:10(3DS), 3=16:9, 4=21:9

// Original ARM instructions (restored when patch is disabled)
static constexpr uint32_t kScaleOrig1  = 0xE5991664; // LDR r1,[r9,#0x664]
static constexpr uint32_t kScaleOrig2  = 0xE59A1664; // LDR r1,[r10,#0x664]
static constexpr uint16_t kScaleOrigVal = 0x1555;     // 4:3 native aspect value

// Patch instructions per mode: MOV r1, #imm
static constexpr uint32_t kScalePatchInstr[4] = {
    0xE3A01099,  // 5:3 (3DS)
    0xE3A0109F,  // 16:10 (3DS)
    0xE3A0108F,  // 16:9
    0xE3A0106D,  // 21:9
};

// 16-bit aspect ratio values per mode
static constexpr uint16_t kScalePatchVal[4] = {
    0x1AAB,  // 5:3
    0x199A,  // 16:10
    0x1C72,  // 16:9
    0x2555,  // 21:9
};

static bool s_scalePatchApplied = false;

static void ApplyScalingPatch(melonDS::NDS* nds, const RomAddresses& rom, int mode)
{
    if (mode < 0 || mode > 3) return;
    melonDS::u8* ram = nds->MainRAM;

    // AR-style conditional write: only patch if current value matches original
    // (5xxxxxxx = "if 32-bit value at addr == expected, execute next line")
    if (Read32(ram, rom.scalePatchAddr1) == kScaleOrig1)
        Write32(ram, rom.scalePatchAddr1, kScalePatchInstr[mode]);

    if (Read32(ram, rom.scalePatchAddr2) == kScaleOrig2)
        Write32(ram, rom.scalePatchAddr2, kScalePatchInstr[mode]);

    // (9xxxxxxx = "if 16-bit value at addr == expected, execute next line")
    if (Read16(ram, rom.scaleValueAddr) == kScaleOrigVal)
        Write16(ram, rom.scaleValueAddr, kScalePatchVal[mode]);

    s_scalePatchApplied = true;
}

static void RestoreScalingPatch(melonDS::NDS* nds, const RomAddresses& rom)
{
    if (!s_scalePatchApplied) return;
    melonDS::u8* ram = nds->MainRAM;

    Write32(ram, rom.scalePatchAddr1, kScaleOrig1);
    Write32(ram, rom.scalePatchAddr2, kScaleOrig2);
    Write16(ram, rom.scaleValueAddr, kScaleOrigVal);

    s_scalePatchApplied = false;
}

void InGameAspectRatio_ResetPatchState()
{
    s_scalePatchApplied = false;
}

// Map Screen.h aspectRatio id → scaling patch mode.
// Returns -1 if no patch needed (4:3 native, window, or unknown).
static int AspectIdToScaleMode(int aspectId)
{
    switch (aspectId) {
    case 4: return 0;  // 5:3 (3DS)
    case 1: return 2;  // 16:9
    case 2: return 3;  // 21:9
    default: return -1;
    }
}

void InGameAspectRatio_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                                  const RomAddresses& rom)
{
    bool enabled = localCfg.GetBool(kCfgAspectRatioEnabled);
    if (!enabled) return;

    int comboMode = localCfg.GetInt(kCfgAspectRatioMode);
    // Combo: 0=Auto, 1=5:3, 2=16:10, 3=16:9, 4=21:9

    int patchMode;
    if (comboMode == 0) {
        // Auto: read current aspect ratio from window config
        auto* mw = emu->getMainWindow();
        if (!mw) return;
        int aspectId = mw->getWindowConfig().GetInt("ScreenAspectTop");
        patchMode = AspectIdToScaleMode(aspectId);
        if (patchMode < 0) return; // 4:3 or window → no patch
    } else {
        patchMode = comboMode - 1;
    }

    ApplyScalingPatch(emu->getNDS(), rom, patchMode);
}

// =========================================================================
//  OSD color patch
//
//  Patches the BGR555 color literals used by MPH's OSD message system.
//  Each of the 11 OSD message categories has an independent color setting.
//  The global R/G/B acts as a convenience "set all" reference in the UI.
//
//  H211 "node stolen" is a separate section: it originally uses a hardcoded
//  ARM immediate rather than a patchable literal, so it gets its own 10-
//  instruction color-encoding shim, independently toggled and defaulting red.
// =========================================================================

static constexpr const char* kCfgOsdColorEnable     = "Metroid.Visual.OsdColor";
static constexpr const char* kCfgOsdColorH211Enable  = "Metroid.Visual.OsdColorH211";
static constexpr const char* kCfgOsdColorH211R       = "Metroid.Visual.OsdColorH211R";
static constexpr const char* kCfgOsdColorH211G       = "Metroid.Visual.OsdColorH211G";
static constexpr const char* kCfgOsdColorH211B       = "Metroid.Visual.OsdColorH211B";

static bool s_osdColorPatchApplied = false;
// 0=none, 1=4-instr shim (H211 follows ReturnBase), 2=10-instr separate color
static int  s_osdH211PatchMode     = 0;

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
}

void OsdColor_RestoreOnce(melonDS::NDS* nds, const RomAddresses& rom)
{
    if (!s_osdColorPatchApplied && s_osdH211PatchMode == 0) return;

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
        // First 4 instructions are identical for both shim and separate-color revert
        Write32(ram, shimBase + 0x00u, kH211OrigInstr0);
        Write32(ram, shimBase + 0x04u, kH211OrigInstr1);
        Write32(ram, shimBase + 0x08u, LIST_OsdH211OrigShimInstr2[idx]);
        Write32(ram, shimBase + 0x0Cu, kH211OrigInstr3);
        if (s_osdH211PatchMode == 2) {
            // Restore additional 6 instructions written by the separate-color shim
            Write32(ram, shimBase + 0x10u, LIST_OsdH211SepOrigInstr4[idx]);
            Write32(ram, shimBase + 0x14u, kH211SepOrigInstr5);
            Write32(ram, shimBase + 0x18u, kH211SepOrigInstr6);
            Write32(ram, shimBase + 0x1Cu, kH211SepOrigInstr7);
            Write32(ram, shimBase + 0x20u, kH211SepOrigInstr8);
            Write32(ram, shimBase + 0x24u, kH211SepOrigInstr9);
        }
        s_osdH211PatchMode = 0;
    }
}

void OsdColor_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                         const RomAddresses& rom)
{
    if (s_osdColorPatchApplied && s_osdH211PatchMode != 0) return;

    const bool globalEnabled = localCfg.GetBool(kCfgOsdColorEnable);
    const bool h211Enabled   = localCfg.GetBool(kCfgOsdColorH211Enable);

    if (!globalEnabled && !h211Enabled) return;

    melonDS::u8* ram = emu->getNDS()->MainRAM;
    const size_t idx = rom.romGroupIndex;

    if (globalEnabled && !s_osdColorPatchApplied) {
        // Write per-category BGR555 colors to all 11 OSD literal pool addresses.
        // Each category reads its own R/G/B from config independently.
        Write32(ram, LIST_OsdLiteral_LostLives[idx],    CatColor(localCfg, "Metroid.Visual.OsdColorLostLivesR",    "Metroid.Visual.OsdColorLostLivesG",    "Metroid.Visual.OsdColorLostLivesB"));
        Write32(ram, LIST_OsdLiteral_KillDeath[idx],    CatColor(localCfg, "Metroid.Visual.OsdColorKillDeathR",    "Metroid.Visual.OsdColorKillDeathG",    "Metroid.Visual.OsdColorKillDeathB"));
        Write32(ram, LIST_OsdLiteral_ReturnBase[idx],   CatColor(localCfg, "Metroid.Visual.OsdColorReturnBaseR",   "Metroid.Visual.OsdColorReturnBaseG",   "Metroid.Visual.OsdColorReturnBaseB"));
        Write32(ram, LIST_OsdLiteral_NoAmmo[idx],       CatColor(localCfg, "Metroid.Visual.OsdColorNoAmmoR",       "Metroid.Visual.OsdColorNoAmmoG",       "Metroid.Visual.OsdColorNoAmmoB"));
        Write32(ram, LIST_OsdLiteral_CowardDetect[idx], CatColor(localCfg, "Metroid.Visual.OsdColorCowardDetectR", "Metroid.Visual.OsdColorCowardDetectG", "Metroid.Visual.OsdColorCowardDetectB"));
        Write32(ram, LIST_OsdLiteral_AcquiringNode[idx],CatColor(localCfg, "Metroid.Visual.OsdColorAcquiringNodeR","Metroid.Visual.OsdColorAcquiringNodeG","Metroid.Visual.OsdColorAcquiringNodeB"));
        Write32(ram, LIST_OsdLiteral_Turret[idx],       CatColor(localCfg, "Metroid.Visual.OsdColorTurretR",       "Metroid.Visual.OsdColorTurretG",       "Metroid.Visual.OsdColorTurretB"));
        Write32(ram, LIST_OsdLiteral_OctoReset[idx],    CatColor(localCfg, "Metroid.Visual.OsdColorOctoResetR",    "Metroid.Visual.OsdColorOctoResetG",    "Metroid.Visual.OsdColorOctoResetB"));
        Write32(ram, LIST_OsdLiteral_OctoDrop[idx],     CatColor(localCfg, "Metroid.Visual.OsdColorOctoDropR",     "Metroid.Visual.OsdColorOctoDropG",     "Metroid.Visual.OsdColorOctoDropB"));
        Write32(ram, LIST_OsdLiteral_OctoCond[idx],     CatColor(localCfg, "Metroid.Visual.OsdColorOctoCondR",     "Metroid.Visual.OsdColorOctoCondG",     "Metroid.Visual.OsdColorOctoCondB"));
        Write32(ram, LIST_OsdLiteral_OctoMissing[idx],  CatColor(localCfg, "Metroid.Visual.OsdColorOctoMissingR",  "Metroid.Visual.OsdColorOctoMissingG",  "Metroid.Visual.OsdColorOctoMissingB"));
        s_osdColorPatchApplied = true;
    }

    if (s_osdH211PatchMode == 0) {
        if (h211Enabled) {
            // H211 Separate Color (10 instructions): encode the BGR555 color directly
            // in ARM mov/orr immediates so H211 uses its own independent color.
            const uint32_t h211Color = ToBgr555(
                localCfg.GetInt(kCfgOsdColorH211R),
                localCfg.GetInt(kCfgOsdColorH211G),
                localCfg.GetInt(kCfgOsdColorH211B));
            const uint32_t ll = h211Color & 0xFFu;
            const uint32_t hh = (h211Color >> 8) & 0xFFu;

            const uint32_t shimBase = LIST_OsdH211ShimAddr[idx];
            Write32(ram, shimBase + 0x00u, 0xE3A02000u | ll);           // mov r2,#ll
            Write32(ram, shimBase + 0x04u, 0xE3822C00u | hh);           // orr r2,r2,#hh<<8
            Write32(ram, shimBase + 0x08u, 0xE3A0301Fu);                // mov r3,#0x1F (alpha)
            Write32(ram, shimBase + 0x0Cu, 0xE98D000Cu);                // stmib sp,{r2,r3}
            Write32(ram, shimBase + 0x10u, LIST_OsdH211SepInstr4[idx]); // ldr r1,[pc,#offset] -> font ptr holder
            Write32(ram, shimBase + 0x14u, LIST_OsdH211SepInstr5[idx]); // ldr r1,[r1,...]
            Write32(ram, shimBase + 0x18u, 0xE3A0205Au);                // mov r2,#0x5A (timer)
            Write32(ram, shimBase + 0x1Cu, 0xE3A03011u);                // mov r3,#0x11 (flags)
            Write32(ram, shimBase + 0x20u, 0xE28DC00Cu);                // add r12,sp,#0xC
            Write32(ram, shimBase + 0x24u, 0xE88C000Eu);                // stm r12,{r1,r2,r3}
            s_osdH211PatchMode = 2;
        } else if (globalEnabled) {
            // H211 shim (4 instructions): redirect H211's immediate load to the
            // ReturnBase literal, which is now set to the ReturnBase category color.
            const uint32_t shimBase = LIST_OsdH211ShimAddr[idx];
            Write32(ram, shimBase + 0x00u, LIST_OsdH211ShimInstr0[idx]); // ldr r2,[pc,#offset] -> ReturnBase
            Write32(ram, shimBase + 0x04u, 0xE3A0301Fu);                 // mov r3,#0x1F (alpha)
            Write32(ram, shimBase + 0x08u, 0xE98D000Cu);                 // stmib sp,{r2,r3}
            Write32(ram, shimBase + 0x0Cu, LIST_OsdH211ShimInstr3[idx]); // ldr r1,[pc,#offset] -> font ptr
            s_osdH211PatchMode = 1;
        }
    }
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
