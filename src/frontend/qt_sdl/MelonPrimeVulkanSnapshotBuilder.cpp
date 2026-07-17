#include "MelonPrimeVulkanSnapshotBuilder.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cassert>
#include <cstring>

namespace MelonPrime
{
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

bool lineHasStructuredSlot(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    std::size_t y) noexcept
{
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        if (((control[rowBase + x] >> 24u) & 0x40u) != 0u)
            return true;
    }
    return false;
}

void copyLine(
    const SoftPackedFrameSnapshot& source,
    bool top,
    std::size_t y,
    SoftPackedFrameSnapshot& destination)
{
    const auto& sourcePlane0 = top ? source.packedTopPlane0 : source.packedBottomPlane0;
    const auto& sourcePlane1 = top ? source.packedTopPlane1 : source.packedBottomPlane1;
    const auto& sourceControl = top ? source.packedTopControl : source.packedBottomControl;
    const auto& sourceMeta = top ? source.packedTopLineMeta : source.packedBottomLineMeta;
    auto& destinationPlane0 = top ? destination.packedTopPlane0 : destination.packedBottomPlane0;
    auto& destinationPlane1 = top ? destination.packedTopPlane1 : destination.packedBottomPlane1;
    auto& destinationControl = top ? destination.packedTopControl : destination.packedBottomControl;
    auto& destinationMeta = top ? destination.packedTopLineMeta : destination.packedBottomLineMeta;
    const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
    constexpr std::size_t rowBytes = SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32);
    std::memcpy(destinationPlane0.data() + rowBase, sourcePlane0.data() + rowBase, rowBytes);
    std::memcpy(destinationPlane1.data() + rowBase, sourcePlane1.data() + rowBase, rowBytes);
    std::memcpy(destinationControl.data() + rowBase, sourceControl.data() + rowBase, rowBytes);
    destinationMeta[y] = (sourceMeta[y] & 0xFFFF0000u) | (destinationMeta[y] & 0x0000FFFFu);
}

void protectOpaqueBlack(
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control) noexcept
{
    for (std::size_t index = 0; index < control.size(); ++index)
    {
        u32 alpha = control[index] >> 24u;
        const bool structuredSlot = (alpha & 0x40u) != 0u;
        const bool hasAbove = structuredSlot && (alpha & 0x80u) != 0u;
        const bool black2d = hasAbove
            ? packedPixelIsOpaqueBlack(plane1[index])
            : (!structuredSlot && packedPixelIsOpaqueBlack(plane0[index]));
        if (black2d)
        {
            alpha |= 0x20u;
            control[index] = (control[index] & 0x00FFFFFFu) | (alpha << 24u);
        }
    }
}

void collectStats(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    SoftPackedScreenStats& stats)
{
    stats = {};
    for (std::size_t y = 0; y < lineMeta.size(); ++y)
    {
        const u32 meta = lineMeta[y];
        const u32 displayMode = (meta >> 16u) & 0x3u;
        stats.DisplayModeCounts[displayMode]++;
        if (displayMode == 1u && (meta & kMetaRegularCaptureUses3d) != 0u)
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

        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        bool captureBackedComp4Line = false;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 controlAlpha = control[index] >> 24u;
            const u32 compMode = controlAlpha & 0xFu;
            if (compMode < stats.CompModeCounts.size())
                stats.CompModeCounts[compMode]++;
            const bool structuredSlot = (controlAlpha & 0x40u) != 0u;
            const bool structuredAbove = structuredSlot && (controlAlpha & 0x80u) != 0u;
            const bool structured2dOnly = !structuredSlot && (controlAlpha & 0x80u) != 0u;
            if (structuredSlot) stats.StructuredSlotPixels++;
            if (structuredAbove) stats.StructuredAbovePixels++;
            if (structured2dOnly) stats.Structured2DOnlyPixels++;
            if (packedPixelIsUseful(plane0[index])) stats.Plane0UsefulPixels++;
            if (packedPixelHasVisibleColor(plane0[index])) stats.Plane0VisiblePixels++;
            if (packedPixelIsOpaqueBlack(plane0[index])) stats.Plane0OpaqueBlackPixels++;
            if (packedPixelIsUseful(plane1[index])) stats.Plane1UsefulPixels++;
            if (packedPixelHasVisibleColor(plane1[index])) stats.Plane1VisiblePixels++;
            if (packedPixelIsOpaqueBlack(plane1[index])) stats.Plane1OpaqueBlackPixels++;
            if (structuredAbove && packedPixelHasVisibleColor(plane1[index])) stats.StructuredAboveVisiblePixels++;
            if (structuredAbove && packedPixelIsOpaqueBlack(plane1[index])) stats.StructuredAboveBlackPixels++;
            if (structured2dOnly && packedPixelHasVisibleColor(plane0[index])) stats.Structured2DOnlyVisiblePixels++;
            if ((controlAlpha & 0x20u) != 0u) stats.ProtectedBlackPixels++;
            if (compMode == 4u
                && plane0[index] == kPacked3dPlaceholder
                && plane1[index] == kPacked3dPlaceholder)
            {
                stats.CaptureBackedComp4Pixels++;
                captureBackedComp4Line = true;
            }
        }
        if (captureBackedComp4Line)
            stats.CaptureBackedComp4Lines++;
    }
}

