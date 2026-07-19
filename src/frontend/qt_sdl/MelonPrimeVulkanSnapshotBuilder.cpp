#include "MelonPrimeVulkanSnapshotBuilder.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstring>

namespace MelonPrime
{
bool areRendererDebugBgObjLogsEnabled();

namespace
{

constexpr u32 kPacked3dPlaceholder = 0x20000000u;
constexpr u32 kMetaRegularCaptureUses3d = 1u << 21u;
constexpr u32 kMetaVramCaptureUses3d = 1u << 22u;
constexpr u32 kMetaForceLive3dCompMode7 = 1u << 18u;
constexpr u32 kMetaExactRegularCaptureUses3d = 1u << 19u;
bool packedPixelIsOpaqueBlack(u32 pixel) noexcept
{
    return pixel != 0u
        && pixel != kPacked3dPlaceholder
        && (pixel & 0x00FFFFFFu) == 0u
        && (pixel >> 24u) != 0u;
}

bool packedPixelHasVisibleColor(u32 pixel) noexcept
{
    return pixel != 0u
        && pixel != kPacked3dPlaceholder
        && (pixel & 0x00FFFFFFu) != 0u;
}

bool packedPixelIsUseful(u32 pixel) noexcept
{
    return packedPixelHasVisibleColor(pixel) || packedPixelIsOpaqueBlack(pixel);
}

bool packedLineNeedsCompMode7Live3dFallback(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    u32 lineMeta,
    int line) noexcept
{
    const u32 displayMode = (lineMeta >> 16u) & 0x3u;
    if (displayMode != 1u || (lineMeta & kMetaRegularCaptureUses3d) == 0u)
        return false;
    const size_t rowBase = static_cast<size_t>(line) * SoftPackedFrameSnapshot::kScreenWidth;
    bool sawCompMode7 = false;
    for (size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
    {
        const size_t index = rowBase + x;
        const u32 compMode = (control[index] >> 24u) & 0xFu;
        if (compMode == 7u)
            sawCompMode7 = true;
    }

    return sawCompMode7;
}

bool packedLineHasAnyVisibleColor(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& pixels,
    int line) noexcept
{
    if (line < 0 || line >= SoftPackedFrameSnapshot::kLineCount)
        return false;

    const size_t rowBase = static_cast<size_t>(line) * SoftPackedFrameSnapshot::kScreenWidth;
    for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
    {
        if (packedPixelHasVisibleColor(pixels[rowBase + static_cast<size_t>(x)]))
            return true;
    }

    return false;
}

bool packedResolvedLineHasAnyUsefulPixel(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& pixels,
    std::size_t y) noexcept
{
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const u32 pixel = pixels[rowBase + x];
        if (pixel != 0u && pixel != kPacked3dPlaceholder)
            return true;
    }
    return false;
}

bool packedResolvedLineIsMostlyOpaqueBlack(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& pixels,
    std::size_t y) noexcept
{
    int blackPixels = 0;
    int usefulPixels = 0;
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const u32 pixel = pixels[rowBase + x];
        if (pixel == 0u || pixel == kPacked3dPlaceholder)
            continue;
        ++usefulPixels;
        if ((pixel & 0x00FFFFFFu) == 0u)
            ++blackPixels;
    }
    return usefulPixels > 0
        && blackPixels >= static_cast<int>((SoftPackedFrameSnapshot::kScreenWidth * 9u) / 10u);
}

bool lineMaskHasAnyValidLine(
    const std::array<u8, SoftPackedFrameSnapshot::kLineCount>& lineMask) noexcept
{
    return std::any_of(lineMask.begin(), lineMask.end(), [](u8 value) { return value != 0u; });
}

bool softPackedScreenUsesPlainStructured3dSlot(const SoftPackedScreenStats& stats) noexcept
{
    constexpr u32 nearlyFullPixelThreshold =
        (SoftPackedFrameSnapshot::kPixelCount * 7u) / 8u;
    constexpr u32 dominantLineThreshold = SoftPackedFrameSnapshot::kLineCount / 2u;
    return stats.DisplayModeCounts[1] > dominantLineThreshold
        && stats.StructuredSlotPixels > nearlyFullPixelThreshold
        && stats.StructuredAbovePixels == 0u
        && stats.StructuredAboveVisiblePixels == 0u
        && stats.Structured2DOnlyPixels == 0u
        && stats.Plane0VisiblePixels == 0u
        && stats.Plane1VisiblePixels == 0u
        && stats.RegularCaptureUses3dLines == 0u
        && stats.VramCaptureUses3dLines == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool softPackedScreenUsesFullStructured2dOnlyDisplay(const SoftPackedScreenStats& stats) noexcept
{
    constexpr u32 nearlyFullPixelThreshold =
        (SoftPackedFrameSnapshot::kPixelCount * 7u) / 8u;
    return stats.DisplayModeCounts[1] > (SoftPackedFrameSnapshot::kLineCount / 2u)
        && stats.CompModeCounts[7] > nearlyFullPixelThreshold
        && stats.Structured2DOnlyPixels > nearlyFullPixelThreshold
        && stats.StructuredSlotPixels == 0u
        && stats.StructuredAbovePixels == 0u
        && stats.RegularCaptureUses3dLines == 0u
        && stats.VramCaptureUses3dLines == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool softPackedScreenUsesFullStructuredSlotDisplay(const SoftPackedScreenStats& stats) noexcept
{
    constexpr u32 nearlyFullPixelThreshold =
        (SoftPackedFrameSnapshot::kPixelCount * 7u) / 8u;
    return stats.DisplayModeCounts[1] > (SoftPackedFrameSnapshot::kLineCount / 2u)
        && stats.StructuredSlotPixels > nearlyFullPixelThreshold
        && stats.StructuredAbovePixels == 0u
        && stats.Structured2DOnlyPixels == 0u
        && stats.RegularCaptureUses3dLines == 0u
        && stats.VramCaptureUses3dLines == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool packedScreenUsesFullStructuredCompMode2Slot(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) noexcept
{
    constexpr u32 nearlyFullPixelThreshold =
        (SoftPackedFrameSnapshot::kPixelCount * 7u) / 8u;
    u32 matchingPixels = 0;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const u32 meta = lineMeta[y];
        const u32 displayMode = (meta >> 16u) & 0x3u;
        const bool structuredDisplayOnly =
            displayMode == 1u
            && (meta & (kMetaRegularCaptureUses3d
                | kMetaVramCaptureUses3d
                | kMetaForceLive3dCompMode7)) == 0u;
        if (!structuredDisplayOnly)
            continue;

        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const u32 controlAlpha = control[rowBase + x] >> 24u;
            const u32 compMode = controlAlpha & 0xFu;
            const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
            const bool structuredAbove = structuredSlot && (controlAlpha & 0x80u) != 0u;
            if (compMode == 2u && structuredSlot && !structuredAbove)
                ++matchingPixels;
        }
    }
    return matchingPixels > nearlyFullPixelThreshold;
}

bool softPackedScreenUsesMostlyStructured2dOnlyDisplay(const SoftPackedScreenStats& stats) noexcept
{
    constexpr u32 screenPixels = SoftPackedFrameSnapshot::kPixelCount;
    constexpr u32 dominantLineThreshold = SoftPackedFrameSnapshot::kLineCount / 2u;
    return stats.DisplayModeCounts[1] > dominantLineThreshold
        && stats.CompModeCounts[7] > ((screenPixels * 7u) / 8u)
        && stats.Structured2DOnlyPixels > ((screenPixels * 3u) / 4u)
        && stats.StructuredSlotPixels <= (screenPixels / 8u)
        && stats.StructuredAbovePixels == 0u
        && stats.RegularCaptureUses3dLines == 0u
        && stats.VramCaptureUses3dLines == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool softPackedScreenUsesEmptyDisplayCapture(const SoftPackedScreenStats& stats) noexcept
{
    return stats.DisplayModeCounts[2] > (SoftPackedFrameSnapshot::kLineCount / 2u)
        && stats.DisplayModeCounts[1] == 0u
        && std::all_of(stats.CompModeCounts.begin(), stats.CompModeCounts.end(), [](u32 count) { return count == 0u; })
        && stats.StructuredSlotPixels == 0u
        && stats.StructuredAbovePixels == 0u
        && stats.Structured2DOnlyPixels == 0u
        && stats.Plane0UsefulPixels == 0u
        && stats.Plane0VisiblePixels == 0u
        && stats.Plane1UsefulPixels == 0u
        && stats.Plane1VisiblePixels == 0u
        && stats.RegularCaptureUses3dLines == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool softPackedScreenUsesRegularStructured3dCaptureSlot(const SoftPackedScreenStats& stats) noexcept
{
    constexpr u32 screenPixels = SoftPackedFrameSnapshot::kPixelCount;
    return stats.DisplayModeCounts[1] > (SoftPackedFrameSnapshot::kLineCount / 2u)
        && stats.RegularCaptureUses3dLines > (SoftPackedFrameSnapshot::kLineCount / 2u)
        && stats.VramCaptureUses3dLines == 0u
        && stats.StructuredSlotPixels > ((screenPixels * 7u) / 8u)
        && stats.StructuredAbovePixels == 0u
        && stats.Structured2DOnlyPixels == 0u
        && stats.ForceLive3dCompMode7Lines == 0u;
}

bool lineHasUsefulPixel(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& pixels,
    std::size_t y) noexcept
{
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        if (packedPixelIsUseful(pixels[rowBase + x]))
            return true;
    }
    return false;
}

SoftPackedScreenStats collectPackedScreenStatsFromSnapshot(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta)
{
    SoftPackedScreenStats stats{};
    const bool exactNonRegularDisplayContentCounts = areRendererDebugBgObjLogsEnabled();

    for (int y = 0; y < 192; y++)
    {
        const u32 meta = lineMeta[static_cast<size_t>(y)];
        const u32 displayMode = (meta >> 16u) & 0x3u;
        stats.DisplayModeCounts[displayMode]++;
        if ((meta & kMetaRegularCaptureUses3d) != 0u)
            stats.RegularCaptureUses3dLines++;
        if (displayMode == 2u && (meta & kMetaVramCaptureUses3d) != 0u)
            stats.VramCaptureUses3dLines++;
        if ((meta & kMetaForceLive3dCompMode7) != 0u)
            stats.ForceLive3dCompMode7Lines++;

        const int xOffset = static_cast<int>((meta >> 24u) & 0xFFu)
            - ((((meta >> 16u) & 0x80u) != 0u) ? 256 : 0);
        if (!stats.HasOffsets)
        {
            stats.MinXOffset = xOffset;
            stats.MaxXOffset = xOffset;
            stats.HasOffsets = true;
        }
        else
        {
            stats.MinXOffset = std::min(stats.MinXOffset, xOffset);
            stats.MaxXOffset = std::max(stats.MaxXOffset, xOffset);
        }

        if (displayMode != 1u)
        {
            const size_t rowBase = static_cast<size_t>(y) * SoftPackedFrameSnapshot::kScreenWidth;
            bool plane0VisibleFound = stats.Plane0VisiblePixels > 0u;
            bool plane1VisibleFound = stats.Plane1VisiblePixels > 0u;
            bool plane0UsefulFound = stats.Plane0UsefulPixels > 0u;
            bool plane1UsefulFound = stats.Plane1UsefulPixels > 0u;
            if (!exactNonRegularDisplayContentCounts && plane0VisibleFound && plane1VisibleFound)
                continue;
            for (int x = 0; x < 256; x++)
            {
                const size_t index = rowBase + static_cast<size_t>(x);
                const u32 plane0Pixel = plane0[index];
                const u32 plane1Pixel = plane1[index];
                const bool plane0Useful = plane0Pixel != 0u && plane0Pixel != kPacked3dPlaceholder;
                const bool plane1Useful = plane1Pixel != 0u && plane1Pixel != kPacked3dPlaceholder;
                if (plane0Useful)
                {
                    if (exactNonRegularDisplayContentCounts || !plane0UsefulFound)
                    {
                        stats.Plane0UsefulPixels++;
                        plane0UsefulFound = true;
                    }
                    if ((plane0Pixel & 0x00FFFFFFu) != 0u)
                    {
                        stats.Plane0VisiblePixels++;
                        plane0VisibleFound = true;
                    }
                    else if (exactNonRegularDisplayContentCounts || !plane0VisibleFound)
                    {
                        stats.Plane0OpaqueBlackPixels++;
                    }
                }
                if (plane1Useful)
                {
                    if (exactNonRegularDisplayContentCounts || !plane1UsefulFound)
                    {
                        stats.Plane1UsefulPixels++;
                        plane1UsefulFound = true;
                    }
                    if ((plane1Pixel & 0x00FFFFFFu) != 0u)
                    {
                        stats.Plane1VisiblePixels++;
                        plane1VisibleFound = true;
                    }
                    else if (exactNonRegularDisplayContentCounts || !plane1VisibleFound)
                    {
                        stats.Plane1OpaqueBlackPixels++;
                    }
                }

                if (!exactNonRegularDisplayContentCounts && plane0VisibleFound && plane1VisibleFound)
                    break;
            }
            continue;
        }

        bool lineHasCaptureBackedComp4 = false;
        const size_t rowBase = static_cast<size_t>(y) * SoftPackedFrameSnapshot::kScreenWidth;
        for (int x = 0; x < 256; x++)
        {
            const size_t index = rowBase + static_cast<size_t>(x);
            const u32 controlAlpha = control[index] >> 24u;
            const u32 plane0Pixel = plane0[index];
            const u32 plane1Pixel = plane1[index];
            const bool plane0Useful = plane0Pixel != 0u && plane0Pixel != kPacked3dPlaceholder;
            const bool plane1Useful = plane1Pixel != 0u && plane1Pixel != kPacked3dPlaceholder;
            if (plane0Useful)
            {
                stats.Plane0UsefulPixels++;
                if ((plane0Pixel & 0x00FFFFFFu) != 0u)
                    stats.Plane0VisiblePixels++;
                else
                    stats.Plane0OpaqueBlackPixels++;
            }
            if (plane1Useful)
            {
                stats.Plane1UsefulPixels++;
                if ((plane1Pixel & 0x00FFFFFFu) != 0u)
                    stats.Plane1VisiblePixels++;
                else
                    stats.Plane1OpaqueBlackPixels++;
            }
            const u32 compMode = controlAlpha & 0xFu;
            if (compMode < stats.CompModeCounts.size())
                stats.CompModeCounts[compMode]++;
            const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
            const bool structuredAbove = structuredSlot && (controlAlpha & 0x80u) != 0u;
            const bool structured2DOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
            if ((controlAlpha & 0x20u) != 0u)
                stats.ProtectedBlackPixels++;
            if (structuredSlot)
                stats.StructuredSlotPixels++;
            if (structuredAbove)
            {
                stats.StructuredAbovePixels++;
                if (plane1Useful && (plane1Pixel & 0x00FFFFFFu) != 0u)
                    stats.StructuredAboveVisiblePixels++;
                else if (plane1Useful)
                    stats.StructuredAboveBlackPixels++;
            }
            if (structured2DOnly)
            {
                stats.Structured2DOnlyPixels++;
                if (plane0Useful && (plane0Pixel & 0x00FFFFFFu) != 0u)
                    stats.Structured2DOnlyVisiblePixels++;
            }

            const bool captureBackedComp4 =
                compMode == 4u
                && plane0[index] == kPacked3dPlaceholder
                && plane1[index] == kPacked3dPlaceholder;
            if (!captureBackedComp4)
                continue;

            stats.CaptureBackedComp4Pixels++;
            lineHasCaptureBackedComp4 = true;
        }

        if (lineHasCaptureBackedComp4)
            stats.CaptureBackedComp4Lines++;
    }

    return stats;
}

bool structuredLineHasPayload(
    const u32* plane0,
    const u32* plane1,
    const u32* control,
    std::size_t rowBase) noexcept
{
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        if (control[index] != 0u || plane1[index] != 0u || plane0[index] != 0u)
            return true;
    }
    return false;
}

void copyStructuredLine(
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const u32* structuredPlane0,
    const u32* structuredPlane1,
    const u32* structuredControl,
    std::size_t rowBase) noexcept
{
    constexpr std::size_t rowBytes = SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32);
    std::memcpy(plane0.data() + rowBase, structuredPlane0 + rowBase, rowBytes);
    std::memcpy(plane1.data() + rowBase, structuredPlane1 + rowBase, rowBytes);
    std::memcpy(control.data() + rowBase, structuredControl + rowBase, rowBytes);
}

bool packedControlMarksProtectedBlack2D(u32 control) noexcept
{
    return ((control >> 24u) & 0x20u) != 0u;
}

bool packedPixelIsCaptureBackedComp4(u32 plane0Pixel, u32 plane1Pixel, u32 controlPixel) noexcept
{
    const u32 compMode = (controlPixel >> 24u) & 0xFu;
    return compMode == 4u
        && plane0Pixel == kPacked3dPlaceholder
        && plane1Pixel == kPacked3dPlaceholder;
}

struct CaptureLineCounts
{
    int regularCaptureLines = 0;
    int vramCaptureLines = 0;
};

CaptureLineCounts countCaptureLineCounts(
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) noexcept
{
    CaptureLineCounts counts;
    for (u32 meta : lineMeta)
    {
        const u32 displayMode = (meta >> 16u) & 0x3u;
        if (displayMode == 1u && (meta & kMetaRegularCaptureUses3d) != 0u)
            ++counts.regularCaptureLines;
        if (displayMode == 2u && (meta & kMetaVramCaptureUses3d) != 0u)
            ++counts.vramCaptureLines;
    }
    return counts;
}

// Engine A/B shadow planes must not be merged back onto physical Top/Bottom
// via frame-level physicalScreenSwap. source.plane[0/1] is already routed.

// --- Capture metadata normalization (Sapphire clearBroadPartialRegularCapture
// / clearBroadRegularCaptureAgainstOppositeVram).

void normalizeCaptureLineMeta(
    std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    bool partialCapture3dMask,
    int regularCaptureLineCount,
    int vramCaptureLineCount,
    int oppositeVramCaptureLineCount,
    bool clearBroadRegularAgainstOppositeVram) noexcept
{
    constexpr int kLineCount = static_cast<int>(SoftPackedFrameSnapshot::kLineCount);

    if (partialCapture3dMask
        && regularCaptureLineCount > (kLineCount / 2)
        && vramCaptureLineCount == 0)
    {
        for (u32& meta : lineMeta)
        {
            const u32 displayMode = (meta >> 16u) & 0x3u;
            const bool exactRegularCapture = (meta & kMetaExactRegularCaptureUses3d) != 0u;
            if (displayMode == 1u && !exactRegularCapture)
                meta &= ~kMetaRegularCaptureUses3d;
        }
    }

    if (clearBroadRegularAgainstOppositeVram
        && regularCaptureLineCount > (kLineCount / 2)
        && vramCaptureLineCount == 0
        && oppositeVramCaptureLineCount > (kLineCount / 2))
    {
        bool hasExactRegularCapture = false;
        for (u32 meta : lineMeta)
        {
            const u32 displayMode = (meta >> 16u) & 0x3u;
            if (displayMode == 1u && (meta & kMetaExactRegularCaptureUses3d) != 0u)
            {
                hasExactRegularCapture = true;
                break;
            }
        }
        if (!hasExactRegularCapture)
        {
            for (u32& meta : lineMeta)
            {
                const u32 displayMode = (meta >> 16u) & 0x3u;
                if (displayMode == 1u)
                    meta &= ~kMetaRegularCaptureUses3d;
            }
        }
    }
}

// --- Temporal carries (Sapphire carryPreviousLatchedScreenLines /
// carryPreviousTemporalOverlayPixels / carryPreviousFullRegularComp7Overlay).

bool latchedSnapshotLineIsZero(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    std::size_t y) noexcept
{
    if (lineMeta[y] != 0u)
        return false;

    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        if (plane0[index] != 0u || plane1[index] != 0u)
            return false;
    }
    return true;
}

