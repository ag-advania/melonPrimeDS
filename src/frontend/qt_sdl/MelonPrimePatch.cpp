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
// Mode: 0=5:3(3DS), 1=16:10(3DS), 2=16:9, 3=21:9

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

void InGameScaling_Tick(EmuInstance* emu, Config::Table& localCfg,
                        const RomAddresses& rom, bool isInGame)
{
    if (!isInGame) return;

    bool enabled = localCfg.GetBool(kCfgScalingEnabled);
    if (!enabled) {
        if (s_scalePatchApplied) {
            RestoreScalingPatch(emu->getNDS(), rom);
        }
        return;
    }

    int mode = localCfg.GetInt(kCfgScalingMode);
    // Write every frame (game may reset the 16-bit value)
    ApplyScalingPatch(emu->getNDS(), rom, mode);
}

} // namespace MelonPrime

#endif // MELONPRIME_INGAME_SCALING