void populateComp4Placeholder(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    bool top,
    SoftPackedFrameSnapshot& snapshot,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& placeholder)
{
    // Only promote exact capture-source pixels that already belong to this
    // physical LCD. Historical Plane0 is ordinary 2D and must not be elevated
    // into Comp4 capture placeholders (cross-screen injection risk).
    const bool captureSourceMatchesPhysicalScreen =
        snapshot.captureScreenSwapValid
        && snapshot.captureScreenSwap == top;
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        const auto& screenNeedsCapture3d = top
            ? snapshot.topScreenNeedsCapture3dMask
            : snapshot.bottomScreenNeedsCapture3dMask;
        const bool currentCaptureLine = screenNeedsCapture3d[y] != 0u;
        const bool currentCaptureSourceValid =
            snapshot.hasCapture3dSource
            && snapshot.capture3dSourceLineValidMask[y] != 0u;
        for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
        {
            const std::size_t index = rowBase + x;
            const u32 controlAlpha = control[index] >> 24u;
            const bool captureBackedComp4 = (controlAlpha & 0xFu) == 4u
                && plane0[index] == kPacked3dPlaceholder
                && plane1[index] == kPacked3dPlaceholder;
            if (!captureBackedComp4)
                continue;

            u32 value = 0u;
            if (currentCaptureLine
                && currentCaptureSourceValid
                && captureSourceMatchesPhysicalScreen)
            {
                value = snapshot.capture3dSourceDsFrame[index];
            }
            if (packedPixelIsUseful(value))
                placeholder[index] = value;
        }
    }
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

// --- Structured merge from engine planes (Sapphire copyStructuredLine /
// mergeStructuredDisplayLine). Engine planes are already at native
// kScreenWidth stride, unlike Sapphire's stride-769 packed framebuffer.

bool structuredLineHasPayload(
    const u32* structuredPlane0,
    const u32* structuredPlane1,
    const u32* structuredControl,
    std::size_t rowBase) noexcept
{
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        if (structuredControl[index] != 0u || structuredPlane1[index] != 0u || structuredPlane0[index] != 0u)
            return true;
    }
    return false;
}

bool packedLineHas3dSlot(
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    std::size_t rowBase) noexcept
{
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 plane0Alpha = plane0[index] >> 24u;
        const u32 plane1Alpha = plane1[index] >> 24u;
        const u32 controlAlpha = control[index] >> 24u;
        if ((plane0Alpha & 0xC0u) == 0x40u
            || (plane1Alpha & 0xC0u) == 0x40u
            || (controlAlpha & 0x40u) != 0u)
        {
            return true;
        }
    }
    return false;
}