bool latchedSnapshotLineNeedsTemporalCarry(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    std::size_t y) noexcept
{
    if (latchedSnapshotLineIsZero(plane0, plane1, lineMeta, y))
        return true;

    const u32 meta = lineMeta[y];
    if ((meta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u)
        return false;

    const u32 displayMode = (meta >> 16u) & 0x3u;
    if (displayMode != 1u)
        return false;
    if (lineHasUsefulPixel(plane0, y) || lineHasUsefulPixel(plane1, y))
        return false;

    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 plane0Pixel = plane0[index];
        const u32 plane1Pixel = plane1[index];
        const bool plane0IsMissing =
            plane0Pixel == 0u || plane0Pixel == 0xFF000000u || plane0Pixel == kPacked3dPlaceholder;
        const bool plane1IsMissing =
            plane1Pixel == 0u || plane1Pixel == 0xFF000000u || plane1Pixel == kPacked3dPlaceholder;
        if (!plane0IsMissing || !plane1IsMissing)
            return false;
    }
    return true;
}

bool previousSnapshotLineNeedsTemporalCarry(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    std::size_t y) noexcept
{
    const u32 meta = lineMeta[y];
    if ((meta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u)
        return true;

    const u32 displayMode = (meta >> 16u) & 0x3u;
    if (displayMode != 1u)
        return false;

    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 compMode = (control[index] >> 24u) & 0xFu;
        if (compMode == 4u
            && plane0[index] == kPacked3dPlaceholder
            && plane1[index] == kPacked3dPlaceholder)
        {
            return true;
        }
    }
    return false;
}

int carryPreviousLatchedScreenLines(
    const SoftPackedFrameSnapshot& previousSnapshot,
    bool topScreen,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) noexcept
{
    if (!previousSnapshot.valid)
        return 0;

    const auto& previousPlane0 = topScreen ? previousSnapshot.packedTopPlane0 : previousSnapshot.packedBottomPlane0;
    const auto& previousPlane1 = topScreen ? previousSnapshot.packedTopPlane1 : previousSnapshot.packedBottomPlane1;
    const auto& previousControl = topScreen ? previousSnapshot.packedTopControl : previousSnapshot.packedBottomControl;
    const auto& previousLineMeta = topScreen ? previousSnapshot.packedTopLineMeta : previousSnapshot.packedBottomLineMeta;

    int carriedLines = 0;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        if (!latchedSnapshotLineNeedsTemporalCarry(plane0, plane1, lineMeta, y))
            continue;
        if (latchedSnapshotLineIsZero(previousPlane0, previousPlane1, previousLineMeta, y))
            continue;
        if (!previousSnapshotLineNeedsTemporalCarry(previousPlane0, previousPlane1, previousControl, previousLineMeta, y))
            continue;

        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        const u32 currentMeta = lineMeta[y];
        const u32 previousMeta = previousLineMeta[y];
        const bool currentLineHasExplicit3DMeta =
            (currentMeta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u;
        const bool previousLineHasExplicit3DMeta =
            (previousMeta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u;
        if (!currentLineHasExplicit3DMeta && !previousLineHasExplicit3DMeta)
            continue;

        int previousOpaqueBlackPixels = 0;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 previousCompMode = (previousControl[index] >> 24u) & 0xFu;
            const u32 previousPixel = previousPlane0[index];
            if (previousCompMode == 7u
                && previousPixel != 0u
                && previousPixel != kPacked3dPlaceholder
                && (previousPixel & 0x00FFFFFFu) == 0u)
            {
                ++previousOpaqueBlackPixels;
            }
        }
        const bool previousLineIsMostlyOpaqueBlack =
            static_cast<std::size_t>(previousOpaqueBlackPixels) >= (SoftPackedFrameSnapshot::kScreenWidth * 95u / 100u);
        const bool previousLineUsesRegular3dCapture =
            (previousMeta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d)) != 0u;
        if (previousLineUsesRegular3dCapture && !previousLineIsMostlyOpaqueBlack)
            continue;

        constexpr std::size_t rowBytes = SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32);
        std::memcpy(plane0.data() + rowBase, previousPlane0.data() + rowBase, rowBytes);
        std::memcpy(plane1.data() + rowBase, previousPlane1.data() + rowBase, rowBytes);
        std::memcpy(control.data() + rowBase, previousControl.data() + rowBase, rowBytes);
        lineMeta[y] = (previousLineMeta[y] & 0xFFFF0000u) | (lineMeta[y] & 0x0000FFFFu);
        ++carriedLines;
    }
    return carriedLines;
}

bool packedLineHasCarryableOverlayComposition(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    std::size_t y) noexcept
{
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 compMode = (control[index] >> 24u) & 0xFu;
        if (compMode == 7u)
        {
            if (packedPixelHasVisibleColor(plane0[index]))
                return true;
            continue;
        }
        if (packedPixelIsCaptureBackedComp4(plane0[index], plane1[index], control[index]))
            continue;
        return true;
    }
    return false;
}

bool packedLineCanAcceptTemporalOverlay(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    u32 lineMeta,
    std::size_t y) noexcept
{
    const u32 displayMode = (lineMeta >> 16u) & 0x3u;
    if (displayMode != 1u || (lineMeta & kMetaRegularCaptureUses3d) == 0u)
        return false;

    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 controlAlpha = control[index] >> 24u;
        const u32 compMode = controlAlpha & 0xFu;
        const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
        if (compMode == 7u || packedPixelIsCaptureBackedComp4(plane0[index], plane1[index], control[index]))
            return true;
        if (structuredSlot)
            return true;
    }
    return false;
}

