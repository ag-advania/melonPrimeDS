#ifndef MELON_PRIME_HUD_PATCH_LIFECYCLE_H
#define MELON_PRIME_HUD_PATCH_LIFECYCLE_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Native HUD patch lifecycle.
//
//  Apply/restore/reset of the native-HUD (no-HUD, helmet layer) patches and
//  the host-side tracking that has to survive savestate RAM replacement.
//  This changes for ROM/patch reasons; keeping it out of the renderer header
//  stops a front-end that only draws from seeing patch internals.
// =========================================================================

#include <cstdint>

#include "MelonPrimeHudConfigState.h"

class EmuInstance;

namespace MelonPrime {

    // Cold config-boundary query used to cache whether Vulkan should mask the
    // native helmet BG1-3 layers. Never call this from a per-frame hot path.
    bool CustomHud_IsHelmetLayerHideConfigured(Config::Table& localCfg);

    // Per-frame, before RunFrame: keep the native helmet layers off across the
    // spawn window. No-op unless the helmet hide patch is currently applied;
    // native UI frames are left untouched by this RAM/register clamp.
    void CustomHud_ClampHelmetLayersPreFrame(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        const RomAddresses& rom,
        uint8_t playerPosition);

    // Ensure the no-HUD patch is reverted when custom HUD is disabled.
    // Call every frame from Screen.cpp even when the HUD overlay is not rendered.
    void CustomHud_EnsurePatchRestored(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        uint8_t playerPosition,
        bool isInGame
    );

    // Reset patch tracking state (call on emu stop/reset).
    void CustomHud_ResetPatchState(CustomHudConfigState& hudConfig);

    // Reconcile host-side patch tracking after savestate RAM replacement.
    // Every native-HUD instruction is immediately rewritten from the current
    // configuration, independent of the settings used to save the state.
    void CustomHud_ReconcilePatchAfterSavestateLoad(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        uint8_t playerPosition);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_PATCH_LIFECYCLE_H