void copyStructuredLineFromEngine(
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

void mergeStructuredDisplayLineFromEngine(
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& baselinePlane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& baselinePlane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& baselineControl,
    const u32* structuredPlane0,
    const u32* structuredPlane1,
    const u32* structuredControl,
    std::size_t rowBase,
    bool captureBackedClass4Only,
    bool screenSwapToggledThisFrame) noexcept
{
    for (std::size_t x = 0; x < SoftPackedFrameSnapshot::kScreenWidth; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 packedPlane0 = baselinePlane0[index];
        const u32 packedPlane1 = baselinePlane1[index];
        const u32 packedControl = baselineControl[index];
        const u32 packedPlane0Alpha = packedPlane0 >> 24u;
        const u32 packedPlane1Alpha = packedPlane1 >> 24u;
        const u32 packedControlAlpha = packedControl >> 24u;
        const bool packedNeeds3DSlot =
            (packedPlane0Alpha & 0xC0u) == 0x40u
            || (packedPlane1Alpha & 0xC0u) == 0x40u
            || (packedControlAlpha & 0x40u) != 0u;
        const u32 structuredP0 = structuredPlane0[index];
        const u32 structuredP1 = structuredPlane1[index];
        const u32 structuredC = structuredControl[index];
        const bool structuredHasRenderablePayload =
            (structuredP0 != 0u && structuredP0 != kPacked3dPlaceholder)
            || (structuredP1 != 0u && structuredP1 != kPacked3dPlaceholder);
        const u32 structuredControlAlpha = structuredC >> 24u;
        const bool structuredHas3DSlot =
            ((structuredP0 >> 24u) & 0xC0u) == 0x40u
            || ((structuredP1 >> 24u) & 0xC0u) == 0x40u
            || (structuredControlAlpha & 0x40u) != 0u;
        const bool structuredHasAbove =
            (structuredControlAlpha & 0x40u) != 0u
            && (structuredControlAlpha & 0x80u) != 0u
            && structuredP1 != 0u;
        const bool packedHasCurrent2D =
            (packedPlane0 != 0u && packedPlane0 != kPacked3dPlaceholder)
            || (packedPlane1 != 0u && packedPlane1 != kPacked3dPlaceholder);
        const bool packedCurrent2DOnly = packedHasCurrent2D && !packedNeeds3DSlot;

        if (!structuredHasRenderablePayload && !(packedNeeds3DSlot && structuredHas3DSlot))
        {
            if (structuredHas3DSlot && packedCurrent2DOnly)
            {
                control[index] = (packedControl & 0x00FFFFFFu)
                    | ((packedControlAlpha | 0x80u) << 24u);
            }
            continue;
        }

        plane0[index] = structuredP0;
        plane1[index] = structuredP1;
        control[index] = structuredC;
        if (structuredHas3DSlot && !structuredHasAbove && packedCurrent2DOnly)
        {
            plane1[index] = packedPlane0;
            const u32 overlayControlRgb =
                captureBackedClass4Only
                    && screenSwapToggledThisFrame
                    && (packedControl & 0x00FFFFFFu) != 0u
                    ? (packedControl & 0x00FFFFFFu)
                    : (structuredC & 0x00FFFFFFu);
            const bool protectedBlack =
                packedPixelIsOpaqueBlack(packedPlane0)
                && !packedPixelHasVisibleColor(packedPlane0);
            control[index] =
                overlayControlRgb
                | ((structuredControlAlpha
                    | 0x40u
                    | 0x80u
                    | (protectedBlack ? 0x20u : 0u)) << 24u);
        }
    }
}

void mergeStructuredScreenPlanes(
    const u32* structuredPlane0,
    const u32* structuredPlane1,
    const u32* structuredControl,
    bool captureBackedHasStructured2DSource,
    bool captureBackedClass4Only,
    bool screenSwapToggledThisFrame,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& control,
    std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta)
{
    if (structuredPlane0 == nullptr || structuredPlane1 == nullptr || structuredControl == nullptr)
        return;

    // Sapphire reads the merge source from the raw framebuffer (distinct
    // memory from the destination it writes). Melon overlays in place, so
    // take an explicit baseline snapshot before any pixel gets rewritten.
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount> baselinePlane0 = plane0;
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount> baselinePlane1 = plane1;
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount> baselineControl = control;

    const CaptureLineCounts ownCounts = countCaptureLineCounts(lineMeta);
    const bool hasPartialRegularCapture =
        ownCounts.regularCaptureLines > 0
        && ownCounts.regularCaptureLines < static_cast<int>(SoftPackedFrameSnapshot::kLineCount);

    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        const u32 meta = lineMeta[y];
        const u32 displayMode = (meta >> 16u) & 0x3u;
        const bool partialRegularCaptureLine =
            hasPartialRegularCapture && (meta & kMetaRegularCaptureUses3d) != 0u;

        bool structuredPayloadKnown = false;
        bool structuredPayload = false;
        const auto hasStructuredPayload = [&]() {
            if (!structuredPayloadKnown)
            {
                structuredPayload = structuredLineHasPayload(
                    structuredPlane0, structuredPlane1, structuredControl, rowBase);
                structuredPayloadKnown = true;
            }
            return structuredPayload;
        };

        const bool lineNeedsStructured3d =
            !captureBackedHasStructured2DSource
            || (meta & (kMetaRegularCaptureUses3d | kMetaVramCaptureUses3d | kMetaForceLive3dCompMode7)) != 0u
            || packedLineHas3dSlot(baselinePlane0, baselinePlane1, baselineControl, rowBase);
        const bool structuredDisplayLine =
            displayMode == 1u
            && lineNeedsStructured3d
            && (!partialRegularCaptureLine || hasStructuredPayload());
        const bool structuredVramCapture =
            displayMode == 2u
            && (meta & kMetaVramCaptureUses3d) != 0u
            && hasStructuredPayload();

        if (structuredDisplayLine && captureBackedHasStructured2DSource)
        {
            mergeStructuredDisplayLineFromEngine(
                plane0, plane1, control,
                baselinePlane0, baselinePlane1, baselineControl,
                structuredPlane0, structuredPlane1, structuredControl,
                rowBase,
                captureBackedClass4Only,
                screenSwapToggledThisFrame);
        }
        else if (structuredDisplayLine || structuredVramCapture)
        {
            copyStructuredLineFromEngine(
                plane0, plane1, control,
                structuredPlane0, structuredPlane1, structuredControl,
                rowBase);
        }
    }
}

