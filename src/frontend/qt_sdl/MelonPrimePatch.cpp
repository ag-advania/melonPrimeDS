#ifdef MELONPRIME_INGAME_SCALING

#include "MelonPrimePatch.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "EmuInstance.h"
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
static constexpr const char* kCfgScalingEnabled = "Metroid.Visual.InGameScaling";
static constexpr const char* kCfgScalingMode    = "Metroid.Visual.InGameScalingMode";
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
    melonDS::u8* ram = nds->MainRAM();

    // ARM instruction patches (32-bit writes)
    uint32_t instr = kScalePatchInstr[mode];
    Write32(ram, rom.scalePatchAddr1, instr);
    Write32(ram, rom.scalePatchAddr2, instr);

    // Aspect ratio value (16-bit write, game may reset this each frame)
    Write16(ram, rom.scaleValueAddr, kScalePatchVal[mode]);

    s_scalePatchApplied = true;
}

static void RestoreScalingPatch(melonDS::NDS* nds, const RomAddresses& rom)
{
    if (!s_scalePatchApplied) return;
    melonDS::u8* ram = nds->MainRAM();

    Write32(ram, rom.scalePatchAddr1, kScaleOrig1);
    Write32(ram, rom.scalePatchAddr2, kScaleOrig2);
    Write16(ram, rom.scaleValueAddr, kScaleOrigVal);

    s_scalePatchApplied = false;
}

void InGameScaling_ResetPatchState()
{
    s_scalePatchApplied = false;
}

// Map Screen.h aspectRatio id → scaling patch mode.
// Returns -1 if no patch should be applied (4:3 native or unknown).
static int AspectIdToScaleMode(int aspectId)
{
    switch (aspectId) {
    case 4: return 0;  // 5:3 (3DS)
    case 1: return 2;  // 16:9
    case 2: return 3;  // 21:9
    default: return -1; // 4:3 (id=0), window (id=3), or unknown
    }
}

void InGameScaling_Tick(EmuInstance* emu, Config::Table& localCfg,
                        const RomAddresses& rom, bool isInGame,
                        int screenAspectId)
{
    if (!isInGame) return;

    bool enabled = localCfg.GetBool(kCfgScalingEnabled);
    if (!enabled) {
        if (s_scalePatchApplied)
            RestoreScalingPatch(emu->getNDS(), rom);
        return;
    }

    int comboMode = localCfg.GetInt(kCfgScalingMode);
    // Combo: 0=Auto, 1=5:3, 2=16:10, 3=16:9, 4=21:9

    int patchMode;
    if (comboMode == 0) {
        // Auto: derive from current Aspect Ratio setting
        patchMode = AspectIdToScaleMode(screenAspectId);
        if (patchMode < 0) {
            // 4:3 or window → no patch needed
            if (s_scalePatchApplied)
                RestoreScalingPatch(emu->getNDS(), rom);
            return;
        }
    } else {
        // Manual: combo index 1-4 → patch mode 0-3
        patchMode = comboMode - 1;
    }

    ApplyScalingPatch(emu->getNDS(), rom, patchMode);
}

} // namespace MelonPrime

#endif // MELONPRIME_INGAME_SCALING
