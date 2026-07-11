#ifdef MELONPRIME_DS

#include "MelonPrimePatchAspectRatio.h"
#include "MelonPrimePatchState.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeHudPropSchema.inc"
#include "EmuInstance.h"
#include "Window.h"
#include "NDS.h"

namespace MelonPrime {

static constexpr const char* kCfgAspectRatioEnabled = MP_HUD_PROP_KEY_InGameAspectRatio;
static constexpr const char* kCfgAspectRatioMode    = MP_HUD_PROP_KEY_InGameAspectRatioMode;

static constexpr uint32_t kScaleOrig1  = 0xE5991664;
static constexpr uint32_t kScaleOrig2  = 0xE59A1664;
static constexpr uint16_t kScaleOrigVal = 0x1555;

static constexpr uint32_t kScalePatchInstr[4] = {
    0xE3A01099, 0xE3A0109F, 0xE3A0108F, 0xE3A0106D,
};
static constexpr uint16_t kScalePatchVal[4] = {
    0x1AAB, 0x199A, 0x1C72, 0x2555,
};

static void ApplyScalingPatch(MelonPrimePatchState& state, melonDS::NDS* nds, const RomAddresses& rom, int mode)
{
    if (mode < 0 || mode > 3) return;
    melonDS::u8* ram = nds->MainRAM;
    if (Read32(ram, rom.scalePatchAddr1) == kScaleOrig1)
        Write32(ram, rom.scalePatchAddr1, kScalePatchInstr[mode]);
    if (Read32(ram, rom.scalePatchAddr2) == kScaleOrig2)
        Write32(ram, rom.scalePatchAddr2, kScalePatchInstr[mode]);
    if (Read16(ram, rom.scaleValueAddr) == kScaleOrigVal)
        Write16(ram, rom.scaleValueAddr, kScalePatchVal[mode]);
    state.aspectRatioApplied = true;
}

void InGameAspectRatio_ResetPatchState(MelonPrimePatchState& state)
{
    state.aspectRatioApplied = false;
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

void InGameAspectRatio_ApplyOnce(MelonPrimePatchState& state,
                                  EmuInstance* emu, Config::Table& localCfg,
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
    ApplyScalingPatch(state, emu->getNDS(), rom, patchMode);
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
