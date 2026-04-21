#ifdef MELONPRIME_DS

#include "MelonPrimePatchAspectRatio.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "EmuInstance.h"
#include "Window.h"
#include "NDS.h"

namespace MelonPrime {

static constexpr const char* kCfgAspectRatioEnabled = "Metroid.Visual.InGameAspectRatio";
static constexpr const char* kCfgAspectRatioMode    = "Metroid.Visual.InGameAspectRatioMode";

static constexpr uint32_t kScaleOrig1  = 0xE5991664;
static constexpr uint32_t kScaleOrig2  = 0xE59A1664;
static constexpr uint16_t kScaleOrigVal = 0x1555;

static constexpr uint32_t kScalePatchInstr[4] = {
    0xE3A01099, 0xE3A0109F, 0xE3A0108F, 0xE3A0106D,
};
static constexpr uint16_t kScalePatchVal[4] = {
    0x1AAB, 0x199A, 0x1C72, 0x2555,
};

static bool s_scalePatchApplied = false;

static void ApplyScalingPatch(melonDS::NDS* nds, const RomAddresses& rom, int mode)
{
    if (mode < 0 || mode > 3) return;
    melonDS::u8* ram = nds->MainRAM;
    if (Read32(ram, rom.scalePatchAddr1) == kScaleOrig1)
        Write32(ram, rom.scalePatchAddr1, kScalePatchInstr[mode]);
    if (Read32(ram, rom.scalePatchAddr2) == kScaleOrig2)
        Write32(ram, rom.scalePatchAddr2, kScalePatchInstr[mode]);
    if (Read16(ram, rom.scaleValueAddr) == kScaleOrigVal)
        Write16(ram, rom.scaleValueAddr, kScalePatchVal[mode]);
    s_scalePatchApplied = true;
}

void InGameAspectRatio_ResetPatchState()
{
    s_scalePatchApplied = false;
}

static int AspectIdToScaleMode(int aspectId)
{
    switch (aspectId) {
    case 4: return 0;
    case 1: return 2;
    case 2: return 3;
    default: return -1;
    }
}

void InGameAspectRatio_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                                  const RomAddresses& rom)
{
    bool enabled = localCfg.GetBool(kCfgAspectRatioEnabled);
    if (!enabled) return;

    int comboMode = localCfg.GetInt(kCfgAspectRatioMode);
    int patchMode;
    if (comboMode == 0) {
        auto* mw = emu->getMainWindow();
        if (!mw) return;
        int aspectId = mw->getWindowConfig().GetInt("ScreenAspectTop");
        patchMode = AspectIdToScaleMode(aspectId);
        if (patchMode < 0) return;
    } else {
        patchMode = comboMode - 1;
    }
    ApplyScalingPatch(emu->getNDS(), rom, patchMode);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
