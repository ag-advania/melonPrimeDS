#ifndef MELON_PRIME_HUD_SCREEN_VISUAL_STATE_H
#define MELON_PRIME_HUD_SCREEN_VISUAL_STATE_H

// Runtime-aware helpers for constructing one Custom HUD visual frame key.
// The POD types live in MelonPrimeHudScreenVisualTypes.h so generic Screen.h
// does not pull in runtime or localization dependencies.

#include "MelonPrimeHudScreenVisualTypes.h"
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeLocalization.h"

class EmuInstance;

inline HudVisualFrameIdentity MelonPrimeHud_ProbeVisualFrameIdentity(
    EmuInstance* emu)
{
    HudVisualFrameIdentity identity;
    identity.gameFrame = MelonPrime::CustomHud_GetVisualGameFrame(
        emu, &identity.nds);
    return identity;
}

inline bool MelonPrimeHud_IsSameVisualGameFrame(
    const HudVisualFrameIdentity& identity,
    const HudVisualFrameKey& previous)
{
    return previous.identity == identity;
}

inline HudVisualFrameKey MelonPrimeHud_MakeVisualFrameKey(
    const HudVisualFrameIdentity& identity,
    const MelonPrime::CustomHudConfigState& hudConfig,
    std::uint32_t configEpoch,
    std::uint32_t fontEpoch,
    int overlayWidth,
    int overlayHeight,
    float topStretchX,
    float hudScale,
    float originX,
    float originY,
    std::uint64_t rendererGeneration,
    bool hudEnabled,
    bool editMode)
{
    HudVisualFrameKey key;
    key.identity = identity;
    key.stamp.configEpoch = configEpoch;
    key.stamp.fontEpoch = fontEpoch;
    key.stamp.stateGeneration = MelonPrime::CustomHud_GetVisualGeneration(hudConfig);
    key.stamp.menuLanguage = static_cast<int>(MelonPrime::UiText::ActiveMenuLanguage());
    key.stamp.overlayWidth = overlayWidth;
    key.stamp.overlayHeight = overlayHeight;
    key.stamp.topStretchX = topStretchX;
    key.stamp.hudScale = hudScale;
    key.stamp.originX = originX;
    key.stamp.originY = originY;
    key.stamp.rendererGeneration = rendererGeneration;
    key.stamp.hudEnabled = hudEnabled;
    key.stamp.editMode = editMode;
    return key;
}

#endif // MELON_PRIME_HUD_SCREEN_VISUAL_STATE_H