int carryPreviousTemporalOverlayPixels(
    const SoftPackedFrameSnapshot& previousSnapshot,
    bool topScreen,
    bool captureBackedPartialClass0Only,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) noexcept
{
    if (!previousSnapshot.valid)
        return 0;

    const auto& previousPlane0 = topScreen ? previousSnapshot.packedTopPlane0 : previousSnapshot.packedBottomPlane0;
    const auto& previousPlane1 = topScreen ? previousSnapshot.packedTopPlane1 : previousSnapshot.packedBottomPlane1;
    const auto& previousControl = topScreen ? previousSnapshot.packedTopControl : previousSnapshot.packedBottomControl;
    const auto& previousLineMeta = topScreen ? previousSnapshot.packedTopLineMeta : previousSnapshot.packedBottomLineMeta;

    int carriedLines = 0;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        if (!packedLineCanAcceptTemporalOverlay(plane0, plane1, control, lineMeta[y], y))
            continue;
        if (!packedLineHasCarryableOverlayComposition(previousPlane0, previousPlane1, previousControl, y))
            continue;

        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        const u32 previousMeta = previousLineMeta[y];
        const u32 currentMeta = lineMeta[y];
        const bool previousLineUsesCapture3D =
            (previousMeta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d)) != 0u;
        const bool currentLineUsesCapture3D =
            (currentMeta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u;
        bool carriedAnyPixel = false;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 previousControlAlpha = previousControl[index] >> 24u;
            const u32 currentControlAlpha = control[index] >> 24u;
            const u32 previousCompMode = previousControlAlpha & 0xFu;
            const u32 currentCompMode = currentControlAlpha & 0xFu;
            const bool currentIsCaptureBackedComp4 =
                packedPixelIsCaptureBackedComp4(plane0[index], plane1[index], control[index]);
            const bool currentIsStructuredSlot = (currentControlAlpha & 0x40u) != 0u;
            const bool currentHasStructuredAbove =
                currentIsStructuredSlot && (currentControlAlpha & 0x80u) != 0u;
            const bool currentHasUsableAbove =
                currentHasStructuredAbove
                && (packedPixelHasVisibleColor(plane1[index])
                    || (packedControlMarksProtectedBlack2D(control[index]) && packedPixelIsOpaqueBlack(plane1[index])));
            const bool currentLive3DShouldOwnPixel =
                currentLineUsesCapture3D
                && currentCompMode == 7u
                && !currentHasUsableAbove
                && packedPixelHasVisibleColor(plane0[index]);
            const bool currentAcceptsOverlay =
                currentCompMode == 7u || currentIsCaptureBackedComp4 || currentHasStructuredAbove;
            const bool previousIsCaptureBackedComp4 =
                packedPixelIsCaptureBackedComp4(previousPlane0[index], previousPlane1[index], previousControl[index]);
            const bool previousHasStructuredAbove =
                (previousControlAlpha & 0x40u) != 0u
                && (previousControlAlpha & 0x80u) != 0u
                && packedPixelHasVisibleColor(previousPlane1[index]);
            const bool previousHasProtectedBlackAbove =
                (previousControlAlpha & 0x40u) != 0u
                && (previousControlAlpha & 0x80u) != 0u
                && packedControlMarksProtectedBlack2D(previousControl[index])
                && packedPixelIsOpaqueBlack(previousPlane1[index]);
            const bool previousHasProtectedBlackOnly =
                (previousControlAlpha & 0x40u) == 0u
                && (previousControlAlpha & 0x80u) != 0u
                && packedControlMarksProtectedBlack2D(previousControl[index])
                && packedPixelIsOpaqueBlack(previousPlane0[index]);

            if (captureBackedPartialClass0Only
                && (previousHasStructuredAbove || previousHasProtectedBlackAbove || previousHasProtectedBlackOnly)
                && currentIsStructuredSlot
                && !currentHasUsableAbove
                && (!currentLive3DShouldOwnPixel || previousHasProtectedBlackAbove || previousHasProtectedBlackOnly))
            {
                plane1[index] = previousHasProtectedBlackOnly ? previousPlane0[index] : previousPlane1[index];
                const u32 structuredAlpha = currentCompMode
                    | 0x40u
                    | 0x80u
                    | ((previousHasProtectedBlackAbove || previousHasProtectedBlackOnly) ? 0x20u : 0u);
                control[index] = (control[index] & 0x00FFFFFFu) | (structuredAlpha << 24u);
                carriedAnyPixel = true;
                continue;
            }

            const bool previousPlain2DOverlay =
                !previousLineUsesCapture3D
                && previousCompMode <= 4u
                && !previousIsCaptureBackedComp4
                && packedPixelHasVisibleColor(previousPlane0[index]);
            const bool previousPlainOverlayHasMetadata =
                (previousControl[index] & 0x00FFFFFFu) != 0u
                || (previousControlAlpha & (0x20u | 0x40u | 0x80u)) != 0u;
            const bool previousIsRealOverlay =
                (previousPlain2DOverlay && (!currentLineUsesCapture3D || previousPlainOverlayHasMetadata))
                || previousHasStructuredAbove;
            const bool previousComp7HadOverlayControl =
                previousCompMode == 7u && (previousControl[index] & 0x00FFFFFFu) != 0u;
            const bool currentPlane0Explicit2D =
                plane0[index] != 0u && plane0[index] != kPacked3dPlaceholder;
            const bool currentPlane1Explicit2D =
                plane1[index] != 0u && plane1[index] != kPacked3dPlaceholder;
            const bool currentHasExplicit2D =
                currentPlane0Explicit2D || currentPlane1Explicit2D || currentHasUsableAbove;

            if (currentHasExplicit2D
                && (previousIsRealOverlay || previousIsCaptureBackedComp4 || previousComp7HadOverlayControl)
                && !previousHasProtectedBlackAbove
                && !previousHasProtectedBlackOnly)
            {
                continue;
            }
            if (currentLive3DShouldOwnPixel
                && (previousIsRealOverlay || previousComp7HadOverlayControl)
                && !previousHasProtectedBlackAbove
                && !previousHasProtectedBlackOnly)
            {
                continue;
            }
            if (previousComp7HadOverlayControl
                && currentCompMode == 7u
                && packedPixelHasVisibleColor(plane0[index]))
            {
                control[index] = (control[index] & 0xFF000000u) | (previousControl[index] & 0x00FFFFFFu);
                carriedAnyPixel = true;
                continue;
            }

            const bool shouldCarry =
                currentAcceptsOverlay || previousIsRealOverlay || previousIsCaptureBackedComp4;
            if (!shouldCarry)
                continue;

            if (previousCompMode == 7u)
            {
                if (!currentIsCaptureBackedComp4 || !packedPixelHasVisibleColor(previousPlane0[index]))
                    continue;
            }

            plane0[index] = previousPlane0[index];
            plane1[index] = previousPlane1[index];
            if (previousIsRealOverlay && !currentAcceptsOverlay)
                control[index] = (previousControl[index] & 0x00FFFFFFu) | 0x05000000u;
            else
                control[index] = previousControl[index];
            carriedAnyPixel = true;
        }

        if (carriedAnyPixel)
            ++carriedLines;
    }
    return carriedLines;
}

int carryPreviousFullRegularComp7Overlay(
    const SoftPackedFrameSnapshot& previousSnapshot,
    bool topScreen,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) noexcept
{
    if (!previousSnapshot.valid)
        return 0;

    const auto& previousPlane0 = topScreen ? previousSnapshot.packedTopPlane0 : previousSnapshot.packedBottomPlane0;
    const auto& previousPlane1 = topScreen ? previousSnapshot.packedTopPlane1 : previousSnapshot.packedBottomPlane1;
    const auto& previousControl = topScreen ? previousSnapshot.packedTopControl : previousSnapshot.packedBottomControl;

    int carriedLines = 0;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const u32 currentMeta = lineMeta[y];
        const bool currentFullRegularCaptureLine =
            ((currentMeta >> 16u) & 0x3u) == 1u
            && (currentMeta & kMetaRegularCaptureUses3d) != 0u
            && (currentMeta & kMetaVramCaptureUses3d) == 0u;
        if (!currentFullRegularCaptureLine)
            continue;

        bool carriedLine = false;
        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 currentControlAlpha = control[index] >> 24u;
            const u32 currentCompMode = currentControlAlpha & 0xFu;
            const bool currentStructuredSlot = (currentControlAlpha & 0x40u) != 0u;
            const bool currentHasAbove = currentStructuredSlot && (currentControlAlpha & 0x80u) != 0u;
            if (currentCompMode != 7u || !currentStructuredSlot || currentHasAbove)
                continue;

            const u32 previousControlAlpha = previousControl[index] >> 24u;
            const u32 previousCompMode = previousControlAlpha & 0xFu;
            const bool previousStructuredSlot = (previousControlAlpha & 0x40u) != 0u;
            const bool previousStructuredAbove = previousStructuredSlot && (previousControlAlpha & 0x80u) != 0u;
            const bool previousStructured2DOnly = !previousStructuredSlot && (previousControlAlpha & 0x80u) != 0u;
            const bool previousProtectedBlack = (previousControlAlpha & 0x20u) != 0u;

            u32 overlayPixel = 0u;
            if (previousStructuredAbove
                && (packedPixelHasVisibleColor(previousPlane1[index])
                    || (previousProtectedBlack && packedPixelIsOpaqueBlack(previousPlane1[index]))))
            {
                overlayPixel = previousPlane1[index];
            }
            else if (packedPixelHasVisibleColor(previousPlane1[index])
                || (previousProtectedBlack && packedPixelIsOpaqueBlack(previousPlane1[index])))
            {
                overlayPixel = previousPlane1[index];
            }
            else if (previousStructured2DOnly
                && (packedPixelHasVisibleColor(previousPlane0[index])
                    || (previousProtectedBlack && packedPixelIsOpaqueBlack(previousPlane0[index]))))
            {
                overlayPixel = previousPlane0[index];
            }
            else if (previousCompMode == 7u && packedPixelHasVisibleColor(previousPlane1[index]))
            {
                overlayPixel = previousPlane1[index];
            }

            if (overlayPixel == 0u || overlayPixel == kPacked3dPlaceholder)
                continue;

            const bool protectedBlack = previousProtectedBlack || packedPixelIsOpaqueBlack(overlayPixel);
            plane1[index] = overlayPixel;
            control[index] =
                (control[index] & 0x00FFFFFFu)
                | ((currentCompMode | 0x40u | 0x80u | (protectedBlack ? 0x20u : 0u)) << 24u);
            carriedLine = true;
        }

        if (carriedLine)
            ++carriedLines;
    }
    return carriedLines;
}

// --- Lowres capture-image-only screen promotion (Sapphire
// promoteLowresCaptureImageToStructuredSlot).

void promoteLowresCaptureImageToStructuredSlot(
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* previousControl,
    bool allowTemporalContinuation,
    bool allowClass4VramAlternation,
    bool partialCapture3dMask,
    int ownRegularCaptureLineCount,
    int oppositeRegularCaptureLineCount,
    int oppositeVramCaptureLineCount) noexcept
{
    constexpr int kLineCount = static_cast<int>(SoftPackedFrameSnapshot::kLineCount);
    const bool ownFullScreenRegularCapture =
        !partialCapture3dMask
        && ownRegularCaptureLineCount > (kLineCount / 2)
        && oppositeVramCaptureLineCount == 0;
    if (ownRegularCaptureLineCount != 0 && !ownFullScreenRegularCapture)
        return;

    u32 structured2DOnlyPixels = 0;
    u32 structuredSlotPixels = 0;
    u32 plane1UsefulPixels = 0;
    u32 previousStructuredSlotPixels = 0;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const u32 meta = lineMeta[y];
        if (((meta >> 16u) & 0x3u) != 1u)
            return;

        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 controlAlpha = control[index] >> 24u;
            const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
            const bool structured2DOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
            if (structuredSlot) ++structuredSlotPixels;
            if (structured2DOnly) ++structured2DOnlyPixels;
            if (previousControl != nullptr && (((*previousControl)[index] >> 24u) & 0x40u) != 0u)
                ++previousStructuredSlotPixels;
            if (plane1[index] != 0u && plane1[index] != kPacked3dPlaceholder)
                ++plane1UsefulPixels;
        }
    }

    constexpr u32 screenPixels = static_cast<u32>(SoftPackedFrameSnapshot::kPixelCount);
    const bool currentCaptureAlternation =
        ownFullScreenRegularCapture
        || (oppositeRegularCaptureLineCount > (kLineCount / 2) && oppositeVramCaptureLineCount == 0)
        || (allowClass4VramAlternation
            && ownRegularCaptureLineCount == 0
            && oppositeRegularCaptureLineCount == 0
            && oppositeVramCaptureLineCount > (kLineCount / 2));
    const bool continuesPromotedCaptureImage =
        allowTemporalContinuation && previousStructuredSlotPixels > (screenPixels / 2u);
    if (!currentCaptureAlternation && !continuesPromotedCaptureImage)
        return;
    if (!ownFullScreenRegularCapture && structuredSlotPixels > (screenPixels / 8u))
        return;
    if (structured2DOnlyPixels < ((screenPixels * 3u) / 4u) || plane1UsefulPixels != 0u)
        return;

    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 controlAlpha = control[index] >> 24u;
            const bool structured2DOnly = (controlAlpha & 0x80u) != 0u && (controlAlpha & 0x40u) == 0u;
            const bool protectedBlack = (controlAlpha & 0x20u) != 0u;
            if (!structured2DOnly || protectedBlack)
                continue;
            if (plane0[index] == 0u || plane0[index] == kPacked3dPlaceholder)
                continue;

            const u32 compMode = controlAlpha & 0x0Fu;
            plane0[index] = 0u;
            plane1[index] = 0u;
            control[index] = (control[index] & 0x00FFFFFFu) | ((compMode | 0x40u) << 24u);
        }
    }
}

} // namespace

void MelonPrimeVulkanSnapshotBuilder::reset() noexcept
{
    previousSnapshot.clear();
    engineATop = {};
    engineABottom = {};
    lastValidTopScreenCapture3dDsFrame.fill(0);
    lastValidBottomScreenCapture3dDsFrame.fill(0);
    lastValidTopScreenResolvedPrimary.fill(0);
    lastValidBottomScreenResolvedPrimary.fill(0);
    lastValidTopScreenResolvedPrimaryLines.fill(0);
    lastValidBottomScreenResolvedPrimaryLines.fill(0);
    cachedAtypicalDisplayTopPrimary.fill(0);
    cachedAtypicalDisplayBottomPrimary.fill(0);
    cachedAtypicalDisplayTopPrimaryLines.fill(0);
    cachedAtypicalDisplayBottomPrimaryLines.fill(0);
    hasLastValidTopScreenCapture3dDsFrame = false;
    hasLastValidBottomScreenCapture3dDsFrame = false;
    vulkanRegularCaptureTransitionResyncPending = false;
    vulkanStructuredCaptureGateFrames = 0;
    framesSinceLastScreenSwapToggle = 1024;
    wasInAlternatingMode = false;
}

bool MelonPrimeVulkanSnapshotBuilder::takeRegularCaptureTransitionResyncRequest() noexcept
{
    const bool pending = vulkanRegularCaptureTransitionResyncPending;
    vulkanRegularCaptureTransitionResyncPending = false;
    return pending;
}

