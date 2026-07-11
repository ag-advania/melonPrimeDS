#include "MelonPrimePatchLifecycle.h"

#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchRegistry.h"
#include "MelonPrimePatchOsdColor.h"
#include "MelonPrimePatchExpandStageMatrix.h"

namespace MelonPrime::PatchLifecycle {

namespace {

void SetMatchHooksActive(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom,
                         MelonPrimeCore* core,
                         bool active)
{
    ARM9Hook_SetMatchHooksActive(
        nds,
        cfg,
        rom.romGroupIndex,
        core,
        active,
        emu);
}

void ResetPatchAndHookBookkeeping(MelonPrimeCore* core)
{
    Patches_ResetAll(core->PatchState());
    ARM9Hook_ResetPatchState();
}

void RestoreStopPatches(melonDS::NDS* nds,
                        EmuInstance* emu,
                        Config::Table& cfg,
                        const RomAddresses& rom,
                        MelonPrimeCore* core)
{
    const PatchCtx ctx{ nds, emu, cfg, rom, core->PatchState() };
    Patches_RestoreOnStop(ctx);
}

void ApplyRegistryPatches(uint8_t siteMask,
                          melonDS::NDS* nds,
                          EmuInstance* emu,
                          Config::Table& cfg,
                          const RomAddresses& rom,
                          MelonPrimeCore* core)
{
    const PatchCtx ctx{ nds, emu, cfg, rom, core->PatchState() };
    Patches_Apply(siteMask, ctx);
}

void RestoreLeavePatches(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom,
                         MelonPrimeCore* core)
{
    const PatchCtx ctx{ nds, emu, cfg, rom, core->PatchState() };
    Patches_RestoreOnLeave(ctx);
}

} // namespace

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom,
                      MelonPrimeCore* core)
{
    ARM9Hook_Uninstall(nds, core, emu);
    RestoreStopPatches(nds, emu, cfg, rom, core);
    ResetPatchAndHookBookkeeping(core);
}

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu,
                  MelonPrimeCore* core)
{
    ARM9Hook_Uninstall(nds, core, emu);
    // boot reset: state only, no RAM restore (emu memory is being re-initialized)
    ResetPatchAndHookBookkeeping(core);
}

void RestoreForEmuStop(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       MelonPrimeCore* core)
{
    // Historical OnEmuStop behavior: DS patch restore runs unconditionally,
    // regardless of whether a ROM was ever detected this session.
    ARM9Hook_Uninstall(nds, core, emu);
    RestoreStopPatches(nds, emu, cfg, rom, core);
    ResetPatchAndHookBookkeeping(core);
}

void ReapplyForConfigReload(melonDS::NDS* nds,
                            EmuInstance* emu,
                            Config::Table& cfg,
                            const RomAddresses& rom,
                            MelonPrimeCore* core,
                            bool romDetected,
                            bool battleRuntimeMode)
{
    OsdColor_InvalidatePatch(core->PatchState());
    ExpandStageMatrix_InvalidatePatch(core->PatchState());
    if (!romDetected || !battleRuntimeMode)
        return;

    SetMatchHooksActive(nds, emu, cfg, rom, core, true);
    ApplyRegistryPatches(PatchSite_ConfigReload, nds, emu, cfg, rom, core);
}

void ApplyOutOfGameFrame(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom,
                         MelonPrimeCore* core)
{
    ApplyRegistryPatches(PatchSite_OutOfGameFrame, nds, emu, cfg, rom, core);
}

void RestoreOnMatchEnd(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       MelonPrimeCore* core)
{
    RestoreLeavePatches(nds, emu, cfg, rom, core);
    SetMatchHooksActive(nds, emu, cfg, rom, core, false);
}

void ApplyOnBattleRuntimeEnter(melonDS::NDS* nds,
                               EmuInstance* emu,
                               Config::Table& cfg,
                               const RomAddresses& rom,
                               MelonPrimeCore* core,
                               bool nativeWeaponSwitchEnabled)
{
    ApplyRegistryPatches(PatchSite_BattleRuntime, nds, emu, cfg, rom, core);
    SetMatchHooksActive(nds, emu, cfg, rom, core, true);
    if (nativeWeaponSwitchEnabled)
        (void)MelonPrimeCore::WeaponSwitchHook_IsSiteValid(nds, rom.romGroupIndex);
}

void DeactivateHooksOnLeaveInGame(melonDS::NDS* nds,
                                  EmuInstance* emu,
                                  Config::Table& cfg,
                                  const RomAddresses& rom,
                                  MelonPrimeCore* core)
{
    SetMatchHooksActive(nds, emu, cfg, rom, core, false);
}

void DeactivateHooksForRomDetect(melonDS::NDS* nds,
                                 EmuInstance* emu,
                                 Config::Table& cfg,
                                 const RomAddresses& rom,
                                 MelonPrimeCore* core)
{
    SetMatchHooksActive(nds, emu, cfg, rom, core, false);
}

} // namespace MelonPrime::PatchLifecycle
