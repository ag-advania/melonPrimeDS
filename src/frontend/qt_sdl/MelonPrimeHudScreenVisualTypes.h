#ifndef MELON_PRIME_HUD_SCREEN_VISUAL_TYPES_H
#define MELON_PRIME_HUD_SCREEN_VISUAL_TYPES_H

// POD identity/stamp/key types for one Custom HUD visual frame. This header is
// intentionally free of runtime, localization, and renderer dependencies so
// generic Screen.h only pays for the value type it stores.

#include <cstdint>

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

#endif // MELON_PRIME_HUD_SCREEN_VISUAL_TYPES_H
