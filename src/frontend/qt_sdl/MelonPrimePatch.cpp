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

} // namespace MelonPrime

#endif // MELONPRIME_DS
