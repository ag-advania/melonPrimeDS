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
                      const RomAddresses& rom);

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu);

void RestoreForEmuStop(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       bool romDetected);

void ReapplyForConfigReload(melonDS::NDS* nds,
                            EmuInstance* emu,
                            Config::Table& cfg,
                            const RomAddresses& rom,
                            MelonPrimeCore* core,
                            bool romDetected,
                            bool battleRuntimeMode);

// Step 3 / Site E (see melonprime_patch_lifecycle_gateway_step3_plan.md).
// Called every out-of-game focused frame from RunFrameHook. Registry
// entries at PatchSite_OutOfGameFrame (FixWifi / UseFirmwareLanguage /
// ExpandStageMatrix) self-guard, so this is a cheap cold-path check, not a
// per-frame patch write — do not add further gating around this call.
void ApplyOutOfGameFrame(melonDS::NDS* nds,
                         EmuInstance* emu,
                         Config::Table& cfg,
                         const RomAddresses& rom);

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

} // namespace PatchLifecycle
} // namespace MelonPrime
