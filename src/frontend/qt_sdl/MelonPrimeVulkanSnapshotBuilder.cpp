#include "MelonPrimeVulkanSnapshotBuilder.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstring>

namespace MelonPrime
{
namespace
{

constexpr u32 kPacked3dPlaceholder = 0x20000000u;
constexpr u32 kMetaRegularCaptureUses3d = 1u << 21u;
constexpr u32 kMetaVramCaptureUses3d = 1u << 22u;
constexpr u32 kMetaForceLive3dCompMode7 = 1u << 18u;
constexpr u32 kCaptureMetaMask =
    kMetaRegularCaptureUses3d
    | kMetaVramCaptureUses3d
    | kMetaForceLive3dCompMode7;

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
    const SoftPackedFrameSnapshot* phaseHistory,
    bool top,
    SoftPackedFrameSnapshot& snapshot,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& placeholder)
{
    const auto* historyPlane0 = phaseHistory == nullptr
        ? nullptr
        : (top ? &phaseHistory->packedTopPlane0 : &phaseHistory->packedBottomPlane0);
    for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
    {
        const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
        const bool currentCaptureLine = snapshot.hasCapture3dSource
            && snapshot.captureLineUses3dMask[y] != 0u;
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
            if (currentCaptureLine)
                value = snapshot.capture3dSourceDsFrame[index];
            if (!packedPixelIsUseful(value) && historyPlane0 != nullptr)
                value = (*historyPlane0)[index];
            if (packedPixelIsUseful(value))
                placeholder[index] = value;
        }
    }
}

} // namespace

void MelonPrimeVulkanSnapshotBuilder::reset() noexcept
{
    for (PhaseHistory& history : phaseHistory)
    {
        history.snapshot.clear();
        history.generation = 0;
        history.valid = false;
    }
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
        if (source.lineMeta[screen] == nullptr)
            return false;
        for (std::size_t plane = 0; plane < 3u; ++plane)
        {
            if (source.plane[screen][plane] == nullptr)
                return false;
        }
    }

    PhaseHistory& matchingHistory = phaseHistory[source.screenSwap ? 1u : 0u];
    if (matchingHistory.valid && source.generation < matchingHistory.generation)
        return false;
    if (matchingHistory.valid && source.generation == matchingHistory.generation)
    {
        destination = matchingHistory.snapshot;
        destination.frameId = frameId;
        return true;
    }

    destination.clear();
    destination.frameId = frameId;
    destination.frontBufferLatched = source.frontBuffer;
    destination.screenSwapLatched = source.screenSwap;
    destination.captureBackedClass4Only = source.captureBackedClass4Only;
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
    if (source.captureLineUses3d != nullptr)
    {
        std::memcpy(
            destination.captureLineUses3dMask.data(),
            source.captureLineUses3d,
            destination.captureLineUses3dMask.size() * sizeof(u8));
    }

    // A ScreenSwap-toggling title alternates ownership every frame. Only the
    // last snapshot from the same ownership phase is eligible for recovery;
    // using the immediately previous opposite phase is what swaps the screens.
    if (matchingHistory.valid)
    {
        for (std::size_t y = 0; y < SoftPackedFrameSnapshot::kLineCount; ++y)
        {
            auto recoverScreenLine = [&](bool top) {
                auto& plane0 = top ? destination.packedTopPlane0 : destination.packedBottomPlane0;
                auto& plane1 = top ? destination.packedTopPlane1 : destination.packedBottomPlane1;
                auto& control = top ? destination.packedTopControl : destination.packedBottomControl;
                auto& lineMeta = top ? destination.packedTopLineMeta : destination.packedBottomLineMeta;
                const bool captureLine = (lineMeta[y] & kCaptureMetaMask) != 0u;
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

            const bool currentCaptureLine = destination.captureLineUses3dMask[y] != 0u;
            const bool currentCaptureUseful = destination.hasCapture3dSource
                && lineHasUsefulPixel(destination.capture3dSourceDsFrame, y);
            if (currentCaptureLine && !currentCaptureUseful
                && matchingHistory.snapshot.hasCapture3dSource
                && lineHasUsefulPixel(matchingHistory.snapshot.capture3dSourceDsFrame, y))
            {
                const std::size_t rowBase = y * SoftPackedFrameSnapshot::kScreenWidth;
                std::memcpy(
                    destination.capture3dSourceDsFrame.data() + rowBase,
                    matchingHistory.snapshot.capture3dSourceDsFrame.data() + rowBase,
                    SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
                destination.captureFallbackLines[y] = 1u;
                destination.hasCapture3dSource = true;
            }
        }
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

    const SoftPackedFrameSnapshot* phaseSnapshot = matchingHistory.valid
        ? &matchingHistory.snapshot
        : nullptr;
    populateComp4Placeholder(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        phaseSnapshot,
        true,
        destination,
        destination.comp4TopPlaceholder);
    populateComp4Placeholder(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        phaseSnapshot,
        false,
        destination,
        destination.comp4BottomPlaceholder);

    matchingHistory.snapshot = destination;
    matchingHistory.generation = source.generation;
    matchingHistory.valid = true;
    return true;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
