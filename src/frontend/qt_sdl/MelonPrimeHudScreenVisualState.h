#ifndef MELON_PRIME_HUD_SCREEN_VISUAL_STATE_H
#define MELON_PRIME_HUD_SCREEN_VISUAL_STATE_H

// Compact value types used to identify and retain one Custom HUD visual
// frame.  Keeping these outside Screen.h prevents the generic QWidget seam
// from owning HUD-specific presentation semantics while preserving the same
// POD comparisons and inline key construction used by the renderer paths.

#include <cstdint>

#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeLocalization.h"

class EmuInstance;

struct HudVisualFrameIdentity
{
    const void* nds = nullptr;
    std::uint32_t gameFrame = 0;

    bool operator==(const HudVisualFrameIdentity& other) const noexcept
    {
        return nds == other.nds && gameFrame == other.gameFrame;
    }
};

struct HudVisualFrameStamp
{
    std::uint32_t configEpoch = 0;
    std::uint32_t fontEpoch = 0;
    std::uint32_t stateGeneration = 0;
    int menuLanguage = 0;
    int overlayWidth = 0;
    int overlayHeight = 0;
    float topStretchX = 0.0f;
    float hudScale = 0.0f;
    float originX = 0.0f;
    float originY = 0.0f;
    std::uint64_t rendererGeneration = 0;
    bool hudEnabled = false;
    bool editMode = false;

    bool operator==(const HudVisualFrameStamp& other) const noexcept
    {
        return configEpoch == other.configEpoch
            && fontEpoch == other.fontEpoch
            && stateGeneration == other.stateGeneration
            && menuLanguage == other.menuLanguage
            && overlayWidth == other.overlayWidth
            && overlayHeight == other.overlayHeight
            && topStretchX == other.topStretchX
            && hudScale == other.hudScale
            && originX == other.originX
            && originY == other.originY
            && rendererGeneration == other.rendererGeneration
            && hudEnabled == other.hudEnabled
            && editMode == other.editMode;
    }
};

struct HudVisualFrameKey
{
    HudVisualFrameIdentity identity{};
    HudVisualFrameStamp stamp{};

    bool operator==(const HudVisualFrameKey& other) const noexcept
    {
        return identity == other.identity && stamp == other.stamp;
    }
};

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
