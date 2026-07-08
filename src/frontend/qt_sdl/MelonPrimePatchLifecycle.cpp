#include "MelonPrimePatchLifecycle.h"

#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchRegistry.h"

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

void ResetPatchAndHookBookkeeping()
{
    Patches_ResetAll();
    ARM9Hook_ResetPatchState();
}

void RestoreStopPatches(melonDS::NDS* nds,
                        EmuInstance* emu,
                        Config::Table& cfg,
                        const RomAddresses& rom)
{
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_RestoreOnStop(ctx);
}

} // namespace

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom)
{
    ARM9Hook_Uninstall(nds, emu);
    RestoreStopPatches(nds, emu, cfg, rom);
    ResetPatchAndHookBookkeeping();
}

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu)
{
    ARM9Hook_Uninstall(nds, emu);
    // boot reset: state only, no RAM restore (emu memory is being re-initialized)
    ResetPatchAndHookBookkeeping();
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
    RestoreStopPatches(nds, emu, cfg, rom);
    ResetPatchAndHookBookkeeping();
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

    SetMatchHooksActive(nds, emu, cfg, rom, core, true);
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
    SetMatchHooksActive(nds, emu, cfg, rom, core, false);
}

void ApplyOnBattleRuntimeEnter(melonDS::NDS* nds,
                               EmuInstance* emu,
                               Config::Table& cfg,
                               const RomAddresses& rom,
                               MelonPrimeCore* core,
                               bool nativeWeaponSwitchEnabled)
{
    const PatchCtx ctx{ nds, emu, cfg, rom };
    Patches_Apply(PatchSite_BattleRuntime, ctx);
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