bool MelonPrimeVulkanSnapshotBuilder::build(
    const StructuredVulkanSnapshotSource& source,
    u64 frameId,
    SoftPackedFrameSnapshot& destination)
{
    if (source.frontBuffer < 0 || source.frontBuffer > 1 || source.generation == 0u)
        return false;
    for (std::size_t screen = 0; screen < 2u; ++screen)
    {
        if (source.lineMeta[screen] == nullptr || source.screenNeedsCapture3d[screen] == nullptr)
            return false;
        for (std::size_t plane = 0; plane < 3u; ++plane)
        {
            if (source.plane[screen][plane] == nullptr)
                return false;
        }
    }

    if (source.packedTop == nullptr || source.packedBottom == nullptr)
        return false;

    destination.clear();
    destination.frameId = frameId;
    destination.sourceGeneration = source.generation;
    destination.frontBufferLatched = source.frontBuffer;
    destination.captureScreenSwap = source.captureScreenSwap;
    destination.captureScreenSwapValid = source.captureScreenSwapValid;
    destination.screenSwapLatched = source.screenSwapLatched;
    destination.captureBackedClass4Only = source.captureBackedClass4Only;
    destination.captureBackedHasStructured2DSource = source.captureBackedHasStructured2DSource;

    constexpr std::size_t pixelBytes = SoftPackedFrameSnapshot::kPixelCount * sizeof(u32);
    constexpr std::size_t kPackedStride = SoftPackedFrameSnapshot::kScreenWidth * 3u + 1u;
    // The desktop producer has already performed Sapphire's structured-line
    // merge into authoritative physical Top/Bottom packed buffers. Latch that
    // result once; all code below is Sapphire's post-merge temporal repair.
    auto unpackPackedScreen =
            [](const u32* packed,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
                std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
                {
                    const std::size_t packedRow = y * kPackedStride;
                    const std::size_t snapshotRow = y * SoftPackedFrameSnapshot::kScreenWidth;
                    std::memcpy(
                        plane0.data() + snapshotRow,
                        packed + packedRow,
                        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
                    std::memcpy(
                        plane1.data() + snapshotRow,
                        packed + packedRow + SoftPackedFrameSnapshot::kScreenWidth,
                        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
                    std::memcpy(
                        control.data() + snapshotRow,
                        packed + packedRow + (SoftPackedFrameSnapshot::kScreenWidth * 2u),
                        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
                    lineMeta[y] = packed[packedRow + (SoftPackedFrameSnapshot::kScreenWidth * 3u)];
                }
        };
    unpackPackedScreen(
        source.packedTop,
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    unpackPackedScreen(
        source.packedBottom,
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);

    if (source.capture3dSourceLineValid != nullptr)
    {
        std::memcpy(
            destination.capture3dSourceLineValidMask.data(),
            source.capture3dSourceLineValid,
            destination.capture3dSourceLineValidMask.size() * sizeof(u8));
    }
    if (source.captureLineUses3dMask != nullptr)
    {
        std::memcpy(
            destination.captureLineUses3dMask.data(),
            source.captureLineUses3dMask,
            destination.captureLineUses3dMask.size() * sizeof(u8));
    }
    std::memcpy(
        destination.topScreenNeedsCapture3dMask.data(),
        source.screenNeedsCapture3d[0],
        destination.topScreenNeedsCapture3dMask.size() * sizeof(u8));
    std::memcpy(
        destination.bottomScreenNeedsCapture3dMask.data(),
        source.screenNeedsCapture3d[1],
        destination.bottomScreenNeedsCapture3dMask.size() * sizeof(u8));

    const bool screenSwapToggledThisFrame =
        previousSnapshot.valid
        && previousSnapshot.screenSwapLatched != destination.screenSwapLatched;
    // Desktop does not expose Sapphire's renderer-2D debug controls through
    // this split builder, so the production repair path is always active.
    constexpr bool renderer2dDebugControlsActive = false;

    if (destination.captureBackedHasStructured2DSource)
        vulkanStructuredCaptureGateFrames = 2;
    else if (vulkanStructuredCaptureGateFrames > 0)
        --vulkanStructuredCaptureGateFrames;

    const CaptureLineCounts preRepairTopLineCounts =
        countCaptureLineCounts(destination.packedTopLineMeta);
    const CaptureLineCounts preRepairBottomLineCounts =
        countCaptureLineCounts(destination.packedBottomLineMeta);
    const bool regularCaptureOwnershipResetAllowed =
        previousSnapshot.valid
        && previousSnapshot.screenSwapLatched == destination.screenSwapLatched;
    const bool frameHasVramCapture3d =
        preRepairTopLineCounts.vramCaptureLines > 0
        || preRepairBottomLineCounts.vramCaptureLines > 0;
    const bool topEnteredDominantRegularCapture =
        destination.captureBackedHasStructured2DSource
        && regularCaptureOwnershipResetAllowed
        && !frameHasVramCapture3d
        && preRepairTopLineCounts.regularCaptureLines
            > static_cast<int>(SoftPackedFrameSnapshot::kLineCount / 2u)
        && countCaptureLineCounts(previousSnapshot.packedTopLineMeta).regularCaptureLines == 0;
    const bool bottomEnteredDominantRegularCapture =
        destination.captureBackedHasStructured2DSource
        && regularCaptureOwnershipResetAllowed
        && !frameHasVramCapture3d
        && preRepairBottomLineCounts.regularCaptureLines
            > static_cast<int>(SoftPackedFrameSnapshot::kLineCount / 2u)
        && countCaptureLineCounts(previousSnapshot.packedBottomLineMeta).regularCaptureLines == 0;
    if (topEnteredDominantRegularCapture)
    {
        lastValidTopScreenResolvedPrimaryLines.fill(0);
        hasLastValidTopScreenCapture3dDsFrame = false;
    }
    if (bottomEnteredDominantRegularCapture)
    {
        lastValidBottomScreenResolvedPrimaryLines.fill(0);
        hasLastValidBottomScreenCapture3dDsFrame = false;
    }
    if (topEnteredDominantRegularCapture || bottomEnteredDominantRegularCapture)
        vulkanRegularCaptureTransitionResyncPending = true;

    if (source.captureBackedFullClass0AlternatingCapture)
    {
        const auto promoteVramDisplayCaptureLines =
            [](std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                int vramDisplayLines = 0;
                for (u32 meta : lineMeta)
                    vramDisplayLines += ((meta >> 16u) & 0x3u) == 2u ? 1 : 0;
                if (vramDisplayLines <= static_cast<int>(SoftPackedFrameSnapshot::kLineCount / 2u))
                    return;
                for (u32& meta : lineMeta)
                {
                    if (((meta >> 16u) & 0x3u) == 2u)
                        meta |= kMetaVramCaptureUses3d;
                }
            };
        promoteVramDisplayCaptureLines(destination.packedTopLineMeta);
        promoteVramDisplayCaptureLines(destination.packedBottomLineMeta);
    }

    // source.plane[0/1] is already physical Top/Bottom from GPU_Soft scanline
    // routing. Do NOT merge Engine A/B shadow planes back onto Top/Bottom using
    // the frame-level physicalScreenSwap bit — that second routing swaps whole
    // 2D screens. Keep physical planes authoritative (Sapphire contract).

    // Capture metadata normalization on physical lineMeta only.
    {
        int capture3dMaskLines = 0;
        for (u8 uses3d : destination.captureLineUses3dMask)
            capture3dMaskLines += uses3d != 0u ? 1 : 0;
        const bool partialCapture3dMask =
            (capture3dMaskLines > 0
                && capture3dMaskLines < static_cast<int>(SoftPackedFrameSnapshot::kLineCount))
            || (source.structuredCopyLines > 0u
                && source.structuredCopyLines < SoftPackedFrameSnapshot::kLineCount);

        const CaptureLineCounts topLineCounts = countCaptureLineCounts(destination.packedTopLineMeta);
        const CaptureLineCounts bottomLineCounts = countCaptureLineCounts(destination.packedBottomLineMeta);
        normalizeCaptureLineMeta(
            destination.packedTopLineMeta,
            partialCapture3dMask,
            topLineCounts.regularCaptureLines,
            topLineCounts.vramCaptureLines,
            bottomLineCounts.vramCaptureLines,
            destination.captureBackedHasStructured2DSource
                || source.captureBackedFullClass0AlternatingCapture);
        normalizeCaptureLineMeta(
            destination.packedBottomLineMeta,
            partialCapture3dMask,
            bottomLineCounts.regularCaptureLines,
            bottomLineCounts.vramCaptureLines,
            topLineCounts.vramCaptureLines,
            destination.captureBackedHasStructured2DSource
                || source.captureBackedFullClass0AlternatingCapture);
    }

    // Sapphire recomputes these capture-line counts once, immediately after
    // metadata normalization and before the temporal carries / Engine-A
    // whole-screen cache replacement below. carryPreviousLatchedScreenLines
    // and applyCachedEngineASnapshot both rewrite the high 16 bits of
    // lineMeta (display mode + capture flags), so recomputing after those
    // steps (as opposed to here) yields different gate values downstream in
    // promoteLowresCaptureImageToStructuredSlot / repairTopFullRegularCapture2DBaseFromPrevious.
    const bool partialCapture3dMask = [&] {
        int capture3dMaskLines = 0;
        for (u8 uses3d : destination.captureLineUses3dMask)
            capture3dMaskLines += uses3d != 0u ? 1 : 0;
        return (capture3dMaskLines > 0
                && capture3dMaskLines < static_cast<int>(SoftPackedFrameSnapshot::kLineCount))
            || (source.structuredCopyLines > 0u
                && source.structuredCopyLines < SoftPackedFrameSnapshot::kLineCount);
    }();
    const CaptureLineCounts topLineCounts = countCaptureLineCounts(destination.packedTopLineMeta);
    const CaptureLineCounts bottomLineCounts = countCaptureLineCounts(destination.packedBottomLineMeta);
    const int topRegularCaptureLineCount = topLineCounts.regularCaptureLines;
    const int bottomRegularCaptureLineCount = bottomLineCounts.regularCaptureLines;
    const int topVramCaptureLineCount = topLineCounts.vramCaptureLines;
    const int bottomVramCaptureLineCount = bottomLineCounts.vramCaptureLines;

    const int carriedTopLatchedLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousLatchedScreenLines(
            previousSnapshot,
            true,
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
    const int carriedBottomLatchedLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousLatchedScreenLines(
            previousSnapshot,
            false,
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);
    const int carriedTopTemporalOverlayLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousTemporalOverlayPixels(
            previousSnapshot,
            true,
            source.captureBackedPartialClass0Only,
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
    const int carriedBottomTemporalOverlayLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousTemporalOverlayPixels(
            previousSnapshot,
            false,
            source.captureBackedPartialClass0Only,
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);
    int carriedTopFullRegularComp7OverlayLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousFullRegularComp7Overlay(
            previousSnapshot,
            true,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
    int carriedBottomFullRegularComp7OverlayLines = renderer2dDebugControlsActive
        ? 0
        : carryPreviousFullRegularComp7Overlay(
            previousSnapshot,
            false,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);

    if (screenSwapToggledThisFrame)
        framesSinceLastScreenSwapToggle = 0;
    else if (framesSinceLastScreenSwapToggle < 1024)
        ++framesSinceLastScreenSwapToggle;
    const bool isInAlternatingMode = framesSinceLastScreenSwapToggle <= 1;
    if (isInAlternatingMode != wasInAlternatingMode)
    {
        engineATop.valid = false;
        engineABottom.valid = false;
    }
    wasInAlternatingMode = isInAlternatingMode;

    {
        const bool engineAOnTop = destination.screenSwapLatched;
        const bool captureBackedHasStructured2DSource =
            destination.captureBackedHasStructured2DSource;

        auto screenHasMeaningfulContent =
            [](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0) {
                constexpr std::size_t kMinVisiblePixels =
                    SoftPackedFrameSnapshot::kPixelCount / 32u;
                std::size_t visiblePixels = 0;
                for (std::size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; ++i)
                {
                    if (packedPixelHasVisibleColor(plane0[i]))
                    {
                        ++visiblePixels;
                        if (visiblePixels >= kMinVisiblePixels)
                            return true;
                    }
                }
                return false;
            };
        auto screenHasExplicitCurrentContent =
            [](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0) {
                constexpr std::size_t kMinUsefulPixels =
                    SoftPackedFrameSnapshot::kPixelCount / 32u;
                std::size_t usefulPixels = 0;
                for (std::size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; ++i)
                {
                    const u32 pixel = plane0[i];
                    if (pixel != 0u && pixel != kPacked3dPlaceholder)
                    {
                        ++usefulPixels;
                        if (usefulPixels >= kMinUsefulPixels)
                            return true;
                    }
                }
                return false;
            };
        auto screenHasExplicitCompositedContent =
            [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1) {
                return screenHasExplicitCurrentContent(plane0)
                    || screenHasExplicitCurrentContent(plane1);
            };
        auto screenUses3dCaptureMeta =
            [](const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                for (u32 meta : lineMeta)
                {
                    if ((meta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d)) != 0u)
                        return true;
                }
                return false;
            };
        auto screenIsScreenWideCaptureBackedComp4 =
            [](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                int captureBackedComp4Lines = 0;
                for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
                {
                    const u32 meta = lineMeta[y];
                    if ((meta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d)) != 0u)
                        return false;
                    const u32 displayMode = (meta >> 16u) & 0x3u;
                    const bool lineUses3d =
                        (meta & (kMetaRegularCaptureUses3d
                            | kMetaVramCaptureUses3d)) != 0u;
                    if (displayMode != 1u || !lineUses3d)
                        continue;
                    bool lineHasCaptureBackedComp4 = false;
                    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
                    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
                    {
                        const std::size_t index = rowBase + x;
                        const u32 compMode = (control[index] >> 24u) & 0xFu;
                        if (compMode == 4u
                            && plane0[index] == kPacked3dPlaceholder
                            && plane1[index] == kPacked3dPlaceholder)
                        {
                            lineHasCaptureBackedComp4 = true;
                            break;
                        }
                    }
                    if (lineHasCaptureBackedComp4)
                        ++captureBackedComp4Lines;
                }
                return captureBackedComp4Lines
                    > static_cast<int>(SoftPackedFrameSnapshot::kLineCount / 2u);
            };
        auto screenHasStructured2DOnlyContent =
            [](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control) {
                constexpr std::size_t kMinVisiblePixels =
                    SoftPackedFrameSnapshot::kPixelCount / 128u;
                std::size_t visiblePixels = 0;
                for (std::size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; ++i)
                {
                    const u32 controlAlpha = control[i] >> 24u;
                    const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
                    const bool structured2DOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
                    if (!structured2DOnly || !packedPixelHasVisibleColor(plane0[i]))
                        continue;
                    ++visiblePixels;
                    if (visiblePixels >= kMinVisiblePixels)
                        return true;
                }
                return false;
            };
        auto applyCachedEngineASnapshot =
            [](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane0,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane1,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetControl,
                std::array<u32, SoftPackedFrameSnapshot::kLineCount>& targetLineMeta,
                const EnginePhaseCache& cached) {
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount> currentLineMeta = targetLineMeta;
                targetPlane0 = cached.plane0;
                targetPlane1 = cached.plane1;
                targetControl = cached.control;
                targetLineMeta = cached.lineMeta;
                for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
                    targetLineMeta[y] = (targetLineMeta[y] & 0xFFFF0000u) | (currentLineMeta[y] & 0x0000FFFFu);
            };
        auto storeEngineACache =
            [](EnginePhaseCache& cache,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                cache.plane0 = plane0;
                cache.plane1 = plane1;
                cache.control = control;
                cache.lineMeta = lineMeta;
                cache.valid = true;
            };

        if (engineAOnTop)
        {
            if (screenHasMeaningfulContent(destination.packedTopPlane0)
                || screenIsScreenWideCaptureBackedComp4(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1,
                    destination.packedTopControl,
                    destination.packedTopLineMeta))
            {
                storeEngineACache(
                    engineATop,
                    destination.packedTopPlane0,
                    destination.packedTopPlane1,
                    destination.packedTopControl,
                    destination.packedTopLineMeta);
            }

            const bool currentTopHasExplicitCompositedContent =
                screenHasExplicitCompositedContent(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1);
            const bool cachedTopHasStructured2DOnlyContent =
                screenHasStructured2DOnlyContent(engineATop.plane0, engineATop.control);
            if (engineATop.valid
                && !currentTopHasExplicitCompositedContent
                && screenUses3dCaptureMeta(destination.packedTopLineMeta)
                && cachedTopHasStructured2DOnlyContent)
            {
                applyCachedEngineASnapshot(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1,
                    destination.packedTopControl,
                    destination.packedTopLineMeta,
                    engineATop);
            }

            const bool cachedBottomIsScreenWideCaptureBackedComp4 =
                screenIsScreenWideCaptureBackedComp4(
                    engineABottom.plane0,
                    engineABottom.plane1,
                    engineABottom.control,
                    engineABottom.lineMeta);
            const bool currentBottomHasExplicitContent =
                screenHasExplicitCompositedContent(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1);
            const bool cachedBottomHasStructured2DOnlyContent =
                screenHasStructured2DOnlyContent(
                    engineABottom.plane0,
                    engineABottom.control);
            const bool shouldReplaceBottom = engineABottom.valid
                && ((!isInAlternatingMode && !currentBottomHasExplicitContent)
                    || (isInAlternatingMode
                        && (cachedBottomIsScreenWideCaptureBackedComp4
                            || (!captureBackedHasStructured2DSource
                                && !currentBottomHasExplicitContent
                                && cachedBottomHasStructured2DOnlyContent))));
            if (shouldReplaceBottom)
            {
                applyCachedEngineASnapshot(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1,
                    destination.packedBottomControl,
                    destination.packedBottomLineMeta,
                    engineABottom);
            }
        }
        else
        {
            if (screenHasMeaningfulContent(destination.packedBottomPlane0)
                || screenIsScreenWideCaptureBackedComp4(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1,
                    destination.packedBottomControl,
                    destination.packedBottomLineMeta))
            {
                storeEngineACache(
                    engineABottom,
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1,
                    destination.packedBottomControl,
                    destination.packedBottomLineMeta);
            }

            const bool cachedTopIsScreenWideCaptureBackedComp4 =
                screenIsScreenWideCaptureBackedComp4(
                    engineATop.plane0,
                    engineATop.plane1,
                    engineATop.control,
                    engineATop.lineMeta);
            const bool currentTopHasExplicitContent =
                screenHasExplicitCompositedContent(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1);
            const bool cachedTopHasStructured2DOnlyContent =
                screenHasStructured2DOnlyContent(
                    engineATop.plane0,
                    engineATop.control);
            const bool shouldReplaceTop = engineATop.valid
                && ((!isInAlternatingMode && !currentTopHasExplicitContent)
                    || (isInAlternatingMode
                        && (cachedTopIsScreenWideCaptureBackedComp4
                            || (!captureBackedHasStructured2DSource
                                && !currentTopHasExplicitContent
                                && cachedTopHasStructured2DOnlyContent))));
            if (shouldReplaceTop)
            {
                applyCachedEngineASnapshot(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1,
                    destination.packedTopControl,
                    destination.packedTopLineMeta,
                    engineATop);
            }

            const bool currentBottomHasExplicitCompositedContent =
                screenHasExplicitCompositedContent(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1);
            const bool cachedBottomHasStructured2DOnlyContent =
                screenHasStructured2DOnlyContent(engineABottom.plane0, engineABottom.control);
            if (engineABottom.valid
                && !currentBottomHasExplicitCompositedContent
                && screenUses3dCaptureMeta(destination.packedBottomLineMeta)
                && cachedBottomHasStructured2DOnlyContent)
            {
                applyCachedEngineASnapshot(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1,
                    destination.packedBottomControl,
                    destination.packedBottomLineMeta,
                    engineABottom);
            }
        }
    }

    // The producer supplies a complete immutable structured frame. These names
    // deliberately mirror Sapphire's latch locals so the post-merge block below
    // remains a line-for-line semantic port.
    constexpr bool hasStructuredVulkan2D = true;
    const bool captureBackedClass4Only = destination.captureBackedClass4Only;
    const bool captureBackedHasStructured2DSource = destination.captureBackedHasStructured2DSource;
    const bool captureBackedFullClass0AlternatingCapture =
        source.captureBackedFullClass0AlternatingCapture;
    const u32* structuredTopPlane0 = source.plane[0][0];
    const u32* structuredTopPlane1 = source.plane[0][1];
    const u32* structuredTopControl = source.plane[0][2];
    const u32* structuredBottomPlane0 = source.plane[1][0];
    const u32* structuredBottomPlane1 = source.plane[1][1];
    const u32* structuredBottomControl = source.plane[1][2];

    promoteLowresCaptureImageToStructuredSlot(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta,
        previousSnapshot.valid ? &previousSnapshot.packedTopControl : nullptr,
        isInAlternatingMode,
        captureBackedClass4Only && screenSwapToggledThisFrame,
        partialCapture3dMask,
        topRegularCaptureLineCount,
        bottomRegularCaptureLineCount,
        bottomVramCaptureLineCount);
    promoteLowresCaptureImageToStructuredSlot(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta,
        previousSnapshot.valid ? &previousSnapshot.packedBottomControl : nullptr,
        isInAlternatingMode,
        captureBackedClass4Only && screenSwapToggledThisFrame,
        partialCapture3dMask,
        bottomRegularCaptureLineCount,
        topRegularCaptureLineCount,
        topVramCaptureLineCount);



    int preservedTopFullRegularProtectedBlackPixels = 0;
    auto repairTopFullRegularCapture2DBaseFromPrevious =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& previousPlane0,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& previousLineMeta,
            int regularCaptureLineCount,
            int vramCaptureLineCount) {
            if (!previousSnapshot.valid)
                return 0;
            if (!isInAlternatingMode)
                return 0;
            if (regularCaptureLineCount != SoftPackedFrameSnapshot::kScreenHeight)
                return 0;
            if (vramCaptureLineCount != 0)
                return 0;

            size_t regularComp7Pixels = 0;
            size_t regularStructuredAbovePixels = 0;
            size_t regularPixels = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const bool regularCaptureLine =
                    ((meta >> 16u) & 0x3u) == 1u
                    && (meta & kMetaRegularCaptureUses3d) != 0u
                    && (meta & kMetaVramCaptureUses3d) == 0u;
                if (!regularCaptureLine)
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 controlAlpha = control[index] >> 24u;
                    const u32 compMode = controlAlpha & 0xFu;
                    regularPixels++;
                    if (compMode == 7u)
                        regularComp7Pixels++;
                    if ((controlAlpha & 0x80u) != 0u)
                        regularStructuredAbovePixels++;
                }
            }

            if (regularPixels == 0)
                return 0;
            if (regularComp7Pixels < ((regularPixels * 95u) / 100u))
                return 0;
            if (regularStructuredAbovePixels > (regularPixels / 16u))
                return 0;

            size_t previousUsefulLines = 0;
            size_t previousRegularCaptureLines = 0;
            size_t previousWideBlackLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 previousMeta = previousLineMeta[static_cast<size_t>(y)];
                if (((previousMeta >> 16u) & 0x3u) == 1u
                    && (previousMeta & kMetaRegularCaptureUses3d) != 0u)
                {
                    previousRegularCaptureLines++;
                }
                if (packedResolvedLineHasAnyUsefulPixel(previousPlane0, y))
                    previousUsefulLines++;
                if (packedResolvedLineIsMostlyOpaqueBlack(previousPlane0, y))
                    previousWideBlackLines++;
            }

            if (previousUsefulLines <= (SoftPackedFrameSnapshot::kScreenHeight / 2))
                return 0;
            if (previousWideBlackLines >= previousUsefulLines)
                return 0;
            if (previousRegularCaptureLines > (SoftPackedFrameSnapshot::kScreenHeight / 2))
                return 0;

            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount> currentPlane0 = plane0;
            plane0 = previousPlane0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const bool regularCaptureLine =
                    ((meta >> 16u) & 0x3u) == 1u
                    && (meta & kMetaRegularCaptureUses3d) != 0u
                    && (meta & kMetaVramCaptureUses3d) == 0u;
                if (!regularCaptureLine)
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 controlAlpha = control[index] >> 24u;
                    const u32 compMode = controlAlpha & 0xFu;
                    const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
                    const bool structured2DOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
                    const bool protectedBlack2D = structured2DOnly && (controlAlpha & 0x20u) != 0u;
                    if (compMode == 7u
                        && protectedBlack2D
                        && currentPlane0[index] != 0u
                        && currentPlane0[index] != kPacked3dPlaceholder)
                    {
                        plane0[index] = currentPlane0[index];
                        preservedTopFullRegularProtectedBlackPixels++;
                    }
                }
            }
            return 1;
        };

    const bool topFullRegularCaptureWithBottomCompMode2Slot =
        !renderer2dDebugControlsActive
        && isInAlternatingMode
        && topRegularCaptureLineCount == SoftPackedFrameSnapshot::kScreenHeight
        && topVramCaptureLineCount == 0
        && bottomRegularCaptureLineCount == 0
        && bottomVramCaptureLineCount == 0
        && packedScreenUsesFullStructuredCompMode2Slot(
            destination.packedBottomControl,
            destination.packedBottomLineMeta);
    const int repairedTopFullRegular2DBase = renderer2dDebugControlsActive || topFullRegularCaptureWithBottomCompMode2Slot
        ? 0
        : repairTopFullRegularCapture2DBaseFromPrevious(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta,
            previousSnapshot.packedTopPlane0,
            previousSnapshot.packedTopLineMeta,
            topRegularCaptureLineCount,
            topVramCaptureLineCount);
    if (!renderer2dDebugControlsActive && repairedTopFullRegular2DBase > 0)
    {
        carriedTopFullRegularComp7OverlayLines += carryPreviousFullRegularComp7Overlay(
            previousSnapshot,
            true,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
    }

    // Sapphire forces the bottom-most strip of a regular-capture-dominant
    // bottom screen (with no top-screen capture activity at all) to
    // protected opaque black. Without this, that strip keeps whatever the
    // carry/Engine-A repair pipeline produced, which can surface stale or
    // transparent content on the touch screen.
    const bool bottomOnlyRegularCaptureDominant =
        bottomRegularCaptureLineCount > static_cast<int>(SoftPackedFrameSnapshot::kScreenHeight / 2u)
        && topRegularCaptureLineCount == 0
        && topVramCaptureLineCount == 0
        && bottomVramCaptureLineCount == 0;
    if (hasStructuredVulkan2D && partialCapture3dMask && bottomOnlyRegularCaptureDominant)
    {
        constexpr u32 protectedBlackControl = (0x80u | 0x20u) << 24u;
        for (std::size_t y = 171; y < SoftPackedFrameSnapshot::kScreenHeight; ++y)
        {
            u32& lineMeta = destination.packedBottomLineMeta[y];
            lineMeta = (lineMeta & ~0x00030000u) | (1u << 16u);
            const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
            for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
            {
                const std::size_t index = rowBase + x;
                destination.packedBottomPlane0[index] = 0xFF000000u;
                destination.packedBottomPlane1[index] = 0u;
                destination.packedBottomControl[index] = protectedBlackControl;
            }
        }
    }

    int carriedTopVramPairLines = 0;
    int carriedBottomVramPairLines = 0;
    int carriedTopCurrentStructuredVram2DPairLines = 0;
    int carriedBottomCurrentStructuredVram2DPairLines = 0;
    if (hasStructuredVulkan2D
        && captureBackedClass4Only
        && !renderer2dDebugControlsActive
        && previousSnapshot.valid
        && previousSnapshot.screenSwapLatched == destination.screenSwapLatched)
    {
        auto countSnapshotCaptureLines =
            [](const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
                u32 flag,
                u32 requiredDisplayMode) {
                int count = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    const u32 meta = lineMeta[static_cast<size_t>(y)];
                    const u32 displayMode = (meta >> 16u) & 0x3u;
                    if (displayMode == requiredDisplayMode && (meta & flag) != 0u)
                        count++;
                }
                return count;
            };
        auto countSnapshotAnyCaptureLines =
            [](const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
                int count = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    const u32 meta = lineMeta[static_cast<size_t>(y)];
                    if ((meta & (kMetaRegularCaptureUses3d
                            | kMetaVramCaptureUses3d)) != 0u)
                    {
                        count++;
                    }
                }
                return count;
            };
        auto countSnapshotDisplayModeLines =
            [](const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
                u32 requiredDisplayMode) {
                int count = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    const u32 meta = lineMeta[static_cast<size_t>(y)];
                    const u32 displayMode = (meta >> 16u) & 0x3u;
                    if (displayMode == requiredDisplayMode)
                        count++;
                }
                return count;
            };

        const int previousTopVramCaptureLineCount = countSnapshotCaptureLines(
            previousSnapshot.packedTopLineMeta,
            kMetaVramCaptureUses3d,
            2u);
        const int previousBottomVramCaptureLineCount = countSnapshotCaptureLines(
            previousSnapshot.packedBottomLineMeta,
            kMetaVramCaptureUses3d,
            2u);
        const int previousTopAnyCaptureLineCount = countSnapshotAnyCaptureLines(
            previousSnapshot.packedTopLineMeta);
        const int previousBottomAnyCaptureLineCount = countSnapshotAnyCaptureLines(
            previousSnapshot.packedBottomLineMeta);
        const int currentTopAnyCaptureLineCount = countSnapshotAnyCaptureLines(
            destination.packedTopLineMeta);
        const int currentBottomAnyCaptureLineCount = countSnapshotAnyCaptureLines(
            destination.packedBottomLineMeta);

        const bool topVramCaptureAlternates =
            (topVramCaptureLineCount > (SoftPackedFrameSnapshot::kScreenHeight / 2)
                && previousTopAnyCaptureLineCount == 0)
            || (previousTopVramCaptureLineCount > (SoftPackedFrameSnapshot::kScreenHeight / 2)
                && currentTopAnyCaptureLineCount == 0);
        const bool bottomVramCaptureAlternates =
            (bottomVramCaptureLineCount > (SoftPackedFrameSnapshot::kScreenHeight / 2)
                && previousBottomAnyCaptureLineCount == 0)
            || (previousBottomVramCaptureLineCount > (SoftPackedFrameSnapshot::kScreenHeight / 2)
                && currentBottomAnyCaptureLineCount == 0);
        auto copyCurrentStructuredVram2DPair =
            [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
                const u32* structuredPlane0,
                const u32* structuredPlane1,
                const u32* structuredControl,
                bool screenVramCaptureAlternates,
                int currentAnyCaptureLineCount,
                int previousVramCaptureLineCount) {
                if (!screenVramCaptureAlternates
                    || currentAnyCaptureLineCount != 0
                    || previousVramCaptureLineCount <= (SoftPackedFrameSnapshot::kScreenHeight / 2)
                    || countSnapshotDisplayModeLines(lineMeta, 2u) <= (SoftPackedFrameSnapshot::kScreenHeight / 2))
                {
                    return 0;
                }

                int carriedLines = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    const u32 currentMeta = lineMeta[static_cast<size_t>(y)];
                    const bool currentLineIsUnmarkedVramDisplay =
                        ((currentMeta >> 16u) & 0x3u) == 2u
                        && (currentMeta & (kMetaRegularCaptureUses3d
                            | kMetaVramCaptureUses3d)) == 0u;
                    const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                    if (!currentLineIsUnmarkedVramDisplay
                        || !structuredLineHasPayload(structuredPlane0, structuredPlane1, structuredControl, rowBase))
                    {
                        continue;
                    }

                    copyStructuredLine(
                        plane0,
                        plane1,
                        control,
                        structuredPlane0,
                        structuredPlane1,
                        structuredControl,
                        rowBase);
                    carriedLines++;
                }

                return carriedLines;
            };

        auto carryPreviousVramCapturePair =
            [&](std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& previousLineMeta,
                bool screenVramCaptureAlternates,
                int currentAnyCaptureLineCount,
                int previousVramCaptureLineCount) {
                if (!screenVramCaptureAlternates
                    || currentAnyCaptureLineCount != 0
                    || previousVramCaptureLineCount <= (SoftPackedFrameSnapshot::kScreenHeight / 2)
                    || countSnapshotDisplayModeLines(lineMeta, 2u) <= (SoftPackedFrameSnapshot::kScreenHeight / 2))
                {
                    return 0;
                }

                int carriedLines = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    const u32 currentMeta = lineMeta[static_cast<size_t>(y)];
                    const u32 previousMeta = previousLineMeta[static_cast<size_t>(y)];
                    const bool currentLineIsUnmarkedVramDisplay =
                        ((currentMeta >> 16u) & 0x3u) == 2u
                        && (currentMeta & (kMetaRegularCaptureUses3d
                            | kMetaVramCaptureUses3d)) == 0u;
                    const bool previousLineUsesVramCapture =
                        ((previousMeta >> 16u) & 0x3u) == 2u
                        && (previousMeta & kMetaVramCaptureUses3d) != 0u;
                    if (!currentLineIsUnmarkedVramDisplay || !previousLineUsesVramCapture)
                        continue;

                    lineMeta[static_cast<size_t>(y)] =
                        (previousMeta & 0xFFFF0000u)
                        | (currentMeta & 0x0000FFFFu);
                    carriedLines++;
                }
                return carriedLines;
            };

        carriedTopCurrentStructuredVram2DPairLines = copyCurrentStructuredVram2DPair(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta,
            structuredTopPlane0,
            structuredTopPlane1,
            structuredTopControl,
            topVramCaptureAlternates,
            currentTopAnyCaptureLineCount,
            previousTopVramCaptureLineCount);
        carriedBottomCurrentStructuredVram2DPairLines = copyCurrentStructuredVram2DPair(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta,
            structuredBottomPlane0,
            structuredBottomPlane1,
            structuredBottomControl,
            bottomVramCaptureAlternates,
            currentBottomAnyCaptureLineCount,
            previousBottomVramCaptureLineCount);
        carriedTopVramPairLines = carryPreviousVramCapturePair(
            destination.packedTopLineMeta,
            previousSnapshot.packedTopLineMeta,
            topVramCaptureAlternates,
            currentTopAnyCaptureLineCount,
            previousTopVramCaptureLineCount);
        carriedBottomVramPairLines = carryPreviousVramCapturePair(
            destination.packedBottomLineMeta,
            previousSnapshot.packedBottomLineMeta,
            bottomVramCaptureAlternates,
            currentBottomAnyCaptureLineCount,
            previousBottomVramCaptureLineCount);
    }

    destination.topScreenStats = collectPackedScreenStatsFromSnapshot(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    destination.bottomScreenStats = collectPackedScreenStatsFromSnapshot(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);

    auto updateAtypicalDisplayPrimaryCache =
        [](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const SoftPackedScreenStats& stats,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* capture3dSource,
            std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& cachedPrimary,
            std::array<u8, SoftPackedFrameSnapshot::kLineCount>& cachedPrimaryLines) {
            const bool fullStructuredSlot = softPackedScreenUsesFullStructuredSlotDisplay(stats);
            const bool regularStructured3dCapture = softPackedScreenUsesRegularStructured3dCaptureSlot(stats);
            if (!fullStructuredSlot && !regularStructured3dCapture)
            {
                return;
            }

            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                const u32* source = nullptr;
                if (packedResolvedLineHasAnyUsefulPixel(plane0, y))
                    source = plane0.data() + rowBase;
                else if (regularStructured3dCapture
                    && capture3dSource != nullptr
                    && packedResolvedLineHasAnyUsefulPixel(*capture3dSource, y))
                {
                    source = capture3dSource->data() + rowBase;
                }
                if (source == nullptr)
                    continue;

                std::memcpy(
                    cachedPrimary.data() + rowBase,
                    source,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                cachedPrimaryLines[static_cast<size_t>(y)] = 1u;
            }
        };
    if (!renderer2dDebugControlsActive)
    {
        updateAtypicalDisplayPrimaryCache(
            destination.packedTopPlane0,
            destination.topScreenStats,
            destination.hasCapture3dSource ? &destination.capture3dSourceDsFrame : nullptr,
            cachedAtypicalDisplayTopPrimary,
            cachedAtypicalDisplayTopPrimaryLines);
        updateAtypicalDisplayPrimaryCache(
            destination.packedBottomPlane0,
            destination.bottomScreenStats,
            destination.hasCapture3dSource ? &destination.capture3dSourceDsFrame : nullptr,
            cachedAtypicalDisplayBottomPrimary,
            cachedAtypicalDisplayBottomPrimaryLines);
    }

    int carriedTopEmptyDisplay2dPairLines = 0;
    int carriedBottomEmptyDisplay2dPairLines = 0;
    int carriedTopAtypicalDisplayPrimaryLines = 0;
    int carriedBottomAtypicalDisplayPrimaryLines = 0;
    const bool topDisplayCaptureBottomDisplay =
        destination.topScreenStats.DisplayModeCounts[2] > (SoftPackedFrameSnapshot::kScreenHeight / 2u)
        && destination.bottomScreenStats.DisplayModeCounts[1] > (SoftPackedFrameSnapshot::kScreenHeight / 2u);
    const bool bottomDisplayCaptureTopDisplay =
        destination.bottomScreenStats.DisplayModeCounts[2] > (SoftPackedFrameSnapshot::kScreenHeight / 2u)
        && destination.topScreenStats.DisplayModeCounts[1] > (SoftPackedFrameSnapshot::kScreenHeight / 2u);
    if (topDisplayCaptureBottomDisplay || bottomDisplayCaptureTopDisplay)
    {
        auto applyCachedScreenSnapshot =
            [](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane0,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane1,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetControl,
                std::array<u32, SoftPackedFrameSnapshot::kLineCount>& targetLineMeta,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& cachedPlane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& cachedPlane1,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& cachedControl,
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& cachedLineMeta) {
                const std::array<u32, SoftPackedFrameSnapshot::kLineCount> currentLineMeta = targetLineMeta;
                targetPlane0 = cachedPlane0;
                targetPlane1 = cachedPlane1;
                targetControl = cachedControl;
                targetLineMeta = cachedLineMeta;

                for (size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; y++)
                    targetLineMeta[y] = (targetLineMeta[y] & 0xFFFF0000u) | (currentLineMeta[y] & 0x0000FFFFu);
            };

        const bool topEmptyBottom2dOnly =
            softPackedScreenUsesEmptyDisplayCapture(destination.topScreenStats)
            && softPackedScreenUsesFullStructured2dOnlyDisplay(destination.bottomScreenStats);
        const bool bottomEmptyTop2dOnly =
            softPackedScreenUsesEmptyDisplayCapture(destination.bottomScreenStats)
            && softPackedScreenUsesFullStructured2dOnlyDisplay(destination.topScreenStats);
        const bool top2dOnlyBottomEmpty =
            softPackedScreenUsesFullStructured2dOnlyDisplay(destination.topScreenStats)
            && softPackedScreenUsesEmptyDisplayCapture(destination.bottomScreenStats);
        const bool bottom2dOnlyTopEmpty =
            softPackedScreenUsesFullStructured2dOnlyDisplay(destination.bottomScreenStats)
            && softPackedScreenUsesEmptyDisplayCapture(destination.topScreenStats);
        const bool bottomEmptyTopRegular3dCapture =
            softPackedScreenUsesEmptyDisplayCapture(destination.bottomScreenStats)
            && softPackedScreenUsesRegularStructured3dCaptureSlot(destination.topScreenStats);
        auto carryAtypicalDisplayPrimary =
            [](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane0,
                const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& cachedPrimary,
                const std::array<u8, SoftPackedFrameSnapshot::kLineCount>& cachedPrimaryLines) {
                int carriedLines = 0;
                for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
                {
                    if (cachedPrimaryLines[static_cast<size_t>(y)] == 0u)
                        continue;
                    if (packedResolvedLineHasAnyUsefulPixel(targetPlane0, y))
                        continue;

                    const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                    std::memcpy(
                        targetPlane0.data() + rowBase,
                        cachedPrimary.data() + rowBase,
                        static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                    carriedLines++;
                }

                return carriedLines;
            };
        if ((topEmptyBottom2dOnly || top2dOnlyBottomEmpty)
            && lineMaskHasAnyValidLine(cachedAtypicalDisplayTopPrimaryLines))
        {
            carriedTopAtypicalDisplayPrimaryLines = carryAtypicalDisplayPrimary(
                destination.packedTopPlane0,
                cachedAtypicalDisplayTopPrimary,
                cachedAtypicalDisplayTopPrimaryLines);
        }
        if ((bottomEmptyTop2dOnly || bottom2dOnlyTopEmpty || bottomEmptyTopRegular3dCapture)
            && lineMaskHasAnyValidLine(cachedAtypicalDisplayBottomPrimaryLines))
        {
            carriedBottomAtypicalDisplayPrimaryLines = carryAtypicalDisplayPrimary(
                destination.packedBottomPlane0,
                cachedAtypicalDisplayBottomPrimary,
                cachedAtypicalDisplayBottomPrimaryLines);
        }
        if (topEmptyBottom2dOnly && engineATop.valid)
        {
            applyCachedScreenSnapshot(
                destination.packedTopPlane0,
                destination.packedTopPlane1,
                destination.packedTopControl,
                destination.packedTopLineMeta,
                engineATop.plane0,
                engineATop.plane1,
                engineATop.control,
                engineATop.lineMeta);
            carriedTopEmptyDisplay2dPairLines = SoftPackedFrameSnapshot::kScreenHeight;
        }
        if (bottomEmptyTop2dOnly && engineABottom.valid)
        {
            applyCachedScreenSnapshot(
                destination.packedBottomPlane0,
                destination.packedBottomPlane1,
                destination.packedBottomControl,
                destination.packedBottomLineMeta,
                engineABottom.plane0,
                engineABottom.plane1,
                engineABottom.control,
                engineABottom.lineMeta);
            carriedBottomEmptyDisplay2dPairLines = SoftPackedFrameSnapshot::kScreenHeight;
        }
        if (carriedTopEmptyDisplay2dPairLines > 0
            || carriedBottomEmptyDisplay2dPairLines > 0
            || carriedTopAtypicalDisplayPrimaryLines > 0
            || carriedBottomAtypicalDisplayPrimaryLines > 0)
        {
            destination.topScreenStats = collectPackedScreenStatsFromSnapshot(
                destination.packedTopPlane0,
                destination.packedTopPlane1,
                destination.packedTopControl,
                destination.packedTopLineMeta);
            destination.bottomScreenStats = collectPackedScreenStatsFromSnapshot(
                destination.packedBottomPlane0,
                destination.packedBottomPlane1,
                destination.packedBottomControl,
                destination.packedBottomLineMeta);
        }
    }

    if (source.hasCapture3dSource && source.capture3dSource != nullptr)
    {
        std::memcpy(
            destination.capture3dSourceDsFrame.data(),
            source.capture3dSource,
            pixelBytes);
        destination.hasCapture3dSource = true;
    }

    auto repairVramCapturePrimaryFromCaptureSource =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const SoftPackedScreenStats& screenStats,
            const SoftPackedScreenStats& oppositeStats) {
            if (!destination.hasCapture3dSource)
                return 0;
            if (screenStats.DisplayModeCounts[2] <= (SoftPackedFrameSnapshot::kScreenHeight / 2u)
                || screenStats.VramCaptureUses3dLines <= (SoftPackedFrameSnapshot::kScreenHeight / 2u)
                || screenStats.RegularCaptureUses3dLines != 0u)
            {
                return 0;
            }

            const bool oppositeStructuredPair =
                softPackedScreenUsesFullStructured2dOnlyDisplay(oppositeStats)
                || softPackedScreenUsesMostlyStructured2dOnlyDisplay(oppositeStats)
                || softPackedScreenUsesPlainStructured3dSlot(oppositeStats)
                || softPackedScreenUsesRegularStructured3dCaptureSlot(oppositeStats);
            if (!oppositeStructuredPair)
                return 0;

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                u32& meta = lineMeta[static_cast<size_t>(y)];
                const bool vramCaptureLine =
                    ((meta >> 16u) & 0x3u) == 2u
                    && (meta & kMetaVramCaptureUses3d) != 0u
                    && (meta & kMetaRegularCaptureUses3d) == 0u;
                if (!vramCaptureLine)
                    continue;
                if (packedLineHasAnyVisibleColor(plane0, y))
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                std::memcpy(
                    plane0.data() + rowBase,
                    destination.capture3dSourceDsFrame.data() + rowBase,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                meta &= ~kMetaVramCaptureUses3d;
                repairedLines++;
            }

            return repairedLines;
        };

    auto repairStructured2dOnlyPrimaryFromCaptureSource =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const SoftPackedScreenStats& screenStats,
            const SoftPackedScreenStats& oppositeStats) {
            if (!destination.hasCapture3dSource)
                return 0;
            if (!softPackedScreenUsesMostlyStructured2dOnlyDisplay(screenStats))
                return 0;
            if (oppositeStats.RegularCaptureUses3dLines <= (SoftPackedFrameSnapshot::kScreenHeight / 2u)
                || oppositeStats.VramCaptureUses3dLines != 0u)
            {
                return 0;
            }

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const bool structuredOnlyLine =
                    ((meta >> 16u) & 0x3u) == 1u
                    && (meta & (kMetaRegularCaptureUses3d
                        | kMetaVramCaptureUses3d
                        | kMetaForceLive3dCompMode7)) == 0u;
                if (!structuredOnlyLine)
                    continue;
                if (packedLineHasAnyVisibleColor(plane0, y))
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                std::memcpy(
                    plane0.data() + rowBase,
                    destination.capture3dSourceDsFrame.data() + rowBase,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                repairedLines++;
            }

            return repairedLines;
        };

    const int repairedTopVramCaptureSourceLines = renderer2dDebugControlsActive
        ? 0
        : repairVramCapturePrimaryFromCaptureSource(
            destination.packedTopPlane0,
            destination.packedTopLineMeta,
            destination.topScreenStats,
            destination.bottomScreenStats);
    const int repairedBottomVramCaptureSourceLines = renderer2dDebugControlsActive
        ? 0
        : repairVramCapturePrimaryFromCaptureSource(
            destination.packedBottomPlane0,
            destination.packedBottomLineMeta,
            destination.bottomScreenStats,
            destination.topScreenStats);
    const int repairedTopStructured2dOnlyCaptureSourceLines = renderer2dDebugControlsActive
        ? 0
        : repairStructured2dOnlyPrimaryFromCaptureSource(
            destination.packedTopPlane0,
            destination.packedTopLineMeta,
            destination.topScreenStats,
            destination.bottomScreenStats);
    const int repairedBottomStructured2dOnlyCaptureSourceLines = renderer2dDebugControlsActive
        ? 0
        : repairStructured2dOnlyPrimaryFromCaptureSource(
            destination.packedBottomPlane0,
            destination.packedBottomLineMeta,
            destination.bottomScreenStats,
            destination.topScreenStats);
    if (repairedTopVramCaptureSourceLines > 0
        || repairedBottomVramCaptureSourceLines > 0
        || repairedTopStructured2dOnlyCaptureSourceLines > 0
        || repairedBottomStructured2dOnlyCaptureSourceLines > 0)
    {
        destination.topScreenStats = collectPackedScreenStatsFromSnapshot(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
        destination.bottomScreenStats = collectPackedScreenStatsFromSnapshot(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);
    }

    int repairedTopClass4VramOverlayLines = 0;
    int repairedBottomClass4VramOverlayLines = 0;
    auto repairClass4VramCaptureOverlay =
        [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
            std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& previousPlane1,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& previousControl,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& previousLineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& oppositeLineMeta,
            int oppositeVramCaptureLineCount) {
            if (!captureBackedClass4Only
                || !isInAlternatingMode
                || renderer2dDebugControlsActive
                || !previousSnapshot.valid)
            {
                return 0;
            }

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 currentMeta = lineMeta[static_cast<size_t>(y)];
                const u32 previousMeta = previousLineMeta[static_cast<size_t>(y)];
                const u32 oppositeMeta = oppositeLineMeta[static_cast<size_t>(y)];
                const u32 currentDisplayMode = (currentMeta >> 16u) & 0x3u;
                const u32 previousDisplayMode = (previousMeta >> 16u) & 0x3u;
                const bool currentIsStructuredDisplay =
                    currentDisplayMode == 1u
                    && (currentMeta & (kMetaRegularCaptureUses3d
                        | kMetaVramCaptureUses3d
                        | kMetaForceLive3dCompMode7)) == 0u;
                const bool previousWasVramDisplay =
                    previousDisplayMode == 2u;
                const bool previousWasStructuredDisplay =
                    previousDisplayMode == 1u
                    && (previousMeta & (kMetaRegularCaptureUses3d
                        | kMetaVramCaptureUses3d
                        | kMetaForceLive3dCompMode7)) == 0u;
                const bool currentUsesVram3d =
                    currentDisplayMode == 2u
                    && (currentMeta & kMetaVramCaptureUses3d) != 0u;
                const bool oppositeCurrentlyUsesVram3d =
                    ((oppositeMeta >> 16u) & 0x3u) == 2u
                    && (oppositeMeta & kMetaVramCaptureUses3d) != 0u;
                const bool repairsStructuredPhase =
                    currentIsStructuredDisplay
                    && destination.hasCapture3dSource
                    && previousWasVramDisplay
                    && oppositeCurrentlyUsesVram3d
                    && oppositeVramCaptureLineCount > (SoftPackedFrameSnapshot::kScreenHeight / 2);
                const bool repairsVramPhase =
                    currentUsesVram3d
                    && (previousWasStructuredDisplay || previousWasVramDisplay);
                if (!repairsStructuredPhase && !repairsVramPhase)
                {
                    continue;
                }

                bool repairedLine = false;
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 currentControlAlpha = control[index] >> 24u;
                    const bool currentStructuredSlot = (currentControlAlpha & 0x40u) != 0u;
                    const bool currentHasAbove = currentStructuredSlot && (currentControlAlpha & 0x80u) != 0u;
                    const u32 currentCompMode = currentControlAlpha & 0x0Fu;

                    const u32 currentOverlayPixel = plane1[index];
                    const u32 currentPrimaryPixel = plane0[index];
                    const u32 currentControlRgb = control[index] & 0x00FFFFFFu;
                    const bool currentPlaneHasOverlay =
                        currentOverlayPixel != 0u && currentOverlayPixel != kPacked3dPlaceholder;
                    const bool currentPrimaryHasOverlay =
                        currentPrimaryPixel != 0u && currentPrimaryPixel != kPacked3dPlaceholder;
                    const bool currentControlMarksOverlay = currentControlRgb != 0u;
                    const bool currentHasUsableAbove =
                        currentHasAbove && currentPlaneHasOverlay;
                    const bool currentMarksOverlay =
                        currentPlaneHasOverlay || currentControlMarksOverlay;
                    const u32 previousOverlayPixel = previousPlane1[index];
                    const u32 previousControlAlpha = previousControl[index] >> 24u;
                    const u32 previousControlRgb = previousControl[index] & 0x00FFFFFFu;
                    const bool previousStructuredSlot = (previousControlAlpha & 0x40u) != 0u;
                    const u32 previousCompMode = previousControlAlpha & 0x0Fu;
                    const bool previousPlaneHasOverlay =
                        previousOverlayPixel != 0u && previousOverlayPixel != kPacked3dPlaceholder;
                    const bool previousControlMarksOverlay = previousControlRgb != 0u;
                    const bool previousMarksOverlay =
                        previousPlaneHasOverlay || previousControlMarksOverlay;
                    if (!currentMarksOverlay && !previousMarksOverlay)
                        continue;

                    u32 effectiveCompMode = currentStructuredSlot
                        ? currentCompMode
                        : previousCompMode;
                    if (effectiveCompMode != 7u)
                        continue;
                    if (repairsStructuredPhase && (!currentStructuredSlot || currentHasAbove))
                        continue;
                    if (repairsVramPhase
                        && currentStructuredSlot
                        && currentHasUsableAbove
                        && (!previousStructuredSlot || currentControlRgb == previousControlRgb)
                        && (!currentPrimaryHasOverlay || currentPrimaryPixel == currentOverlayPixel))
                    {
                        continue;
                    }

                    const u32 currentCapturePixel =
                        destination.capture3dSourceDsFrame[index];
                    u32 overlayPixel = 0u;
                    if (repairsVramPhase && currentControlMarksOverlay && currentPrimaryHasOverlay)
                        overlayPixel = currentPrimaryPixel;
                    if (overlayPixel == 0u && currentPlaneHasOverlay)
                        overlayPixel = currentOverlayPixel;
                    if ((overlayPixel == 0u || overlayPixel == kPacked3dPlaceholder)
                        && !repairsStructuredPhase)
                    {
                        overlayPixel = currentCapturePixel;
                    }
                    if (overlayPixel == 0u || overlayPixel == kPacked3dPlaceholder)
                        overlayPixel = previousOverlayPixel;
                    if ((overlayPixel == 0u || overlayPixel == kPacked3dPlaceholder)
                        && !repairsStructuredPhase
                        && previousSnapshot.hasCapture3dSource)
                    {
                        overlayPixel = previousSnapshot.capture3dSourceDsFrame[index];
                    }
                    if (overlayPixel == 0u || overlayPixel == kPacked3dPlaceholder)
                        continue;

                    const bool overlayProtectedBlack =
                        (currentControlAlpha & 0x20u) != 0u
                        || (previousControlAlpha & 0x20u) != 0u;
                    if (packedPixelIsOpaqueBlack(overlayPixel)
                        && !overlayProtectedBlack
                        && !currentPlaneHasOverlay
                        && !previousPlaneHasOverlay)
                    {
                        continue;
                    }

                    const bool protectedBlack =
                        overlayProtectedBlack
                        || packedPixelIsOpaqueBlack(overlayPixel);
                    const u32 overlayControlRgb =
                        currentControlMarksOverlay
                            ? currentControlRgb
                            : previousControlRgb;
                    if (currentHasUsableAbove
                        && currentControlMarksOverlay
                        && currentControlRgb == overlayControlRgb
                        && currentOverlayPixel == overlayPixel)
                    {
                        continue;
                    }
                    plane1[index] = overlayPixel;
                    control[index] = overlayControlRgb
                        | ((effectiveCompMode
                            | 0x40u
                            | 0x80u
                            | (protectedBlack ? 0x20u : 0u)) << 24u);
                    repairedLine = true;
                }

                if (repairedLine)
                    repairedLines++;
            }

            return repairedLines;
        };

    repairedTopClass4VramOverlayLines = repairClass4VramCaptureOverlay(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta,
        previousSnapshot.packedTopPlane1,
        previousSnapshot.packedTopControl,
        previousSnapshot.packedTopLineMeta,
        destination.packedBottomLineMeta,
        bottomVramCaptureLineCount);
    repairedBottomClass4VramOverlayLines = repairClass4VramCaptureOverlay(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta,
        previousSnapshot.packedBottomPlane1,
        previousSnapshot.packedBottomControl,
        previousSnapshot.packedBottomLineMeta,
        destination.packedTopLineMeta,
        topVramCaptureLineCount);
    if (repairedTopClass4VramOverlayLines > 0 || repairedBottomClass4VramOverlayLines > 0)
    {
        destination.topScreenStats = collectPackedScreenStatsFromSnapshot(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
        destination.bottomScreenStats = collectPackedScreenStatsFromSnapshot(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);
    }

    const bool topScreenUsesCurrentCapture3d =
        destination.topScreenStats.RegularCaptureUses3dLines > 0u
        || destination.topScreenStats.VramCaptureUses3dLines > 0u;
    const bool bottomScreenUsesCurrentCapture3d =
        destination.bottomScreenStats.RegularCaptureUses3dLines > 0u
        || destination.bottomScreenStats.VramCaptureUses3dLines > 0u;
    const auto* previousTopScreenPrimary =
        !renderer2dDebugControlsActive && previousSnapshot.valid
        ? &previousSnapshot.packedTopPlane0
        : nullptr;
    const auto* previousBottomScreenPrimary =
        !renderer2dDebugControlsActive && previousSnapshot.valid
        ? &previousSnapshot.packedBottomPlane0
        : nullptr;
    const bool hasTopResolvedPrimaryCache =
        !renderer2dDebugControlsActive && lineMaskHasAnyValidLine(lastValidTopScreenResolvedPrimaryLines);
    const bool hasBottomResolvedPrimaryCache =
        !renderer2dDebugControlsActive && lineMaskHasAnyValidLine(lastValidBottomScreenResolvedPrimaryLines);
    if (destination.hasCapture3dSource)
    {
        if (topScreenUsesCurrentCapture3d && !bottomScreenUsesCurrentCapture3d)
        {
            lastValidTopScreenCapture3dDsFrame = destination.capture3dSourceDsFrame;
            hasLastValidTopScreenCapture3dDsFrame = true;
        }
        else if (bottomScreenUsesCurrentCapture3d && !topScreenUsesCurrentCapture3d)
        {
            lastValidBottomScreenCapture3dDsFrame = destination.capture3dSourceDsFrame;
            hasLastValidBottomScreenCapture3dDsFrame = true;
        }
    }

    auto markCompMode7Live3dFallbackLines =
        [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                u32& meta = lineMeta[static_cast<size_t>(y)];
                const bool captureLineHasVisible3d =
                    destination.hasCapture3dSource
                    && packedLineHasAnyVisibleColor(destination.capture3dSourceDsFrame, y);
                if (captureLineHasVisible3d
                    && packedLineNeedsCompMode7Live3dFallback(plane0, control, meta, y))
                {
                    meta |= kMetaForceLive3dCompMode7;
                }
            }
        };

    auto populateComp4Placeholder = [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
                                        const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
                                        const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
                                        const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
                                        const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* previousScreenPrimary,
                                        const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* resolvedPrimaryCache,
                                        const std::array<u8, SoftPackedFrameSnapshot::kLineCount>* resolvedPrimaryCacheLines,
                                        const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* fallbackCaptureCache,
                                        std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& placeholder) {
        for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
        {
            const u32 meta = lineMeta[static_cast<size_t>(y)];
            const u32 displayMode = (meta >> 16u) & 0x3u;
            if (displayMode != 1u)
                continue;

            const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
            const u32* placeholderSource = nullptr;
            const bool fallbackCaptureLineHasUsefulPixels =
                fallbackCaptureCache != nullptr
                && packedResolvedLineHasAnyUsefulPixel(*fallbackCaptureCache, y);
            const bool fallbackCaptureCanReplaceBlackLine =
                fallbackCaptureLineHasUsefulPixels
                && !packedResolvedLineIsMostlyOpaqueBlack(*fallbackCaptureCache, y);
            if (fallbackCaptureLineHasUsefulPixels)
            {
                placeholderSource = fallbackCaptureCache->data() + rowBase;
            }
            if (previousScreenPrimary != nullptr
                && placeholderSource == nullptr
                && packedResolvedLineHasAnyUsefulPixel(*previousScreenPrimary, y))
            {
                const bool previousLineIsOnlyBlack =
                    packedResolvedLineIsMostlyOpaqueBlack(*previousScreenPrimary, y);
                if (!previousLineIsOnlyBlack || !fallbackCaptureCanReplaceBlackLine)
                    placeholderSource = previousScreenPrimary->data() + rowBase;
            }
            if (placeholderSource == nullptr
                && resolvedPrimaryCache != nullptr
                && resolvedPrimaryCacheLines != nullptr
                && (*resolvedPrimaryCacheLines)[static_cast<size_t>(y)] != 0u
                && packedResolvedLineHasAnyUsefulPixel(*resolvedPrimaryCache, y))
            {
                const bool resolvedLineIsOnlyBlack =
                    packedResolvedLineIsMostlyOpaqueBlack(*resolvedPrimaryCache, y);
                if (!resolvedLineIsOnlyBlack || !fallbackCaptureCanReplaceBlackLine)
                    placeholderSource = resolvedPrimaryCache->data() + rowBase;
            }
            if (placeholderSource == nullptr && fallbackCaptureLineHasUsefulPixels)
            {
                placeholderSource = fallbackCaptureCache->data() + rowBase;
            }
            else if (destination.hasCapture3dSource)
            {
                placeholderSource = destination.capture3dSourceDsFrame.data() + rowBase;
            }

            if (placeholderSource == nullptr)
                continue;

            for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
            {
                const size_t index = rowBase + static_cast<size_t>(x);
                const u32 compMode = (control[index] >> 24u) & 0xFu;
                const bool captureBackedComp4 =
                    compMode == 4u
                    && plane0[index] == kPacked3dPlaceholder
                    && plane1[index] == kPacked3dPlaceholder;
                if (!captureBackedComp4)
                    continue;

                placeholder[index] = placeholderSource[static_cast<size_t>(x)];
            }
        }
    };

    auto updateLastValidResolvedPrimary =
        [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& oppositeLineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& placeholder,
            std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& resolvedPrimaryCache,
            std::array<u8, SoftPackedFrameSnapshot::kLineCount>& resolvedPrimaryCacheLines) {
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const u32 oppositeMeta = oppositeLineMeta[static_cast<size_t>(y)];
                const u32 displayMode = (meta >> 16u) & 0x3u;
                const bool vramCapturePairsWithOppositeRegularCapture =
                    displayMode == 2u
                    && (meta & kMetaVramCaptureUses3d) != 0u
                    && ((oppositeMeta >> 16u) & 0x3u) == 1u
                    && (oppositeMeta & kMetaRegularCaptureUses3d) != 0u
                    && (oppositeMeta & kMetaVramCaptureUses3d) == 0u;
                if (vramCapturePairsWithOppositeRegularCapture)
                    continue;

                const bool forceLive3dCompMode7 = (meta & kMetaForceLive3dCompMode7) != 0u;
                bool captureBackedComp4Line = false;
                bool lineHasVisibleStructuredAbove = false;
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 controlAlpha = control[index] >> 24u;
                    const u32 compMode = controlAlpha & 0xFu;
                    if ((controlAlpha & 0x80u) != 0u
                        && packedPixelHasVisibleColor(plane1[index]))
                    {
                        lineHasVisibleStructuredAbove = true;
                    }
                    if (compMode == 4u
                        && plane0[index] == kPacked3dPlaceholder
                        && plane1[index] == kPacked3dPlaceholder)
                    {
                        captureBackedComp4Line = true;
                        break;
                    }
                }

                const u32* resolvedSource = nullptr;
                if (forceLive3dCompMode7)
                {
                    if (packedResolvedLineHasAnyUsefulPixel(plane0, y))
                    {
                        resolvedSource = plane0.data() + rowBase;
                    }
                    else if (destination.hasCapture3dSource
                        && packedResolvedLineHasAnyUsefulPixel(destination.capture3dSourceDsFrame, y))
                    {
                        resolvedSource = destination.capture3dSourceDsFrame.data() + rowBase;
                    }
                }
                else if (captureBackedComp4Line)
                {
                    if (packedResolvedLineHasAnyUsefulPixel(placeholder, y))
                        resolvedSource = placeholder.data() + rowBase;
                }
                else
                {
                    resolvedSource = plane0.data() + rowBase;
                }

                if (resolvedSource == nullptr)
                    continue;

                std::memcpy(
                    resolvedPrimaryCache.data() + rowBase,
                    resolvedSource,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                if (displayMode == 1u
                    && (meta & kMetaRegularCaptureUses3d) != 0u
                    && (meta & kMetaVramCaptureUses3d) == 0u
                    && lineHasVisibleStructuredAbove)
                {
                    for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                    {
                        const size_t index = rowBase + static_cast<size_t>(x);
                        const u32 controlAlpha = control[index] >> 24u;
                        if ((controlAlpha & 0x80u) != 0u
                            && packedPixelHasVisibleColor(plane1[index]))
                        {
                            resolvedPrimaryCache[index] = plane1[index];
                        }
                    }
                }
                resolvedPrimaryCacheLines[static_cast<size_t>(y)] = 1u;
            }
        };

    auto repairVramCapturePrimaryFromResolvedCache =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& oppositeLineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* resolvedPrimaryCache,
            const std::array<u8, SoftPackedFrameSnapshot::kLineCount>* resolvedPrimaryCacheLines) {
            if (resolvedPrimaryCache == nullptr || resolvedPrimaryCacheLines == nullptr)
                return 0;

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                if ((*resolvedPrimaryCacheLines)[static_cast<size_t>(y)] == 0u)
                    continue;

                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const u32 oppositeMeta = oppositeLineMeta[static_cast<size_t>(y)];
                const bool vramCapturePairsWithOppositeRegularCapture =
                    ((meta >> 16u) & 0x3u) == 2u
                    && (meta & kMetaVramCaptureUses3d) != 0u
                    && ((oppositeMeta >> 16u) & 0x3u) == 1u
                    && (oppositeMeta & kMetaRegularCaptureUses3d) != 0u
                    && (oppositeMeta & kMetaVramCaptureUses3d) == 0u;
                if (!vramCapturePairsWithOppositeRegularCapture)
                    continue;
                if (!packedResolvedLineHasAnyUsefulPixel(*resolvedPrimaryCache, y))
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                std::memcpy(
                    plane0.data() + rowBase,
                    resolvedPrimaryCache->data() + rowBase,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                repairedLines++;
            }

            return repairedLines;
        };

    auto repairRegularCaptureStructuredAbovePrimary =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
            std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta) {
            size_t regularPixels = 0;
            size_t regularStructuredAbovePixels = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const bool regularCapture3dLine =
                    ((meta >> 16u) & 0x3u) == 1u
                    && (meta & kMetaRegularCaptureUses3d) != 0u
                    && (meta & kMetaVramCaptureUses3d) == 0u;
                if (!regularCapture3dLine)
                    continue;

                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const u32 controlAlpha = control[rowBase + static_cast<size_t>(x)] >> 24u;
                    regularPixels++;
                    if ((controlAlpha & 0x80u) != 0u)
                        regularStructuredAbovePixels++;
                }
            }
            if (regularPixels == 0)
                return 0;
            if (regularStructuredAbovePixels > (regularPixels / 16u))
                return 0;

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const bool regularCapture3dLine =
                    ((meta >> 16u) & 0x3u) == 1u
                    && (meta & kMetaRegularCaptureUses3d) != 0u
                    && (meta & kMetaVramCaptureUses3d) == 0u;
                if (!regularCapture3dLine)
                    continue;

                bool repairedLine = false;
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 controlAlpha = control[index] >> 24u;
                    const u32 compMode = controlAlpha & 0xFu;
                    const bool structuredAbove =
                        (controlAlpha & 0x40u) != 0u
                        && (controlAlpha & 0x80u) != 0u;
                    if (!structuredAbove || compMode != 7u)
                        continue;

                    const u32 abovePixel = plane1[index];
                    if (!packedPixelHasVisibleColor(abovePixel)
                        && !packedPixelIsOpaqueBlack(abovePixel))
                    {
                        continue;
                    }

                    plane0[index] = abovePixel;
                    control[index] =
                        (control[index] & 0x00FFFFFFu)
                        | ((controlAlpha & ~(0x40u | 0x80u)) << 24u);
                    repairedLine = true;
                }

                if (repairedLine)
                    repairedLines++;
            }

            return repairedLines;
        };

    const int repairedTopRegularStructuredPrimaryLines = renderer2dDebugControlsActive
        ? 0
        : repairRegularCaptureStructuredAbovePrimary(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta);
    const int repairedBottomRegularStructuredPrimaryLines = renderer2dDebugControlsActive
        ? 0
        : repairRegularCaptureStructuredAbovePrimary(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta);

    const int repairedTopVramPrimaryLines = renderer2dDebugControlsActive
        ? 0
        : repairVramCapturePrimaryFromResolvedCache(
            destination.packedTopPlane0,
            destination.packedTopLineMeta,
            destination.packedBottomLineMeta,
            hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimary : nullptr,
            hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimaryLines : nullptr);
    const int repairedBottomVramPrimaryLines = renderer2dDebugControlsActive
        ? 0
        : repairVramCapturePrimaryFromResolvedCache(
            destination.packedBottomPlane0,
            destination.packedBottomLineMeta,
            destination.packedTopLineMeta,
            hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimary : nullptr,
            hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimaryLines : nullptr);

    markCompMode7Live3dFallbackLines(
        destination.packedTopPlane0,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    markCompMode7Live3dFallbackLines(
        destination.packedBottomPlane0,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);

    populateComp4Placeholder(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta,
        previousTopScreenPrimary,
        hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimary : nullptr,
        hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimaryLines : nullptr,
        hasLastValidTopScreenCapture3dDsFrame ? &lastValidTopScreenCapture3dDsFrame : nullptr,
        destination.comp4TopPlaceholder);
    populateComp4Placeholder(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta,
        previousBottomScreenPrimary,
        hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimary : nullptr,
        hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimaryLines : nullptr,
        hasLastValidBottomScreenCapture3dDsFrame ? &lastValidBottomScreenCapture3dDsFrame : nullptr,
        destination.comp4BottomPlaceholder);

    auto repairTemporalPrimaryFromResolvedCache =
        [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
            const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
            const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>* resolvedPrimaryCache,
            const std::array<u8, SoftPackedFrameSnapshot::kLineCount>* resolvedPrimaryCacheLines) {
            if (resolvedPrimaryCache == nullptr || resolvedPrimaryCacheLines == nullptr)
                return 0;

            int repairedLines = 0;
            for (int y = 0; y < SoftPackedFrameSnapshot::kScreenHeight; y++)
            {
                if ((*resolvedPrimaryCacheLines)[static_cast<size_t>(y)] == 0u)
                    continue;
                if (packedResolvedLineHasAnyUsefulPixel(plane0, y))
                    continue;

                const u32 meta = lineMeta[static_cast<size_t>(y)];
                const u32 displayMode = (meta >> 16u) & 0x3u;
                if (displayMode != 1u)
                    continue;

                const bool temporalCompMode7Uses3d =
                    (meta & (kMetaRegularCaptureUses3d
                        | kMetaForceLive3dCompMode7)) != 0u;
                if (!temporalCompMode7Uses3d)
                    continue;

                bool lineHasCompMode7 = false;
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth);
                for (int x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; x++)
                {
                    const size_t index = rowBase + static_cast<size_t>(x);
                    const u32 compMode = (control[index] >> 24u) & 0xFu;
                    const bool captureBackedComp4 =
                        compMode == 4u
                        && plane0[index] == kPacked3dPlaceholder
                        && plane1[index] == kPacked3dPlaceholder;
                    if (captureBackedComp4)
                    {
                        lineHasCompMode7 = false;
                        break;
                    }
                    if (compMode == 7u)
                        lineHasCompMode7 = true;
                }

                if (!lineHasCompMode7)
                    continue;

                std::memcpy(
                    plane0.data() + rowBase,
                    resolvedPrimaryCache->data() + rowBase,
                    static_cast<size_t>(SoftPackedFrameSnapshot::kScreenWidth) * sizeof(u32));
                repairedLines++;
            }

            return repairedLines;
        };

    const int repairedTopTemporalPrimaryLines = renderer2dDebugControlsActive || topFullRegularCaptureWithBottomCompMode2Slot
        ? 0
        : repairTemporalPrimaryFromResolvedCache(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta,
            hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimary : nullptr,
            hasTopResolvedPrimaryCache ? &lastValidTopScreenResolvedPrimaryLines : nullptr);
    const int repairedBottomTemporalPrimaryLines = renderer2dDebugControlsActive
        ? 0
        : repairTemporalPrimaryFromResolvedCache(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta,
            hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimary : nullptr,
            hasBottomResolvedPrimaryCache ? &lastValidBottomScreenResolvedPrimaryLines : nullptr);

    if (!renderer2dDebugControlsActive)
    {
        updateLastValidResolvedPrimary(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta,
            destination.packedBottomLineMeta,
            destination.comp4TopPlaceholder,
            lastValidTopScreenResolvedPrimary,
            lastValidTopScreenResolvedPrimaryLines);
        updateLastValidResolvedPrimary(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta,
            destination.packedTopLineMeta,
            destination.comp4BottomPlaceholder,
            lastValidBottomScreenResolvedPrimary,
            lastValidBottomScreenResolvedPrimaryLines);
    }


    destination.valid = true;
    previousSnapshot = destination;
    return true;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
