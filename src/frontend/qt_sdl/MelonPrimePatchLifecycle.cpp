#include "MelonPrimePatchLifecycle.h"

#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchRegistry.h"

namespace MelonPrime::PatchLifecycle {

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom)
{
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
    // Historical OnEmuStop behavior: DS patch restore runs unconditionally.
    // romDetected is reserved for a future guard; do not gate restore here yet.
    (void)romDetected;
    ARM9Hook_Uninstall(nds, emu);
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_RestoreOnStop(ctx);
    Patches_ResetAll();
    ARM9Hook_ResetPatchState();
}

void ReapplyForConfigReload(melonDS::NDS* nds,
                            EmuInstance* emu,
                            Config::Table& cfg,
                            const RomAddresses& rom,
                            MelonPrimeCore* core,
                            bool romDetected,
                            bool battleRuntimeMode)
{
    if (!romDetected || !battleRuntimeMode)
        return;

    ARM9Hook_SetMatchHooksActive(
        nds,
        cfg,
        rom.romGroupIndex,
        core,
        true,
        emu);
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_Apply(PatchSite_ConfigReload, ctx);
}

void ApplyOutOfGameFrame(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom)
{
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_Apply(PatchSite_OutOfGameFrame, ctx);
}

void RestoreOnMatchEnd(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       MelonPrimeCore* core)
{
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_RestoreOnLeave(ctx);
    ARM9Hook_SetMatchHooksActive(
        nds,
        cfg,
        rom.romGroupIndex,
        core,
        false,
        emu);
}

} // namespace MelonPrime::PatchLifecycle
