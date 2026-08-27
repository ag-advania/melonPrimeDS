#ifndef MELON_PRIME_HUD_RUNTIME_H
#define MELON_PRIME_HUD_RUNTIME_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD runtime queries.
//
//  Emulated-state interpretation that front-ends consult around a frame:
//  gameplay visibility rules, presentation identity/generation counters, and
//  the match-join sampling entry point.  No drawing, no editor, no patching.
// =========================================================================

#include <cstdint>

#include "MelonPrimeHudConfigState.h"

class EmuInstance;

namespace MelonPrime {

    // Returns true when the shared HUD hide condition is active.
    // Takes the owning instance's HUD state because the decision is sampled
    // through the per-instance HUD frame cache, exactly like the painter path.
    bool CustomHud_ShouldHideForGameplayState(CustomHudConfigState& hudConfig,
                                              EmuInstance* emu,
                                              const RomAddresses& rom,
                                              uint8_t playerPosition);

    // Returns true when the radar overlay should be drawn on the top screen.
    // Renderer front-ends call this outside the painter path, so it carries the
    // same per-instance HUD state ownership as the rest of the runtime API.
    bool CustomHud_ShouldDrawRadarOverlay(CustomHudConfigState& hudConfig,
                                          EmuInstance* emu,
                                          const RomAddresses& rom,
                                          uint8_t playerPosition);

    // Generation for emulation-state transitions that can reuse the same
    // NDS::NumFrames value (match join, reset, ROM/savestate replacement).
    uint32_t CustomHud_GetVisualGeneration(const CustomHudConfigState& hudConfig);

    // Lightweight presentation identity used by renderer front-ends to skip
    // rebuilding an unchanged overlay.  The returned frame is the emulated
    // NDS frame, while `ndsIdentity` receives the instance identity pointer.
    uint32_t CustomHud_GetVisualGameFrame(EmuInstance* emu,
                                          const void** ndsIdentity);

    // Cache battle settings at match join (call from HandleGameJoinInit).
    void CustomHud_OnMatchJoin(CustomHudConfigState& hudConfig, uint8_t* ram, const RomAddresses& rom);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RUNTIME_H
