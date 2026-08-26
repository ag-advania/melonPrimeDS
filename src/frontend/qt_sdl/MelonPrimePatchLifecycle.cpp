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
                         const RomAddresses& rom,
                         MelonPrimeCore* core,
                         bool active)
{
    ARM9Hook_SetMatchHooksActive(
        nds,
        rom.romGroupIndex,
        core,
        active,
        emu);
}

void ResetPatchAndHookBookkeeping(MelonPrimeCore* core)
{
    Patches_ResetAll(core->PatchState());
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

    SetMatchHooksActive(nds, emu, rom, core, true);
    ApplyRegistryPatches(PatchSite_ConfigReload, nds, emu, cfg, rom, core);
}

void ReconcileAfterSavestateLoad(melonDS::NDS* nds,
                                 EmuInstance* emu,
                                 MelonPrimeCore* core)
{
    // Savestate restores the emulated timeline, not MelonPrime host-owned lifecycle
    // and patch bookkeeping. Post-load reconciliation must rebuild host state from
    // the loaded RAM before the next emulated frame. Do not restore old guest RAM
    // values here and do not advance an extra frame.
    ARM9Hook_Uninstall(nds, core, emu);
    Patches_ResetAll(core->PatchState());

    // LowLatencyAim uses NDS::SetARM9InstructionHook, not an ARM9 RAM opcode patch.
    // ARM9Hook_Uninstall already clears the per-instance address set and JIT
    // trampoline ownership; the next lifecycle edge rebuilds it from the loaded
    // timeline and the Core's resolved activation plan.
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
    SetMatchHooksActive(nds, emu, rom, core, false);
}

void ApplyOnBattleRuntimeEnter(melonDS::NDS* nds,
                               EmuInstance* emu,
                               Config::Table& cfg,
                               const RomAddresses& rom,
                               MelonPrimeCore* core,
                               bool nativeWeaponSwitchEnabled)
{
    ApplyRegistryPatches(PatchSite_BattleRuntime, nds, emu, cfg, rom, core);
    SetMatchHooksActive(nds, emu, rom, core, true);
    if (nativeWeaponSwitchEnabled)
        (void)MelonPrimeCore::WeaponSwitchHook_IsSiteValid(nds, rom.romGroupIndex);
}

void DeactivateHooksOnLeaveInGame(melonDS::NDS* nds,
                                  EmuInstance* emu,
                                  Config::Table& cfg,
                                  const RomAddresses& rom,
                                  MelonPrimeCore* core)
{
    (void)cfg;
    SetMatchHooksActive(nds, emu, rom, core, false);
}

void DeactivateHooksForRomDetect(melonDS::NDS* nds,
                                 EmuInstance* emu,
                                 Config::Table& cfg,
                                 const RomAddresses& rom,
                                 MelonPrimeCore* core)
{
    (void)cfg;
    SetMatchHooksActive(nds, emu, rom, core, false);
    // A ROM can be opened/re-detected without a full MelonPrimeCore restart.
    // Drop all patch bookkeeping so a same-region ROM cannot inherit the
    // previous ROM's "already applied" cache.
    Patches_ResetAll(core->PatchState());
}

} // namespace MelonPrime::PatchLifecycle
