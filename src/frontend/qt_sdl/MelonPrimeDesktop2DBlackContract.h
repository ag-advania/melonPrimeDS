#pragma once

#include <array>
#include <cstdint>

#include "VulkanReference/VulkanOutput.h"

namespace MelonDSAndroid
{

inline constexpr u32 kDesktopPacked3dPlaceholder = 0x20000000u;

inline bool packedPixelIsPresent2D(u32 pixel) noexcept
{
    return pixel != 0u
        && pixel != kDesktopPacked3dPlaceholder
        && ((pixel >> 24u) & 0xC0u) != 0x40u;
}

inline bool packedPixelHasColoredRGB(u32 pixel) noexcept
{
    return packedPixelIsPresent2D(pixel)
        && (pixel & 0x00FFFFFFu) != 0u;
}

inline bool packedPixelIsOpaqueBlack(u32 pixel) noexcept
{
    return pixel != 0u
        && pixel != kDesktopPacked3dPlaceholder
        && (pixel & 0x00FFFFFFu) == 0u;
}

inline bool packedControlMarksProtectedBlack2D(u32 control) noexcept
{
    return ((control >> 24u) & 0x20u) != 0u;
}

inline bool packedPixelIsProtectedBlack2D(u32 pixel, u32 control) noexcept
{
    return packedPixelIsPresent2D(pixel)
        && (pixel & 0x00FFFFFFu) == 0u
        && packedControlMarksProtectedBlack2D(control);
}

inline bool engineAOwnsPhysicalTop(u32 topEngine, u32 bottomEngine) noexcept
{
    return topEngine == 0u && bottomEngine == 1u;
}

struct SoftPackedBlackContractStats
{
    u32 present2DPixels = 0;
    u32 opaqueBlack2DPixels = 0;
    u32 protectedBlackPixels = 0;
    u32 blackWithoutProtectionPixels = 0;
    u32 protectedFlagWithoutBlackPixels = 0;
    u32 structuredSlotPixels = 0;
    u32 structuredAbovePixels = 0;
    u32 structured2DOnlyPixels = 0;
};

inline SoftPackedBlackContractStats measureSoftPackedBlackContract(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control)
{
    SoftPackedBlackContractStats stats{};
    for (size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; i++)
    {
        const u32 p0 = plane0[i];
        const u32 p1 = plane1[i];
        const u32 c = control[i];
        const u32 controlAlpha = c >> 24u;
        const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
        const bool structuredAbove = structuredSlot && (controlAlpha & 0x80u) != 0u;
        const bool structured2DOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
        const bool protectedBlack = packedControlMarksProtectedBlack2D(c);
        const bool present0 = packedPixelIsPresent2D(p0);
        const bool present1 = packedPixelIsPresent2D(p1);
        const bool opaqueBlack0 = packedPixelIsOpaqueBlack(p0);
        const bool opaqueBlack1 = packedPixelIsOpaqueBlack(p1);

        if (present0 || present1 || protectedBlack)
            stats.present2DPixels++;
        if (opaqueBlack0 || opaqueBlack1)
            stats.opaqueBlack2DPixels++;
        if (protectedBlack)
            stats.protectedBlackPixels++;
        if ((opaqueBlack0 || opaqueBlack1) && !protectedBlack
            && (structuredSlot || structuredAbove || structured2DOnly))
        {
            stats.blackWithoutProtectionPixels++;
        }
        if (protectedBlack && !opaqueBlack0 && !opaqueBlack1 && !present0 && !present1)
            stats.protectedFlagWithoutBlackPixels++;
        if (structuredSlot)
            stats.structuredSlotPixels++;
        if (structuredAbove)
            stats.structuredAbovePixels++;
        if (structured2DOnly)
            stats.structured2DOnlyPixels++;
    }
    return stats;
}

template<typename Plane0, typename Plane1, typename Control>
inline bool screenHasPresent2DContent(
    const Plane0& plane0,
    const Plane1& plane1,
    const Control& control)
{
    constexpr size_t kMinPresentPixels = SoftPackedFrameSnapshot::kPixelCount / 32;
    size_t presentPixels = 0;
    for (size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; i++)
    {
        if (packedPixelIsPresent2D(plane0[i])
            || packedPixelIsPresent2D(plane1[i])
            || packedControlMarksProtectedBlack2D(control[i]))
        {
            presentPixels++;
            if (presentPixels >= kMinPresentPixels)
                return true;
        }
    }
    return false;
}

} // namespace MelonDSAndroid
