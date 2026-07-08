#include "MelonPrimePatchLifecycle.h"

#include "EmuInstance.h"
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchRegistry.h"

namespace MelonPrime::PatchLifecycle {

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom)
{
    (void)emu;
    ARM9Hook_Uninstall(nds, emu);
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_RestoreOnStop(ctx);
    Patches_ResetAll();
    ARM9Hook_ResetPatchState();
}

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu)
{
    ARM9Hook_Uninstall(nds, emu);
    // boot reset: state only, no RAM restore (emu memory is being re-initialized)
    Patches_ResetAll();
    ARM9Hook_ResetPatchState();
}

void RestoreForEmuStop(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       bool romDetected)
{
    (void)romDetected;
    ARM9Hook_Uninstall(nds, emu);
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_RestoreOnStop(ctx);
    Patches_ResetAll();
    ARM9Hook_ResetPatchState();
}

} // namespace MelonPrime::PatchLifecycle
