#pragma once

namespace melonDS {
class NDS;
}

class EmuInstance;

namespace Config {
class Table;
}

namespace MelonPrime {

struct RomAddresses;
class MelonPrimeCore;

namespace PatchLifecycle {

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom,
                      MelonPrimeCore* core);

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu,
                  MelonPrimeCore* core);

void RestoreForEmuStop(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       MelonPrimeCore* core);

void ReapplyForConfigReload(melonDS::NDS* nds,
                            EmuInstance* emu,
                            Config::Table& cfg,
                            const RomAddresses& rom,
                            MelonPrimeCore* core,
                            bool romDetected,
                            bool battleRuntimeMode);

// Savestate replaces the guest timeline without serializing MelonPrime's
// patch registry or ARM9 hook bookkeeping. Invalidate those host-owned states
// after a successful load; the next normal RunFrameHook rebuilds lifecycle,
// patches, and active hooks from loaded RAM without advancing an extra frame.
void ReconcileAfterSavestateLoad(melonDS::NDS* nds,
                                 EmuInstance* emu,
                                 MelonPrimeCore* core);

// Step 3 / Site E (see melonprime_patch_lifecycle_gateway_step3_plan.md).
// Called every out-of-game focused frame from RunFrameHook. Registry
// entries at PatchSite_OutOfGameFrame (FixWifi / UseFirmwareLanguage /
// ExpandStageMatrix) self-guard. FixWifi caches its one-time signature check
// and ROM-detect reset, so this remains a cheap cold-path check rather than a
// per-frame patch scan/write — do not add further gating around this call.
void ApplyOutOfGameFrame(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom,
                         MelonPrimeCore* core);

// Step 3 / Site A (see melonprime_patch_lifecycle_gateway_step3_plan.md).
// Called once, on the match-end poll transition (flowState leaving
// FLOW_ACTIVE_MATCH while BIT_BATTLE_RUNTIME_MODE is set). Restores the
// battle-runtime static patches and deactivates match ARM9 hooks. The
// caller still owns setting StateFlags::BIT_END_OF_GAME_PATCH_RESTORED —
// that is frame-state ownership, not patch-lifecycle ownership, and must
// stay in RunFrameHook.
void RestoreOnMatchEnd(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       MelonPrimeCore* core);

// Step 3 / Site B (see melonprime_patch_lifecycle_gateway_step3_plan.md).
// Called once from HandleBattleRuntimeEnter() on the first
// frame after join on which the local player is in play (HP != 0), having
// already seen mode==MODE_BATTLE_RUNTIME && flow==FLOW_ACTIVE_MATCH at least
// once. The two stages need not fall on the same frame.
// Applies battle-runtime static patches, activates match ARM9 hooks, and
// validates/installs the guest trampoline of every enabled native path
// (weapon switch Method 1, DirectInvocation Method 2/3) so the cave is
// already written before the first request can be dispatched. The caller
// still owns setting
// StateFlags::BIT_BATTLE_RUNTIME_MODE and keeping HandleBattleRuntimeEnter
// as a single cold outlined function — do not inline it back into
// RunFrameHook.
void ApplyOnBattleRuntimeEnter(melonDS::NDS* nds,
                               EmuInstance* emu,
                               Config::Table& cfg,
                               const RomAddresses& rom,
                               MelonPrimeCore* core,
                               bool nativeWeaponSwitchEnabled,
                               bool directInvocationEnabled);

// Step 3 / Site D (see melonprime_patch_lifecycle_gateway_site_d_plan.md).
// Called from RunFrameHook when the legacy in-game flag is false while
// in-game lifecycle flags are still set. Deactivates match ARM9 hooks only.
// The caller still owns clearing BIT_IN_GAME_INIT /
// BIT_END_OF_GAME_PATCH_RESTORED / BIT_BATTLE_RUNTIME_MODE and the
// transient-input / HUD / weapon-switch cleanup that runs alongside this
// leave-in-game transition. Do not add Patches_RestoreOnLeave here.
void DeactivateHooksOnLeaveInGame(melonDS::NDS* nds,
                                  EmuInstance* emu,
                                  Config::Table& cfg,
                                  const RomAddresses& rom,
                                  MelonPrimeCore* core);

// ROM-detect cold path. Deactivates match ARM9 hooks after a ROM group is
// detected and before ROM-detect OSD/aim-config reload work. This keeps
// direct ARM9Hook_SetMatchHooksActive ownership in PatchLifecycle without
// changing ROM detection state or address resolution.
void DeactivateHooksForRomDetect(melonDS::NDS* nds,
                                 EmuInstance* emu,
                                 Config::Table& cfg,
                                 const RomAddresses& rom,
                                 MelonPrimeCore* core);

} // namespace PatchLifecycle
} // namespace MelonPrime