// --- Capture metadata normalization (Sapphire clearBroadPartialRegularCapture
// / clearBroadRegularCaptureAgainstOppositeVram).

void normalizeCaptureLineMeta(
    std::array<u32, SoftPackedFrameSnapshot::kLineCount>& lineMeta,
    bool partialCapture3dMask,
    int regularCaptureLineCount,
    int vramCaptureLineCount,
    int oppositeVramCaptureLineCount) noexcept
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

    if (regularCaptureLineCount > (kLineCount / 2)
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
// promoteLowresCaptureImageToStructuredSlot), simplified: no
// captureBackedPartialClass0Only-specific overlay carry (that signal is not
// available from StructuredVulkanSnapshotSource).

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
    const auto clearHistories = [](auto& histories) {
        for (PhaseHistory& history : histories)
        {
            history.snapshot.clear();
            history.generation = 0;
            history.valid = false;
        }
    };
    clearHistories(phaseHistory);
    clearHistories(capturePhaseHistory);
    previousSnapshot.clear();
    engineATop = {};
    engineABottom = {};
    framesSinceLastScreenSwapToggle = 1024;
    wasInAlternatingMode = false;
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

    // Phase index follows the physical LCD routing used while packing 2D
    // planes (GPU.ScreenSwap), not merely completed-3D owner. 2D-only menus
    // often leave Renderer3DOwnerIsTop false every frame; keying only on that
    // collapses opposite ScreenSwap phases into one bucket and recovery then
    // fills Top holes with the other engine's pixels.
    const std::size_t phaseIndex = source.captureScreenSwapValid
        ? 2u + (source.physicalScreenSwap ? 2u : 0u) + (source.captureScreenSwap ? 1u : 0u)
        : (source.physicalScreenSwap ? 1u : 0u);
    PhaseHistory& matchingHistory = phaseHistory[phaseIndex];
    if (source.renderer3dReferenceValid
        && matchingHistory.valid
        && source.generation < matchingHistory.generation)
        return false;
    if (source.renderer3dReferenceValid
        && matchingHistory.valid
        && source.generation == matchingHistory.generation)
    {
        destination = matchingHistory.snapshot;
        destination.frameId = frameId;
        return true;
    }
    const std::size_t capturePhaseIndex =
        (source.physicalScreenSwap ? 2u : 0u)
        + (source.captureScreenSwap ? 1u : 0u);
    PhaseHistory& matchingCaptureHistory = source.captureScreenSwapValid
        ? capturePhaseHistory[capturePhaseIndex]
        : matchingHistory;
    const bool matchingCaptureHistoryEligible = source.renderer3dReferenceValid
        && matchingCaptureHistory.valid
        && matchingCaptureHistory.generation <= source.generation;

    destination.clear();
    destination.frameId = frameId;
    destination.sourceGeneration = source.generation;
    destination.renderer3dRenderSerial = source.renderer3dRenderSerial;
    destination.renderer3dCompletionValue = source.renderer3dCompletionValue;
    destination.renderer3dImageSlot = source.renderer3dImageSlot;
    destination.renderer3dReferenceValid = source.renderer3dReferenceValid;
    destination.frontBufferLatched = source.frontBuffer;
    destination.renderer3dOwnerIsTop = source.renderer3dOwnerIsTop;
    destination.captureScreenSwap = source.captureScreenSwap;
    destination.captureScreenSwapValid = source.captureScreenSwapValid;
    destination.physicalScreenSwap = source.physicalScreenSwap;
    destination.captureBackedClass4Only = source.captureBackedClass4Only;
    destination.captureBackedHasStructured2DSource = source.captureBackedHasStructured2DSource;
    destination.valid = true;

    constexpr std::size_t pixelBytes = SoftPackedFrameSnapshot::kPixelCount * sizeof(u32);
    constexpr std::size_t metaBytes = SoftPackedFrameSnapshot::kLineCount * sizeof(u32);
    std::memcpy(destination.packedTopPlane0.data(), source.plane[0][0], pixelBytes);
    std::memcpy(destination.packedTopPlane1.data(), source.plane[0][1], pixelBytes);
    std::memcpy(destination.packedTopControl.data(), source.plane[0][2], pixelBytes);
    std::memcpy(destination.packedTopLineMeta.data(), source.lineMeta[0], metaBytes);
    std::memcpy(destination.packedBottomPlane0.data(), source.plane[1][0], pixelBytes);
    std::memcpy(destination.packedBottomPlane1.data(), source.plane[1][1], pixelBytes);
    std::memcpy(destination.packedBottomControl.data(), source.plane[1][2], pixelBytes);
    std::memcpy(destination.packedBottomLineMeta.data(), source.lineMeta[1], metaBytes);

#ifndef NDEBUG
    // Physical LCD identity is carried by the buffer itself. Renderer-owner
    // metadata must never select or exchange these planes.
    assert(std::memcmp(destination.packedTopPlane0.data(), source.plane[0][0], pixelBytes) == 0);
    assert(std::memcmp(destination.packedTopPlane1.data(), source.plane[0][1], pixelBytes) == 0);
    assert(std::memcmp(destination.packedTopControl.data(), source.plane[0][2], pixelBytes) == 0);
    assert(std::memcmp(destination.packedBottomPlane0.data(), source.plane[1][0], pixelBytes) == 0);
    assert(std::memcmp(destination.packedBottomPlane1.data(), source.plane[1][1], pixelBytes) == 0);
    assert(std::memcmp(destination.packedBottomControl.data(), source.plane[1][2], pixelBytes) == 0);
#endif

    protectOpaqueBlack(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl);
    protectOpaqueBlack(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl);

    if (source.hasCapture3dSource && source.capture3dSource != nullptr)
    {
        std::memcpy(destination.capture3dSourceDsFrame.data(), source.capture3dSource, pixelBytes);
        destination.hasCapture3dSource = true;
    }
    if (source.capture3dSourceLineValid != nullptr)
    {
        std::memcpy(
            destination.capture3dSourceLineValidMask.data(),
            source.capture3dSourceLineValid,
            destination.capture3dSourceLineValidMask.size() * sizeof(u8));
    }
    std::memcpy(
        destination.topScreenNeedsCapture3dMask.data(),
        source.screenNeedsCapture3d[0],
        destination.topScreenNeedsCapture3dMask.size() * sizeof(u8));
    std::memcpy(
        destination.bottomScreenNeedsCapture3dMask.data(),
        source.screenNeedsCapture3d[1],
        destination.bottomScreenNeedsCapture3dMask.size() * sizeof(u8));

    // Capture-source history only here. Physical plane recovery must run AFTER
    // Engine A phase-cache restore; otherwise holes are filled with opposite-
    // phase 2D and screenHasExplicitCompositedContent stays true forever.
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const bool currentCaptureLine = destination.captureScreenSwapValid
            ? (destination.captureScreenSwap
                ? destination.topScreenNeedsCapture3dMask[y] != 0u
                : destination.bottomScreenNeedsCapture3dMask[y] != 0u)
            : (
                destination.topScreenNeedsCapture3dMask[y] != 0u
                || destination.bottomScreenNeedsCapture3dMask[y] != 0u);
        const bool currentCaptureSourceValid = destination.hasCapture3dSource
            && destination.capture3dSourceLineValidMask[y] != 0u;
        const bool historyCaptureSourceValid = matchingCaptureHistoryEligible
            && matchingCaptureHistory.snapshot.hasCapture3dSource
            && matchingCaptureHistory.snapshot.capture3dSourceLineValidMask[y] != 0u;
        if (currentCaptureLine && !currentCaptureSourceValid && historyCaptureSourceValid)
        {
            const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
            std::memcpy(
                destination.capture3dSourceDsFrame.data() + rowBase,
                matchingCaptureHistory.snapshot.capture3dSourceDsFrame.data() + rowBase,
                SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
            destination.captureFallbackLines[y] = 1u;
            destination.capture3dSourceLineValidMask[y] = 1u;
            destination.hasCapture3dSource = true;
            if (!destination.captureScreenSwapValid
                && matchingCaptureHistory.snapshot.captureScreenSwapValid)
            {
                destination.captureScreenSwap = matchingCaptureHistory.snapshot.captureScreenSwap;
                destination.captureScreenSwapValid = true;
            }
        }
    }

    // Hoisted ahead of the Engine A phase cache: the structured-merge and
    // temporal-carry passes below also need screenSwapToggledThisFrame /
    // isInAlternatingMode, and must run before Engine A restores planes.
    const bool screenSwapToggledThisFrame =
        previousSnapshot.valid
        && previousSnapshot.physicalScreenSwap != destination.physicalScreenSwap;
    if (screenSwapToggledThisFrame)
        framesSinceLastScreenSwapToggle = 0;
    else if (framesSinceLastScreenSwapToggle < 1024)
        framesSinceLastScreenSwapToggle++;
    const bool isInAlternatingMode = framesSinceLastScreenSwapToggle <= 1;
    if (isInAlternatingMode != wasInAlternatingMode)
    {
        engineATop.valid = false;
        engineABottom.valid = false;
    }
    wasInAlternatingMode = isInAlternatingMode;

    // Selective structured merge from the engine that currently targets each
    // physical screen (Sapphire copyStructuredLine / mergeStructuredDisplayLine).
    // Top/Bottom routing follows physicalScreenSwap the same way the Engine A
    // phase cache below does.
    const std::size_t topEngineIndex = destination.physicalScreenSwap ? 0u : 1u;
    const std::size_t bottomEngineIndex = destination.physicalScreenSwap ? 1u : 0u;
    mergeStructuredScreenPlanes(
        source.enginePlane[topEngineIndex][0],
        source.enginePlane[topEngineIndex][1],
        source.enginePlane[topEngineIndex][2],
        destination.captureBackedHasStructured2DSource,
        destination.captureBackedClass4Only,
        screenSwapToggledThisFrame,
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    mergeStructuredScreenPlanes(
        source.enginePlane[bottomEngineIndex][0],
        source.enginePlane[bottomEngineIndex][1],
        source.enginePlane[bottomEngineIndex][2],
        destination.captureBackedHasStructured2DSource,
        destination.captureBackedClass4Only,
        screenSwapToggledThisFrame,
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);

    // Capture metadata normalization (Sapphire clearBroadPartialRegularCapture
    // / clearBroadRegularCaptureAgainstOppositeVram), gated on the engine
    // planes actually being present for both screens this frame.
    const bool hasEnginePlanesForNormalization =
        source.enginePlane[topEngineIndex][0] != nullptr
        && source.enginePlane[topEngineIndex][1] != nullptr
        && source.enginePlane[topEngineIndex][2] != nullptr
        && source.enginePlane[bottomEngineIndex][0] != nullptr
        && source.enginePlane[bottomEngineIndex][1] != nullptr
        && source.enginePlane[bottomEngineIndex][2] != nullptr;
    if (hasEnginePlanesForNormalization)
    {
        int combinedCaptureMaskLines = 0;
        for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
        {
            if (destination.topScreenNeedsCapture3dMask[y] != 0u
                || destination.bottomScreenNeedsCapture3dMask[y] != 0u)
            {
                ++combinedCaptureMaskLines;
            }
        }
        const bool partialCapture3dMask =
            combinedCaptureMaskLines > 0
            && combinedCaptureMaskLines < static_cast<int>(SoftPackedFrameSnapshot::kLineCount);

        const CaptureLineCounts topLineCounts = countCaptureLineCounts(destination.packedTopLineMeta);
        const CaptureLineCounts bottomLineCounts = countCaptureLineCounts(destination.packedBottomLineMeta);
        normalizeCaptureLineMeta(
            destination.packedTopLineMeta,
            partialCapture3dMask,
            topLineCounts.regularCaptureLines,
            topLineCounts.vramCaptureLines,
            bottomLineCounts.vramCaptureLines);
        normalizeCaptureLineMeta(
            destination.packedBottomLineMeta,
            partialCapture3dMask,
            bottomLineCounts.regularCaptureLines,
            bottomLineCounts.vramCaptureLines,
            topLineCounts.vramCaptureLines);
    }

    // Temporal carries from the previous frame's latched snapshot, run before
    // Engine A restores planes so they only fill genuinely empty/ambiguous
    // lines rather than fighting the phase-cache repair below.
    carryPreviousLatchedScreenLines(
        previousSnapshot,
        true,
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    carryPreviousLatchedScreenLines(
        previousSnapshot,
        false,
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);
    carryPreviousTemporalOverlayPixels(
        previousSnapshot,
        true,
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    carryPreviousTemporalOverlayPixels(
        previousSnapshot,
        false,
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);
    carryPreviousFullRegularComp7Overlay(
        previousSnapshot,
        true,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    carryPreviousFullRegularComp7Overlay(
        previousSnapshot,
        false,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);

    // Sapphire Engine A Top/Bottom phase cache. physicalScreenSwap == true means
    // Engine A is currently routed to physical Top. Prefer live EnginePlanes[0]
    // as the Engine A identity source; physical destination planes alone are a
    // post-routing approximation and can already be the swapped content.
    {
        const bool engineAOnTop = destination.physicalScreenSwap;
        const bool captureBackedHasStructured2DSource = destination.captureBackedHasStructured2DSource;
        const bool hasEngineAPlanes =
            source.enginePlane[0][0] != nullptr
            && source.enginePlane[0][1] != nullptr
            && source.enginePlane[0][2] != nullptr;

        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> engineAPlane0{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> engineAPlane1{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> engineAControl{};
        if (hasEngineAPlanes)
        {
            constexpr std::size_t pixelBytes =
                SoftPackedFrameSnapshot::kPixelCount * sizeof(u32);
            std::memcpy(engineAPlane0.data(), source.enginePlane[0][0], pixelBytes);
            std::memcpy(engineAPlane1.data(), source.enginePlane[0][1], pixelBytes);
            std::memcpy(engineAControl.data(), source.enginePlane[0][2], pixelBytes);
        }

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
                    if (displayMode != 1u)
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
        auto plane0MatchesEngineA =
            [&](const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& plane0) {
                if (!hasEngineAPlanes)
                    return true;
                std::size_t compared = 0;
                std::size_t matches = 0;
                for (std::size_t i = 0; i < SoftPackedFrameSnapshot::kPixelCount; i += 16u)
                {
                    ++compared;
                    if (plane0[i] == engineAPlane0[i])
                        ++matches;
                }
                return compared == 0u || (matches * 4u) >= (compared * 3u);
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
        auto applyLiveEngineAPlanes =
            [&](std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane0,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane1,
                std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetControl) {
                targetPlane0 = engineAPlane0;
                targetPlane1 = engineAPlane1;
                targetControl = engineAControl;
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

        // Re-anchor the LCD that currently hosts Engine A to live EnginePlanes[0].
        if (hasEngineAPlanes && screenHasMeaningfulContent(engineAPlane0))
        {
            if (engineAOnTop)
            {
                if (!plane0MatchesEngineA(destination.packedTopPlane0))
                {
                    applyLiveEngineAPlanes(
                        destination.packedTopPlane0,
                        destination.packedTopPlane1,
                        destination.packedTopControl);
                }
            }
            else if (!plane0MatchesEngineA(destination.packedBottomPlane0))
            {
                applyLiveEngineAPlanes(
                    destination.packedBottomPlane0,
                    destination.packedBottomPlane1,
                    destination.packedBottomControl);
            }
        }

        if (engineAOnTop)
        {
            const auto& storePlane0 = hasEngineAPlanes ? engineAPlane0 : destination.packedTopPlane0;
            const auto& storePlane1 = hasEngineAPlanes ? engineAPlane1 : destination.packedTopPlane1;
            const auto& storeControl = hasEngineAPlanes ? engineAControl : destination.packedTopControl;
            if (screenHasMeaningfulContent(storePlane0)
                || screenIsScreenWideCaptureBackedComp4(
                    storePlane0,
                    storePlane1,
                    storeControl,
                    destination.packedTopLineMeta))
            {
                storeEngineACache(
                    engineATop,
                    storePlane0,
                    storePlane1,
                    storeControl,
                    destination.packedTopLineMeta);
            }

            const bool currentTopHasExplicitCompositedContent =
                screenHasExplicitCompositedContent(
                    destination.packedTopPlane0,
                    destination.packedTopPlane1);
            const bool cachedTopHasStructured2DOnlyContent =
                screenHasStructured2DOnlyContent(engineATop.plane0, engineATop.control);
            const bool shouldRepairTopFromCache = engineATop.valid
                && cachedTopHasStructured2DOnlyContent
                && (
                    (!currentTopHasExplicitCompositedContent
                        && screenUses3dCaptureMeta(destination.packedTopLineMeta))
                    || (isInAlternatingMode
                        && screenUses3dCaptureMeta(destination.packedTopLineMeta)
                        && !plane0MatchesEngineA(destination.packedTopPlane0)));
            if (shouldRepairTopFromCache)
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
                screenHasStructured2DOnlyContent(engineABottom.plane0, engineABottom.control);
            const bool shouldReplaceBottom = engineABottom.valid
                && (
                    (!isInAlternatingMode && !currentBottomHasExplicitContent)
                    || (isInAlternatingMode
                        && (cachedBottomIsScreenWideCaptureBackedComp4
                            || (!captureBackedHasStructured2DSource
                                && cachedBottomHasStructured2DOnlyContent
                                && (!currentBottomHasExplicitContent
                                    || screenUses3dCaptureMeta(destination.packedBottomLineMeta))))));
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
            const auto& storePlane0 = hasEngineAPlanes ? engineAPlane0 : destination.packedBottomPlane0;
            const auto& storePlane1 = hasEngineAPlanes ? engineAPlane1 : destination.packedBottomPlane1;
            const auto& storeControl = hasEngineAPlanes ? engineAControl : destination.packedBottomControl;
            if (screenHasMeaningfulContent(storePlane0)
                || screenIsScreenWideCaptureBackedComp4(
                    storePlane0,
                    storePlane1,
                    storeControl,
                    destination.packedBottomLineMeta))
            {
                storeEngineACache(
                    engineABottom,
                    storePlane0,
                    storePlane1,
                    storeControl,
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
                screenHasStructured2DOnlyContent(engineATop.plane0, engineATop.control);
            const bool shouldReplaceTop = engineATop.valid
                && (
                    (!isInAlternatingMode && !currentTopHasExplicitContent)
                    || (isInAlternatingMode
                        && (cachedTopIsScreenWideCaptureBackedComp4
                            || (!captureBackedHasStructured2DSource
                                && cachedTopHasStructured2DOnlyContent
                                && (!currentTopHasExplicitContent
                                    || screenUses3dCaptureMeta(destination.packedTopLineMeta))))));
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
            const bool shouldRepairBottomFromCache = engineABottom.valid
                && cachedBottomHasStructured2DOnlyContent
                && (
                    (!currentBottomHasExplicitCompositedContent
                        && screenUses3dCaptureMeta(destination.packedBottomLineMeta))
                    || (isInAlternatingMode
                        && screenUses3dCaptureMeta(destination.packedBottomLineMeta)
                        && !plane0MatchesEngineA(destination.packedBottomPlane0)));
            if (shouldRepairBottomFromCache)
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

    // Auxiliary same-phase hole fill only after Engine A restore.
    if (source.renderer3dReferenceValid && matchingHistory.valid)
    {
        for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
        {
            auto recoverScreenLine = [&](bool top) {
                auto& plane0 = top ? destination.packedTopPlane0 : destination.packedBottomPlane0;
                auto& plane1 = top ? destination.packedTopPlane1 : destination.packedBottomPlane1;
                auto& control = top ? destination.packedTopControl : destination.packedBottomControl;
                const auto& screenNeedsCapture3d = top
                    ? destination.topScreenNeedsCapture3dMask
                    : destination.bottomScreenNeedsCapture3dMask;
                const bool captureLine = screenNeedsCapture3d[y] != 0u;
                if (!captureLine)
                    return;
                const bool hasPayload = lineHasUsefulPixel(plane0, y)
                    || lineHasUsefulPixel(plane1, y)
                    || lineHasStructuredSlot(control, y);
                if (!hasPayload)
                    copyLine(matchingHistory.snapshot, top, y, destination);
            };
            recoverScreenLine(true);
            recoverScreenLine(false);
        }
    }

    // Promote capture-image-only screens to structured slots after Engine A
    // cache restore and same-phase history recovery (Sapphire
    // promoteLowresCaptureImageToStructuredSlot).
    {
        int combinedCaptureMaskLines = 0;
        for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
        {
            if (destination.topScreenNeedsCapture3dMask[y] != 0u
                || destination.bottomScreenNeedsCapture3dMask[y] != 0u)
            {
                ++combinedCaptureMaskLines;
            }
        }
        const bool partialCapture3dMask =
            combinedCaptureMaskLines > 0
            && combinedCaptureMaskLines < static_cast<int>(SoftPackedFrameSnapshot::kLineCount);

        const CaptureLineCounts topLineCounts = countCaptureLineCounts(destination.packedTopLineMeta);
        const CaptureLineCounts bottomLineCounts = countCaptureLineCounts(destination.packedBottomLineMeta);
        const bool allowClass4VramAlternation =
            destination.captureBackedClass4Only && screenSwapToggledThisFrame;
        promoteLowresCaptureImageToStructuredSlot(
            destination.packedTopPlane0,
            destination.packedTopPlane1,
            destination.packedTopControl,
            destination.packedTopLineMeta,
            previousSnapshot.valid ? &previousSnapshot.packedTopControl : nullptr,
            isInAlternatingMode,
            allowClass4VramAlternation,
            partialCapture3dMask,
            topLineCounts.regularCaptureLines,
            bottomLineCounts.regularCaptureLines,
            bottomLineCounts.vramCaptureLines);
        promoteLowresCaptureImageToStructuredSlot(
            destination.packedBottomPlane0,
            destination.packedBottomPlane1,
            destination.packedBottomControl,
            destination.packedBottomLineMeta,
            previousSnapshot.valid ? &previousSnapshot.packedBottomControl : nullptr,
            isInAlternatingMode,
            allowClass4VramAlternation,
            partialCapture3dMask,
            bottomLineCounts.regularCaptureLines,
            topLineCounts.regularCaptureLines,
            topLineCounts.vramCaptureLines);
    }

    collectStats(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta,
        destination.topScreenStats);
    collectStats(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta,
        destination.bottomScreenStats);

    populateComp4Placeholder(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        true,
        destination,
        destination.comp4TopPlaceholder);
    populateComp4Placeholder(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        false,
        destination,
        destination.comp4BottomPlaceholder);

    if (source.renderer3dReferenceValid)
    {
        matchingHistory.snapshot = destination;
        matchingHistory.generation = source.generation;
        matchingHistory.valid = true;
    }
    if (source.renderer3dReferenceValid
        && destination.captureScreenSwapValid
        && destination.hasCapture3dSource)
    {
        const std::size_t completedCapturePhaseIndex =
            (source.physicalScreenSwap ? 2u : 0u)
            + (destination.captureScreenSwap ? 1u : 0u);
        PhaseHistory& completedCaptureHistory =
            capturePhaseHistory[completedCapturePhaseIndex];
        if (!completedCaptureHistory.valid || source.generation >= completedCaptureHistory.generation)
        {
            completedCaptureHistory.snapshot = destination;
            completedCaptureHistory.generation = source.generation;
            completedCaptureHistory.valid = true;
        }
    }
    previousSnapshot = destination;
    return true;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
