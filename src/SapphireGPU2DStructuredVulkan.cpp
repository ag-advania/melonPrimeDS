/*
    Sapphire GPU2D structured Vulkan capture path ported for MelonPrimeDS.
    Source: SapphireRhodonite/melonDS-android-lib @ d77944275fa61f9b79cfcead2c3e98993429a023
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU_Soft.h"
#include "GPU_ColorOp.h"
#include "VulkanDesktopCompat.h"
#include "GPU2D_Soft.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace melonDS
{
namespace
{
constexpr u32 kStructuredVulkan2DSlot3DFlag = 0x40u;
constexpr u32 kStructuredVulkan2DAbove3DFlag = 0x80u;
constexpr u32 kStructuredVulkan2DOnlyFlag = 0x80u;
constexpr u32 kStructuredVulkan2DProtectedBlackFlag = 0x20u;
constexpr u32 kStructuredVulkan2DNo3DCoverageFlag = 0x10u;
constexpr u32 kStructuredVulkan2D3DPlaceholder = 0x20000000u;

u32 StructuredVulkan2DSourceClass(u32 value)
{
    const u32 flags = value >> 24u;
    if (flags == 0u || flags == 0x20u)
        return 0u;
    if ((flags & 0xC0u) == 0x40u)
        return 0u;
    if ((flags & 0x80u) != 0u || (flags & 0x10u) != 0u)
        return 0x10u;
    return flags & 0x0Fu;
}

bool StructuredVulkan2DHas3DSlot(u32 value)
{
    const u32 flags = value >> 24u;
    return (flags & 0xC0u) == 0x40u;
}

bool StructuredVulkan2DIsReal2D(u32 value)
{
    return StructuredVulkan2DSourceClass(value) != 0u;
}

bool StructuredVulkan2DSourceIsReal2D(u32 sourceClass)
{
    return sourceClass != 0u;
}

bool StructuredVulkan2DIsOpaqueBlack(u32 value)
{
    return value != 0u
        && (value >> 24u) != 0x40u
        && (value & 0x00FFFFFFu) == 0u;
}

} // anonymous namespace

const u32* SoftRenderer::GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept
{
    if (!UseStructuredVulkan2D() || plane >= kStructuredPlaneCount)
        return nullptr;

    const size_t screenIndex = topScreen ? 0u : 1u;
    const size_t offset =
        ((screenIndex * kStructuredPlaneCount) + static_cast<size_t>(plane)) * kStructuredPixelCount;
    return StructuredVulkan2DPlanes.data() + offset;
}

void SoftRenderer::ClearStructuredVulkan2DState() noexcept
{
    SapphireDebugCaptureStats = {};
    HasLastDebugCapture3dSource = false;
    std::fill_n(LastDebugCapture3dSource, kStructuredPixelCount, 0u);
    CaptureLineUses3d.fill(0);
    StructuredVulkan2DCaptureSourceLine.fill(0);
    StructuredVulkan2DCaptureSourceLineValid = false;
    StructuredVulkan2DCaptureSourceLineY = 0;
    StructuredVulkan2DPlanes.fill(0);
    StructuredVulkan2DCapturePlanes.fill(0);
    StructuredVulkan2DCaptureLineValid.fill(0);
}

void SoftRenderer::ClearStructuredVulkan2DLine(u32 line)
{
    if (!UseStructuredVulkan2D() || line >= kStructuredScreenHeight)
        return;

    const size_t screenIndex = StructuredVulkan2DCurrentLineTargetsTop ? 0u : 1u;
    const size_t rowBase = static_cast<size_t>(line) * kStructuredScreenWidth;
    const size_t screenBase = screenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    for (size_t plane = 0; plane < kStructuredPlaneCount; plane++)
    {
        std::fill_n(
            StructuredVulkan2DPlanes.data() + screenBase + (plane * kStructuredPixelCount) + rowBase,
            kStructuredScreenWidth,
            0u);
    }
}

void SoftRenderer::ClearStructuredVulkan2DCapture(u32 vramBank)
{
    if (!UseStructuredVulkan2D() || vramBank >= 4u)
        return;

    const size_t screenBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    std::fill_n(
        StructuredVulkan2DCapturePlanes.data() + screenBase,
        kStructuredPlaneCount * kStructuredPixelCount,
        0u);
    std::fill_n(
        StructuredVulkan2DCaptureLineValid.data() + (static_cast<size_t>(vramBank) * kStructuredScreenHeight),
        kStructuredScreenHeight,
        0u);
}

void SoftRenderer::ClearStructuredVulkan2DCaptureRange(u32 vramBank, u32 dstAddress, u32 width)
{
    if (!UseStructuredVulkan2D() || vramBank >= 4u)
        return;

    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const u32 clearWidth = std::min<u32>(width, kStructuredScreenWidth);
    for (u32 x = 0; x < clearWidth; x++)
    {
        const u32 captureAddress = (dstAddress + x) & 0xFFFFu;
        if (captureAddress >= kStructuredPixelCount)
            continue;

        const size_t captureIndex = static_cast<size_t>(captureAddress);
        for (size_t plane = 0; plane < kStructuredPlaneCount; plane++)
            StructuredVulkan2DCapturePlanes[captureBase + (plane * kStructuredPixelCount) + captureIndex] = 0u;

        StructuredVulkan2DCaptureLineValid[
            (static_cast<size_t>(vramBank) * kStructuredScreenHeight)
                + (captureIndex / kStructuredScreenWidth)] = 0u;
    }
}

void SoftRenderer::SaveStructuredVulkan2DCaptureSourceLine(u32 line)
{
    if (!UseStructuredVulkan2D() || line >= kStructuredScreenHeight)
        return;

    const bool sourceTop = CurrentUnitTargetsTopScreen();
    const size_t sourceScreenIndex = sourceTop ? 0u : 1u;
    const size_t sourceBase = sourceScreenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t sourceRowBase = static_cast<size_t>(line) * kStructuredScreenWidth;
    for (size_t plane = 0; plane < kStructuredPlaneCount; plane++)
    {
        std::memcpy(
            StructuredVulkan2DCaptureSourceLine.data() + (plane * kStructuredScreenWidth),
            StructuredVulkan2DPlanes.data() + sourceBase + (plane * kStructuredPixelCount) + sourceRowBase,
            kStructuredScreenWidth * sizeof(u32));
    }
    StructuredVulkan2DCaptureSourceLineY = line;
    StructuredVulkan2DCaptureSourceLineValid = true;
}

void SoftRenderer::CopyStructuredVulkan2DCaptureSourceLineToCapture(
    u32 line,
    u32 vramBank,
    u32 dstAddress,
    u32 width)
{
    if (!UseStructuredVulkan2D()
        || !StructuredVulkan2DCaptureSourceLineValid
        || StructuredVulkan2DCaptureSourceLineY != line
        || vramBank >= 4u)
    {
        return;
    }

    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const u32 copyWidth = std::min<u32>(width, kStructuredScreenWidth);
    SapphireDebugCaptureStats.StructuredCopyLines++;
    for (u32 x = 0; x < copyWidth; x++)
    {
        const u32 captureAddress = (dstAddress + x) & 0xFFFFu;
        if (captureAddress >= kStructuredPixelCount)
            continue;

        const size_t captureIndex = static_cast<size_t>(captureAddress);
        const u32 sourcePlane0 = StructuredVulkan2DCaptureSourceLine[static_cast<size_t>(x)];
        const u32 sourcePlane1 =
            StructuredVulkan2DCaptureSourceLine[kStructuredScreenWidth + static_cast<size_t>(x)];
        const u32 sourceControl =
            StructuredVulkan2DCaptureSourceLine[(kStructuredScreenWidth * 2u) + static_cast<size_t>(x)];
        if (sourcePlane0 != 0u)
            SapphireDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
        if (sourcePlane1 != 0u)
            SapphireDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
        const u32 sourceControlAlpha = sourceControl >> 24u;
        const bool structuredSlot = (sourceControlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
        if (structuredSlot)
            SapphireDebugCaptureStats.StructuredCopySlotPixels++;
        if (structuredSlot && (sourceControlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
            SapphireDebugCaptureStats.StructuredCopyAbovePixels++;
        if (!structuredSlot && (sourceControlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
            SapphireDebugCaptureStats.StructuredCopy2DOnlyPixels++;

        StructuredVulkan2DCapturePlanes[captureBase + captureIndex] = sourcePlane0;
        StructuredVulkan2DCapturePlanes[captureBase + kStructuredPixelCount + captureIndex] = sourcePlane1;
        StructuredVulkan2DCapturePlanes[captureBase + (kStructuredPixelCount * 2u) + captureIndex] = sourceControl;
        StructuredVulkan2DCaptureLineValid[
            (static_cast<size_t>(vramBank) * kStructuredScreenHeight)
                + (captureIndex / kStructuredScreenWidth)] = 1u;
    }
}

void SoftRenderer::CopyStructuredVulkan2DCurrentLineToCapture(u32 line, u32 vramBank, u32 dstAddress, u32 width)
{
    if (!UseStructuredVulkan2D()
        || line >= kStructuredScreenHeight
        || vramBank >= 4u)
    {
        return;
    }

    const bool sourceTop = CurrentUnitTargetsTopScreen();
    const size_t sourceScreenIndex = sourceTop ? 0u : 1u;
    const size_t sourceBase = sourceScreenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t sourceRowBase = static_cast<size_t>(line) * kStructuredScreenWidth;
    const u32 copyWidth = std::min<u32>(width, kStructuredScreenWidth);
    SapphireDebugCaptureStats.StructuredCopyLines++;
    for (u32 x = 0; x < copyWidth; x++)
    {
        const u32 captureAddress = (dstAddress + x) & 0xFFFFu;
        if (captureAddress >= kStructuredPixelCount)
            continue;

        const size_t sourceIndex = sourceRowBase + static_cast<size_t>(x);
        const size_t captureIndex = static_cast<size_t>(captureAddress);
        const u32 sourcePlane0 = StructuredVulkan2DPlanes[sourceBase + sourceIndex];
        const u32 sourcePlane1 = StructuredVulkan2DPlanes[sourceBase + kStructuredPixelCount + sourceIndex];
        const u32 sourceControl = StructuredVulkan2DPlanes[sourceBase + (kStructuredPixelCount * 2u) + sourceIndex];
        if (sourcePlane0 != 0u)
            SapphireDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
        if (sourcePlane1 != 0u)
            SapphireDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
        const u32 sourceControlAlpha = sourceControl >> 24u;
        const bool structuredSlot = (sourceControlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
        if (structuredSlot)
            SapphireDebugCaptureStats.StructuredCopySlotPixels++;
        if (structuredSlot && (sourceControlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
            SapphireDebugCaptureStats.StructuredCopyAbovePixels++;
        if (!structuredSlot && (sourceControlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
            SapphireDebugCaptureStats.StructuredCopy2DOnlyPixels++;
        for (size_t plane = 0; plane < kStructuredPlaneCount; plane++)
        {
            StructuredVulkan2DCapturePlanes[captureBase + (plane * kStructuredPixelCount) + captureIndex] =
                StructuredVulkan2DPlanes[sourceBase + (plane * kStructuredPixelCount) + sourceIndex];
        }
        StructuredVulkan2DCaptureLineValid[
            (static_cast<size_t>(vramBank) * kStructuredScreenHeight)
                + (captureIndex / kStructuredScreenWidth)] = 1u;
    }
}

void SoftRenderer::CopyStructuredVulkan2DCaptureLineToCurrentScreen(u32 line, u32 vramBank)
{
    if (!UseStructuredVulkan2D()
        || line >= kStructuredScreenHeight
        || vramBank >= 4u
        || StructuredVulkan2DCaptureLineValid[(static_cast<size_t>(vramBank) * kStructuredScreenHeight) + line] == 0u)
    {
        return;
    }

    const size_t screenIndex = StructuredVulkan2DCurrentLineTargetsTop ? 0u : 1u;
    const size_t screenBase = screenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t rowBase = static_cast<size_t>(line) * kStructuredScreenWidth;
    for (size_t plane = 0; plane < kStructuredPlaneCount; plane++)
    {
        std::memcpy(
            StructuredVulkan2DPlanes.data() + screenBase + (plane * kStructuredPixelCount) + rowBase,
            StructuredVulkan2DCapturePlanes.data() + captureBase + (plane * kStructuredPixelCount) + rowBase,
            kStructuredScreenWidth * sizeof(u32));
    }
}

bool SoftRenderer::ReadStructuredVulkan2DCapture2DOverlayPixel(
    u32 vramBank,
    u32 vramAddress,
    u32& overlayPixel,
    u32& overlayControlAlpha) const noexcept
{
    overlayPixel = 0u;
    overlayControlAlpha = 0u;
    if (!UseStructuredVulkan2D() || vramBank >= 4u || vramAddress >= kStructuredPixelCount)
        return false;

    const u32 line = vramAddress / kStructuredScreenWidth;
    const size_t lineValidIndex = (static_cast<size_t>(vramBank) * kStructuredScreenHeight) + line;
    if (StructuredVulkan2DCaptureLineValid[lineValidIndex] == 0u)
        return false;

    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t captureIndex = static_cast<size_t>(vramAddress);
    const u32 belowPlane = StructuredVulkan2DCapturePlanes[captureBase + captureIndex];
    const u32 abovePlane =
        StructuredVulkan2DCapturePlanes[captureBase + kStructuredPixelCount + captureIndex];
    const u32 control =
        StructuredVulkan2DCapturePlanes[captureBase + (kStructuredPixelCount * 2u) + captureIndex];
    const u32 controlAlpha = control >> 24u;
    if (controlAlpha == 0u)
        return false;

    const bool structuredSlot = (controlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
    if (structuredSlot && (controlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u && abovePlane != 0u)
    {
        overlayPixel = abovePlane;
        overlayControlAlpha = controlAlpha;
        return true;
    }

    if (!structuredSlot && (controlAlpha & kStructuredVulkan2DOnlyFlag) != 0u && belowPlane != 0u)
    {
        overlayPixel = belowPlane;
        overlayControlAlpha = controlAlpha;
        return true;
    }

    return false;
}

void SoftRenderer::MergeStructuredVulkan2DCapture2DOverlayPixel(
    u32 vramBank,
    u32 vramAddress,
    u32 overlayPixel,
    u32 overlayControlAlpha)
{
    if (!UseStructuredVulkan2D() || vramBank >= 4u || vramAddress >= kStructuredPixelCount || overlayPixel == 0u)
        return;

    const size_t captureBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const size_t captureIndex = static_cast<size_t>(vramAddress);
    u32& belowPlane = StructuredVulkan2DCapturePlanes[captureBase + captureIndex];
    u32& abovePlane = StructuredVulkan2DCapturePlanes[captureBase + kStructuredPixelCount + captureIndex];
    u32& control =
        StructuredVulkan2DCapturePlanes[captureBase + (kStructuredPixelCount * 2u) + captureIndex];
    const u32 controlAlpha = control >> 24u;
    const bool destinationHas3DSlot = (controlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
    const u32 protectedBlack =
        overlayControlAlpha & kStructuredVulkan2DProtectedBlackFlag;

    if (destinationHas3DSlot)
    {
        abovePlane = overlayPixel;
        control = (control & 0x00FFFFFFu)
            | ((controlAlpha
                | kStructuredVulkan2DAbove3DFlag
                | protectedBlack) << 24u);
    }
    else
    {
        belowPlane = overlayPixel;
        const u32 compMode = controlAlpha & 0x0Fu;
        control = (control & 0x00FFFFFFu)
            | (((compMode <= 7u ? compMode : 5u)
                | kStructuredVulkan2DOnlyFlag
                | protectedBlack) << 24u);
    }

    const u32 line = vramAddress / kStructuredScreenWidth;
    StructuredVulkan2DCaptureLineValid[
        (static_cast<size_t>(vramBank) * kStructuredScreenHeight) + line] = 1u;
    SapphireDebugCaptureStats.StructuredCopySourceBOverlayPixels++;
}
void SoftRenderer::StoreStructuredVulkan2DPixel(
    u32 line,
    u32 x,
    u32 originalVal1,
    u32 originalVal2,
    u32 originalVal3,
    u32 legacyVal1,
    u32 legacyVal2,
    u32 legacyControl,
    u32 captureBacked3DSourceClass)
{
    if (!UseStructuredVulkan2D() || line >= kStructuredScreenHeight || x >= kStructuredScreenWidth)
        return;

    const u32 flags0 = originalVal1 >> 24u;
    const u32 flags1 = originalVal2 >> 24u;
    const u32 flags2 = originalVal3 >> 24u;
    const bool slotInPlane0 = (flags0 & 0xC0u) == 0x40u;
    const bool slotInPlane1 = (flags1 & 0xC0u) == 0x40u;
    const bool slotInPlane2 = (flags2 & 0xC0u) == 0x40u;
    const bool has3DSlot = slotInPlane0 || slotInPlane1 || slotInPlane2;
    const u32 legacyAlpha = (legacyControl >> 24u) & 0x0Fu;
    const bool legacyCompMode4 = legacyAlpha == 4u;
    const bool legacyCaptureBackedComp4 =
        legacyCompMode4
        && legacyVal1 == kStructuredVulkan2D3DPlaceholder
        && legacyVal2 == kStructuredVulkan2D3DPlaceholder;
    const size_t index = static_cast<size_t>(line) * kStructuredScreenWidth + static_cast<size_t>(x);
    const size_t screenIndex = StructuredVulkan2DCurrentLineTargetsTop ? 0u : 1u;
    const size_t screenBase = screenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    if (!has3DSlot
        && captureBacked3DSourceClass == 0u
        && !legacyCaptureBackedComp4
        && !StructuredVulkan2DIsOpaqueBlack(legacyVal1))
    {
        StructuredVulkan2DPlanes[screenBase + index] = legacyVal1;
        StructuredVulkan2DPlanes[screenBase + kStructuredPixelCount + index] = 0u;
        StructuredVulkan2DPlanes[screenBase + (kStructuredPixelCount * 2u) + index] =
            (legacyControl & 0x00FFFFFFu) | ((legacyAlpha | kStructuredVulkan2DOnlyFlag) << 24u);
        return;
    }

    const u32 sourceClass0 = StructuredVulkan2DSourceClass(originalVal1);
    const u32 sourceClass1 = StructuredVulkan2DSourceClass(originalVal2);
    const u32 sourceClass2 = StructuredVulkan2DSourceClass(originalVal3);
    const bool captureBackedSlotInPlane0 =
        captureBacked3DSourceClass != 0u
        && sourceClass0 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane1 =
        captureBacked3DSourceClass != 0u
        && sourceClass1 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane2 =
        captureBacked3DSourceClass != 0u
        && sourceClass2 == captureBacked3DSourceClass;
    const bool hasCaptureBacked3DSlot =
        !has3DSlot
        && (captureBackedSlotInPlane0 || captureBackedSlotInPlane1 || captureBackedSlotInPlane2);

    u32 belowPlane = legacyVal1;
    u32 abovePlane = 0u;
    u32 control = legacyControl;
    bool protectedBlack2D = false;

    if (has3DSlot || hasCaptureBacked3DSlot || legacyCaptureBackedComp4)
    {
        bool hasAbovePlane = false;
        if (legacyCaptureBackedComp4)
        {
            belowPlane = 0u;
        }
        else if (slotInPlane0 || captureBackedSlotInPlane0)
        {
            belowPlane = legacyVal2;
        }
        else if (slotInPlane1 || captureBackedSlotInPlane1)
        {
            belowPlane = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    StructuredVulkan2DSourceIsReal2D(sourceClass0)
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else
        {
            belowPlane = legacyVal1;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0) || StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                abovePlane = legacyVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                        || StructuredVulkan2DSourceIsReal2D(sourceClass1))
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }

        const u32 structuredAlpha = legacyAlpha
            | kStructuredVulkan2DSlot3DFlag
            | (hasAbovePlane ? kStructuredVulkan2DAbove3DFlag : 0u);
        control = (legacyControl & 0x00FFFFFFu)
            | ((structuredAlpha
                | (protectedBlack2D ? kStructuredVulkan2DProtectedBlackFlag : 0u)) << 24u);
    }
    else
    {
        protectedBlack2D =
            (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1)
                || StructuredVulkan2DSourceIsReal2D(sourceClass2))
            && StructuredVulkan2DIsOpaqueBlack(legacyVal1);
        control = (legacyControl & 0x00FFFFFFu)
            | ((legacyAlpha
                | kStructuredVulkan2DOnlyFlag
                | (protectedBlack2D ? kStructuredVulkan2DProtectedBlackFlag : 0u)) << 24u);
    }

    StructuredVulkan2DPlanes[screenBase + index] = belowPlane;
    StructuredVulkan2DPlanes[screenBase + kStructuredPixelCount + index] = abovePlane;
    StructuredVulkan2DPlanes[screenBase + (kStructuredPixelCount * 2u) + index] = control;
}

void SoftRenderer::StoreStructuredVulkan2DCapturePixel(
    u32 vramBank,
    u32 vramAddress,
    u32 originalVal1,
    u32 originalVal2,
    u32 originalVal3,
    u32 legacyVal1,
    u32 legacyVal2,
    u32 legacyControl,
    u32 external3DSourceClass,
    bool external3DSlot,
    bool external3DCoverage,
    bool allowUnclassifiedExternal3DSlot)
{
    if (!UseStructuredVulkan2D() || vramBank >= 4u || vramAddress >= kStructuredPixelCount)
        return;

    const size_t screenBase = static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
    const u32 line = vramAddress / kStructuredScreenWidth;
    const u32 x = vramAddress % kStructuredScreenWidth;
    const size_t screenIndex = screenBase + static_cast<size_t>(line) * kStructuredScreenWidth + static_cast<size_t>(x);
    const size_t lineValidIndex = (static_cast<size_t>(vramBank) * kStructuredScreenHeight) + line;

    const u32 sourceClass0 = StructuredVulkan2DSourceClass(originalVal1);
    const u32 sourceClass1 = StructuredVulkan2DSourceClass(originalVal2);
    const u32 sourceClass2 = StructuredVulkan2DSourceClass(originalVal3);
    const bool slotInPlane0 = StructuredVulkan2DHas3DSlot(originalVal1);
    const bool slotInPlane1 = StructuredVulkan2DHas3DSlot(originalVal2);
    const bool slotInPlane2 = StructuredVulkan2DHas3DSlot(originalVal3);
    const bool has3DSlot = slotInPlane0 || slotInPlane1 || slotInPlane2;
    const bool hasExternal3DSlot =
        !has3DSlot
        && external3DSlot
        && (external3DSourceClass != 0u || allowUnclassifiedExternal3DSlot);

    u32 captureBacked3DSourceClass = 0u;
    if (!has3DSlot && !hasExternal3DSlot)
    {
        if (sourceClass0 != 0x10u && sourceClass0 != 0u)
            captureBacked3DSourceClass = sourceClass0;
        else if (sourceClass1 != 0x10u && sourceClass1 != 0u)
            captureBacked3DSourceClass = sourceClass1;
        else if (sourceClass2 != 0x10u && sourceClass2 != 0u)
            captureBacked3DSourceClass = sourceClass2;
    }

    const bool captureBackedSlotInPlane0 =
        captureBacked3DSourceClass != 0u
        && sourceClass0 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane1 =
        captureBacked3DSourceClass != 0u
        && sourceClass1 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane2 =
        captureBacked3DSourceClass != 0u
        && sourceClass2 == captureBacked3DSourceClass;
    const bool hasCaptureBacked3DSlot =
        !has3DSlot
        && !hasExternal3DSlot
        && (captureBackedSlotInPlane0 || captureBackedSlotInPlane1 || captureBackedSlotInPlane2);

    u32 belowPlane = legacyVal1;
    u32 abovePlane = 0u;
    u32 control = legacyControl;
    const u32 existingAbovePlane =
        StructuredVulkan2DCapturePlanes[screenBase + kStructuredPixelCount + (screenIndex - screenBase)];
    const u32 existingControl =
        StructuredVulkan2DCapturePlanes[screenBase + (kStructuredPixelCount * 2u) + (screenIndex - screenBase)];
    const u32 existingControlAlpha = existingControl >> 24u;
    const bool existingHasStructuredAbove =
        (existingControlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u
        && (existingControlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u
        && existingAbovePlane != 0u;
    const u32 legacyAlpha = (legacyControl >> 24u) & 0x0Fu;
    const bool legacyCompMode4 = legacyAlpha == 4u;
    const bool legacyCaptureBackedComp4 =
        legacyCompMode4
        && legacyVal1 == kStructuredVulkan2D3DPlaceholder
        && legacyVal2 == kStructuredVulkan2D3DPlaceholder;
    bool protectedBlack2D = false;
    if (has3DSlot || hasExternal3DSlot || hasCaptureBacked3DSlot || legacyCaptureBackedComp4)
    {
        bool hasAbovePlane = false;
        if (legacyCaptureBackedComp4)
        {
            belowPlane = 0u;
        }
        else if (hasExternal3DSlot)
        {
            belowPlane = legacyVal2;
            if (legacyAlpha == 1u && StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    StructuredVulkan2DSourceIsReal2D(sourceClass0)
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
            else if (
                legacyAlpha == 7u
                && existingHasStructuredAbove
                && existingAbovePlane == legacyVal1)
            {
                abovePlane = existingAbovePlane;
                hasAbovePlane = true;
                protectedBlack2D =
                    (existingControlAlpha & kStructuredVulkan2DProtectedBlackFlag) != 0u;
            }
            else if (
                legacyAlpha == 7u
                && external3DSourceClass != 0u
                && sourceClass0 != external3DSourceClass
                && StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    StructuredVulkan2DSourceIsReal2D(sourceClass0)
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else if (external3DSlot && slotInPlane0)
        {
            belowPlane = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                abovePlane = legacyVal2;
                hasAbovePlane = true;
                protectedBlack2D =
                    StructuredVulkan2DSourceIsReal2D(sourceClass1)
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else if (slotInPlane0 || captureBackedSlotInPlane0)
        {
            belowPlane = legacyVal2;
        }
        else if (slotInPlane1 || captureBackedSlotInPlane1)
        {
            belowPlane = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    StructuredVulkan2DSourceIsReal2D(sourceClass0)
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else
        {
            belowPlane = legacyVal1;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0) || StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                abovePlane = legacyVal1;
                hasAbovePlane = true;
                protectedBlack2D =
                    (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                        || StructuredVulkan2DSourceIsReal2D(sourceClass1))
                    && StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }

        const u32 structuredAlpha = legacyAlpha
            | kStructuredVulkan2DSlot3DFlag
            | (hasAbovePlane ? kStructuredVulkan2DAbove3DFlag : 0u)
            | (external3DSlot && !external3DCoverage ? kStructuredVulkan2DNo3DCoverageFlag : 0u);
        control = (legacyControl & 0x00FFFFFFu)
            | ((structuredAlpha
                | (protectedBlack2D ? kStructuredVulkan2DProtectedBlackFlag : 0u)) << 24u);
    }
    else
    {
        protectedBlack2D =
            (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1)
                || StructuredVulkan2DSourceIsReal2D(sourceClass2))
            && StructuredVulkan2DIsOpaqueBlack(legacyVal1);
        control = (legacyControl & 0x00FFFFFFu)
            | ((legacyAlpha
                | kStructuredVulkan2DOnlyFlag
                | (protectedBlack2D ? kStructuredVulkan2DProtectedBlackFlag : 0u)) << 24u);
    }

    if (StructuredVulkan2DCaptureLineValid[lineValidIndex] == 0u)
        SapphireDebugCaptureStats.StructuredCopyLines++;
    if (belowPlane != 0u)
        SapphireDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
    if (abovePlane != 0u)
        SapphireDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
    const u32 controlAlpha = control >> 24u;
    const bool structuredSlot = (controlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
    if (structuredSlot)
        SapphireDebugCaptureStats.StructuredCopySlotPixels++;
    if (structuredSlot && (controlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
        SapphireDebugCaptureStats.StructuredCopyAbovePixels++;
    if (!structuredSlot && (controlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
        SapphireDebugCaptureStats.StructuredCopy2DOnlyPixels++;

    StructuredVulkan2DCapturePlanes[screenIndex] = belowPlane;
    StructuredVulkan2DCapturePlanes[screenBase + kStructuredPixelCount + (screenIndex - screenBase)] = abovePlane;
    StructuredVulkan2DCapturePlanes[screenBase + (kStructuredPixelCount * 2u) + (screenIndex - screenBase)] = control;
    StructuredVulkan2DCaptureLineValid[lineValidIndex] = 1u;
}

void SoftRenderer::DoCaptureStructured(u32 line, u32 width, u32 sourceLine)
{
    u32* const captureBGOBJLine = GetCaptureBGOBJLine();
    u32* Capture3DLine = nullptr;

    u32 captureCnt = GPU.CaptureFrameCnt;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool captureUsesDirect3D = (captureCnt & (1u << 24u)) != 0u;
    bool captureLineUses3d = false;
    bool captureLineHasUseful3dAlpha = false;
    bool captureDestinationHasNonZeroPixel = false;
    bool debugCaptureSourceReady = false;
    const bool useStructuredVulkan2D = UseStructuredVulkan2D();
    if (useStructuredVulkan2D && CaptureUnit().Num == 0 && line < CaptureLineUses3d.size())
        CaptureLineUses3d[line] = 0;
    const bool captureScreenSwap = GPU.GPU3D.RenderScreenSwapAt3D;
    const bool captureDebugEnabled = MelonDSAndroid::areRendererDebugToolsEnabled();
    const bool captureMetadataEnabled = captureDebugEnabled || useStructuredVulkan2D;
    const bool logCaptureSamples = MelonDSAndroid::areRendererDebugBgObjLogsEnabled();
    if (line == 0)
    {
        HasLastDebugCapture3dSource = false;
        std::memset(LastDebugCapture3dSource, 0, sizeof(LastDebugCapture3dSource));
        SapphireDebugCaptureStats = {};
        SapphireDebugCaptureStats.CaptureWidth = width;
        SapphireDebugCaptureStats.CaptureMode = captureMode;
        SapphireDebugCaptureStats.CaptureBit24 = (captureCnt & (1u << 24u)) != 0u ? 1u : 0u;
    }
    SapphireDebugCaptureStats.CaptureLines++;
    u32 dstvram = (captureCnt >> 16) & 0x3;

    // TODO: confirm this
    // it should work like VRAM display mode, which requires VRAM to be mapped to LCDC
    if (!(GPU.VRAMMap_LCDC & (1<<dstvram)))
        return;

    u16* dst = (u16*)GPU.VRAM[dstvram];
    u32 dstaddr = (((captureCnt >> 18) & 0x3) << 14) + (line * width);
    if (!useStructuredVulkan2D)
    {
        u32* srcA;
        if (captureCnt & (1<<24))
        {
            srcA = Capture3DLine;
        }
        else
        {
            srcA = captureBGOBJLine;
            if (GPU.GPU3D.HasCurrentRenderer())
            {
                for (int i = 0; i < 256; i++)
                {
                    u32 val1 = captureBGOBJLine[i];
                    u32 val2 = captureBGOBJLine[256+i];
                    u32 val3 = captureBGOBJLine[512+i];

                    u32 compmode = (val3 >> 24) & 0xF;

                    if (compmode == 4)
                    {
                        u32 _3dval = Capture3DLine[i];
                        if ((_3dval >> 24) > 0)
                            val1 = ColorBlend5(_3dval, val1);
                        else
                            val1 = val2;
                    }
                    else if (compmode == 1)
                    {
                        u32 _3dval = Capture3DLine[i];
                        if ((_3dval >> 24) > 0)
                        {
                            u32 eva = (val3 >> 8) & 0x1F;
                            u32 evb = (val3 >> 16) & 0x1F;

                            val1 = ColorBlend4(val1, _3dval, eva, evb);
                        }
                        else
                            val1 = val2;
                    }
                    else if (compmode <= 3)
                    {
                        u32 _3dval = Capture3DLine[i];
                        if ((_3dval >> 24) > 0)
                        {
                            u32 evy = (val3 >> 8) & 0x1F;

                            val1 = _3dval;
                            if      (compmode == 2) val1 = ColorBrightnessUp(val1, evy, 0x8);
                            else if (compmode == 3) val1 = ColorBrightnessDown(val1, evy, 0x7);
                        }
                        else
                            val1 = val2;
                    }

                    captureBGOBJLine[i] = val1;
                }
            }
        }

        u16* srcB = NULL;
        u32 srcBaddr = line * 256;

        if (captureCnt & (1<<25))
        {
            srcB = GPU.DispFIFOBuffer;
            srcBaddr = 0;
        }
        else
        {
            u32 srcvram = (CaptureUnit().DispCnt >> 18) & 0x3;
            if (GPU.VRAMMap_LCDC & (1<<srcvram))
                srcB = (u16*)GPU.VRAM[srcvram];

            if (((CaptureUnit().DispCnt >> 16) & 0x3) != 2)
                srcBaddr += ((captureCnt >> 26) & 0x3) << 14;
        }

        dstaddr &= 0xFFFF;
        srcBaddr &= 0xFFFF;

        static_assert(VRAMDirtyGranularity == 512);
        GPU.VRAMDirty[dstvram][(dstaddr * 2) / VRAMDirtyGranularity] = true;

        switch ((captureCnt >> 29) & 0x3)
        {
        case 0:
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    u32 r = (val >> 1) & 0x1F;
                    u32 g = (val >> 9) & 0x1F;
                    u32 b = (val >> 17) & 0x1F;
                    u32 a = ((val >> 24) != 0) ? 0x8000 : 0;

                    dst[dstaddr] = r | (g << 5) | (b << 10) | a;
                    dstaddr = (dstaddr + 1) & 0xFFFF;
                }
            }
            break;

        case 1:
            {
                if (srcB)
                {
                    for (u32 i = 0; i < width; i++)
                    {
                        dst[dstaddr] = srcB[srcBaddr];
                        srcBaddr = (srcBaddr + 1) & 0xFFFF;
                        dstaddr = (dstaddr + 1) & 0xFFFF;
                    }
                }
                else
                {
                    for (u32 i = 0; i < width; i++)
                    {
                        dst[dstaddr] = 0;
                        dstaddr = (dstaddr + 1) & 0xFFFF;
                    }
                }
            }
            break;

        case 2:
        case 3:
            {
                u32 eva = captureCnt & 0x1F;
                u32 evb = (captureCnt >> 8) & 0x1F;

                if (eva > 16) eva = 16;
                if (evb > 16) evb = 16;

                if (srcB)
                {
                    for (u32 i = 0; i < width; i++)
                    {
                        u32 val = srcA[i];

                        u32 rA = (val >> 1) & 0x1F;
                        u32 gA = (val >> 9) & 0x1F;
                        u32 bA = (val >> 17) & 0x1F;
                        u32 aA = ((val >> 24) != 0) ? 1 : 0;

                        val = srcB[srcBaddr];

                        u32 rB = val & 0x1F;
                        u32 gB = (val >> 5) & 0x1F;
                        u32 bB = (val >> 10) & 0x1F;
                        u32 aB = val >> 15;

                        u32 rD = ((rA * aA * eva) + (rB * aB * evb) + 8) >> 4;
                        u32 gD = ((gA * aA * eva) + (gB * aB * evb) + 8) >> 4;
                        u32 bD = ((bA * aA * eva) + (bB * aB * evb) + 8) >> 4;
                        u32 aD = (eva>0 ? aA : 0) | (evb>0 ? aB : 0);

                        if (rD > 0x1F) rD = 0x1F;
                        if (gD > 0x1F) gD = 0x1F;
                        if (bD > 0x1F) bD = 0x1F;

                        dst[dstaddr] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                        srcBaddr = (srcBaddr + 1) & 0xFFFF;
                        dstaddr = (dstaddr + 1) & 0xFFFF;
                    }
                }
                else
                {
                    for (u32 i = 0; i < width; i++)
                    {
                        u32 val = srcA[i];

                        u32 rA = (val >> 1) & 0x1F;
                        u32 gA = (val >> 9) & 0x1F;
                        u32 bA = (val >> 17) & 0x1F;
                        u32 aA = ((val >> 24) != 0) ? 1 : 0;

                        u32 rD = ((rA * aA * eva) + 8) >> 4;
                        u32 gD = ((gA * aA * eva) + 8) >> 4;
                        u32 bD = ((bA * aA * eva) + 8) >> 4;
                        u32 aD = (eva>0 ? aA : 0);

                        dst[dstaddr] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                        dstaddr = (dstaddr + 1) & 0xFFFF;
                    }
                }
            }
            break;
        }
        return;
    }

    const u32 structuredCaptureDstBase = dstaddr & 0xFFFFu;
    bool structuredCaptureStoredFromSourceA = false;

    u16* srcB = NULL;
    u32 srcBaddr = line * 256;
    u32 structuredSourceBVram = 4u;
    bool structuredSourceBFromVram = false;

    if (captureCnt & (1<<25))
    {
        srcB = GPU.DispFIFOBuffer;
        srcBaddr = 0;
    }
    else
    {
        u32 srcvram = (CaptureUnit().DispCnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcB = (u16*)GPU.VRAM[srcvram];
            structuredSourceBVram = srcvram;
            structuredSourceBFromVram = true;
        }

        if (((CaptureUnit().DispCnt >> 16) & 0x3) != 2)
            srcBaddr += ((captureCnt >> 26) & 0x3) << 14;
    }

    srcBaddr &= 0xFFFF;
    const u32 structuredSourceBBaseAddr = srcBaddr;
    const u32 sourceBEvb = (captureCnt >> 8) & 0x1Fu;
    const bool captureBlendsStructuredSourceB =
        useStructuredVulkan2D
        && captureMode >= 2u
        && sourceBEvb != 0u
        && structuredSourceBFromVram;
    std::array<u32, 256> structuredSourceBOverlayPixels {};
    std::array<u32, 256> structuredSourceBOverlayControlAlpha {};
    std::array<u16, 256> structuredCaptureOutputPixels {};
    if (captureBlendsStructuredSourceB)
    {
        const u32 sampleWidth = std::min<u32>(width, 256u);
        for (u32 i = 0; i < sampleWidth; i++)
        {
            ReadStructuredVulkan2DCapture2DOverlayPixel(
                structuredSourceBVram,
                (structuredSourceBBaseAddr + i) & 0xFFFFu,
                structuredSourceBOverlayPixels[static_cast<size_t>(i)],
                structuredSourceBOverlayControlAlpha[static_cast<size_t>(i)]);
        }
    }

    if (useStructuredVulkan2D)
        ClearStructuredVulkan2DCaptureRange(dstvram, structuredCaptureDstBase, width);

    // TODO: handle 3D in GPU3D::CurrentRenderer->Accelerated mode!!

    u32* srcA;
    if (captureUsesDirect3D)
    {
        if (captureDebugEnabled)
            SapphireDebugCaptureStats.Direct3DLines++;
        if (GPU.GPU3D.HasCurrentRenderer())
            GPU.GPU3D.GetCurrentRenderer().SetCaptureScreenSwapHint(captureScreenSwap);
        if (GPU.GPU3D.HasCurrentRenderer())
            Capture3DLine = GPU.GPU3D.GetCurrentRenderer().GetLine(static_cast<int>(sourceLine));
        srcA = Capture3DLine;
        captureLineUses3d = srcA != nullptr;
        if (captureMetadataEnabled && srcA != nullptr)
            debugCaptureSourceReady = true;
        if (captureDebugEnabled && srcA != nullptr)
        {
            for (u32 i = 0; i < width; i++)
            {
                if ((srcA[i] >> 24) != 0u)
                {
                    captureLineHasUseful3dAlpha = true;
                    break;
                }
            }
        }
    }
    else
    {
        srcA = captureBGOBJLine;
        if (GPU.GPU3D.HasCurrentRenderer())
        {
            // In accelerated mode, only fetch the 3D line if this capture line actually
            // needs 3D contribution for source A.
            const bool sourceAContributes = captureMode == 0u
                || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
            bool needs3dComposite = false;
            if (sourceAContributes)
            {
                for (int i = 0; i < 256; i++)
                {
                    const u32 compmode = (captureBGOBJLine[512 + i] >> 24) & 0xF;
                    if (captureDebugEnabled && compmode < 8u)
                        SapphireDebugCaptureStats.CompModeCounts[compmode]++;
                    if (compmode <= 4u)
                    {
                        needs3dComposite = true;
                        break;
                    }
                }
            }

            if (needs3dComposite)
            {
                if (captureDebugEnabled)
                    SapphireDebugCaptureStats.SourceACompositeLines++;
                GPU.GPU3D.GetCurrentRenderer().SetCaptureScreenSwapHint(captureScreenSwap);
                Capture3DLine = GPU.GPU3D.GetCurrentRenderer().GetLine(static_cast<int>(sourceLine));
                if (Capture3DLine)
                {
                    CopyStructuredVulkan2DCaptureSourceLineToCapture(
                        line,
                        dstvram,
                        structuredCaptureDstBase,
                        width);

                    u32 external3DSourceClass = 0u;
                    u32 external3DSourceCounts[17] {};
                    const u32 captureOutputMode = (captureCnt >> 29) & 0x3u;
                    const bool allowUnclassifiedExternal3DSlot =
                        captureOutputMode >= 2u
                        && width == 256u
                        && srcB != nullptr;

                    for (int i = 0; i < 256; i++)
                    {
                        const u32 sourceClass = StructuredVulkan2DSourceClass(captureBGOBJLine[i]);
                        if (sourceClass <= 16u)
                            external3DSourceCounts[sourceClass]++;
                    }
                    constexpr u32 sourceClasses[] = {1u, 2u, 4u, 8u};
                    u32 bestSourceCount = 0u;
                    for (u32 sourceClass : sourceClasses)
                    {
                        if (external3DSourceCounts[sourceClass] > bestSourceCount)
                        {
                            bestSourceCount = external3DSourceCounts[sourceClass];
                            external3DSourceClass = sourceClass;
                        }
                    }
                    if (bestSourceCount < 128u)
                        external3DSourceClass = 0u;

                    captureLineUses3d = true;
                    if (captureDebugEnabled)
                    {
                        for (u32 i = 0; i < width; i++)
                        {
                            if ((Capture3DLine[i] >> 24) != 0u)
                            {
                                captureLineHasUseful3dAlpha = true;
                                break;
                            }
                        }
                    }
                    struct CaptureSamplePoint
                    {
                        const char* label;
                        u32 x;
                        u32 y;
                    };
                    static constexpr CaptureSamplePoint kCaptureSamplePoints[] = {
                        {"seamA", 85u, 14u},
                        {"goodA", 84u, 14u},
                        {"seamB", 75u, 58u},
                        {"goodB", 74u, 58u},
                        {"seamC", 150u, 81u},
                        {"goodC", 149u, 81u},
                    };

                    // In accelerated mode compositing is normally done on the GPU, but
                    // display capture needs source A on CPU for VRAM writes.
                    for (int i = 0; i < 256; i++)
                    {
                        const u32 originalVal1 = captureBGOBJLine[i];
                        const u32 originalVal2 = captureBGOBJLine[256+i];
                        const u32 originalVal3 = captureBGOBJLine[512+i];
                        u32 val1 = originalVal1;
                        u32 val2 = originalVal2;
                        u32 val3 = originalVal3;

                        u32 compmode = (val3 >> 24) & 0xF;
                        const u32 _3dval = Capture3DLine[i];
                        if (captureDebugEnabled && (_3dval >> 24) > 0)
                        {
                            SapphireDebugCaptureStats.Opaque3DSourcePixels++;
                            if ((val1 & 0xFF000000u) == 0x20000000u)
                                SapphireDebugCaptureStats.Opaque3DBackdropPixels++;
                        }

                        if (compmode == 4)
                        {
                            // 3D on top, blending

                            if ((_3dval >> 24) > 0)
                                val1 = ColorBlend5(_3dval, val1);
                            else
                                val1 = val2;
                        }
                        else if (compmode == 1)
                        {
                            // 3D on bottom, blending

                            if ((_3dval >> 24) > 0)
                            {
                                u32 eva = (val3 >> 8) & 0x1F;
                                u32 evb = (val3 >> 16) & 0x1F;

                                val1 = ColorBlend4(val1, _3dval, eva, evb);
                            }
                            else
                                val1 = val2;
                        }
                        else if (compmode <= 3)
                        {
                            // 3D on top, normal/fade

                            if ((_3dval >> 24) > 0)
                            {
                                u32 evy = (val3 >> 8) & 0x1F;

                                val1 = _3dval;
                                if      (compmode == 2) val1 = ColorBrightnessUp(val1, evy, 0x8);
                                else if (compmode == 3) val1 = ColorBrightnessDown(val1, evy, 0x7);
                            }
                            else
                                val1 = val2;
                        }

                        if (logCaptureSamples)
                        {
                            for (const CaptureSamplePoint& sample : kCaptureSamplePoints)
                            {
                                if (sample.y != sourceLine || sample.x != static_cast<u32>(i))
                                    continue;

                                const u32 packedWord =
                                    ((val1 >> 1) & 0x1Fu)
                                    | (((val1 >> 9) & 0x1Fu) << 5)
                                    | (((val1 >> 17) & 0x1Fu) << 10)
                                    | (((val1 >> 24) != 0u) ? 0x8000u : 0u);

                                Platform::Log(
                                    Platform::LogLevel::Warn,
                                    "RendererDebug[CaptureLoop]: label=%s line=%u sourceLine=%u x=%u comp=%u raw3d=%08X val1=%08X val2=%08X val3=%08X packed=%08X",
                                    sample.label,
                                    line,
                                    sourceLine,
                                    static_cast<u32>(i),
                                    compmode,
                                    _3dval,
                                    val1,
                                    val2,
                                    val3,
                                    packedWord
                                );
                                break;
                            }
                        }

                        captureBGOBJLine[i] = val1;
                        StoreStructuredVulkan2DCapturePixel(
                            dstvram,
                            (structuredCaptureDstBase + static_cast<u32>(i)) & 0xFFFFu,
                            originalVal1,
                            originalVal2,
                            originalVal3,
                            val1,
                            val2,
                            val3,
                            external3DSourceClass,
                            true,
                            (_3dval >> 24u) != 0u,
                            allowUnclassifiedExternal3DSlot);
                        structuredCaptureStoredFromSourceA = true;
                    }

                    debugCaptureSourceReady = true;
                }
            }
        }
    }

    dstaddr &= 0xFFFF;
    if (useStructuredVulkan2D && captureLineUses3d && !structuredCaptureStoredFromSourceA)
        CopyStructuredVulkan2DCurrentLineToCapture(line, dstvram, dstaddr, width);

    if (useStructuredVulkan2D && CaptureUnit().Num == 0 && line < CaptureLineUses3d.size())
        CaptureLineUses3d[line] = captureLineUses3d ? 1 : 0;

    if (captureMetadataEnabled && captureLineUses3d && debugCaptureSourceReady && srcA != nullptr)
    {
        std::memcpy(
            &LastDebugCapture3dSource[static_cast<size_t>(sourceLine) * 256u],
            srcA,
            256u * sizeof(u32));
        HasLastDebugCapture3dSource = true;
    }

    static_assert(VRAMDirtyGranularity == 512);
    GPU.VRAMDirty[dstvram][(dstaddr * 2) / VRAMDirtyGranularity] = true;

    auto packCaptureColor = [](u32 val) -> u16 {
        u32 r = (val >> 1) & 0x1F;
        u32 g = (val >> 9) & 0x1F;
        u32 b = (val >> 17) & 0x1F;
        u32 a = ((val >> 24) != 0) ? 0x8000 : 0;
        return static_cast<u16>(r | (g << 5) | (b << 10) | a);
    };
    auto captureColorsClose = [](u16 lhs, u16 rhs) -> bool {
        if (((lhs ^ rhs) & 0x8000u) != 0u)
            return false;

        const int lhsR = lhs & 0x1F;
        const int lhsG = (lhs >> 5) & 0x1F;
        const int lhsB = (lhs >> 10) & 0x1F;
        const int rhsR = rhs & 0x1F;
        const int rhsG = (rhs >> 5) & 0x1F;
        const int rhsB = (rhs >> 10) & 0x1F;
        return std::abs(lhsR - rhsR) <= 2
            && std::abs(lhsG - rhsG) <= 2
            && std::abs(lhsB - rhsB) <= 2;
    };

    switch ((captureCnt >> 29) & 0x3)
    {
    case 0: // source A
        {
            for (u32 i = 0; i < width; i++)
            {
                u32 val = srcA[i];

                // TODO: check what happens when alpha=0

                const u16 packed = packCaptureColor(val);
                structuredCaptureOutputPixels[static_cast<size_t>(i)] = packed;
                dst[dstaddr] = packed;
                if (GPU.GPU3D.HasCurrentRenderer())
                {
                    if (packed != 0)
                    {
                        SapphireDebugCaptureStats.SourceAOutputUsefulPixels++;
                        if ((packed & 0x7FFFu) != 0u)
                            SapphireDebugCaptureStats.SourceAOutputVisiblePixels++;
                        else
                            SapphireDebugCaptureStats.SourceAOutputOpaqueBlackPixels++;
                    }
                }
                if (packed != 0)
                    captureDestinationHasNonZeroPixel = true;
                dstaddr = (dstaddr + 1) & 0xFFFF;
            }
        }
        break;

    case 1: // source B
        {
            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                {
                    const u16 packed = srcB[srcBaddr];
                    structuredCaptureOutputPixels[static_cast<size_t>(i)] = packed;
                    dst[dstaddr] = packed;
                    if (packed != 0)
                        captureDestinationHasNonZeroPixel = true;
                    srcBaddr = (srcBaddr + 1) & 0xFFFF;
                    dstaddr = (dstaddr + 1) & 0xFFFF;
                }
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                {
                    dst[dstaddr] = 0;
                    dstaddr = (dstaddr + 1) & 0xFFFF;
                }
            }
        }
        break;

    case 2: // sources A+B
    case 3:
        {
            u32 eva = captureCnt & 0x1F;
            u32 evb = (captureCnt >> 8) & 0x1F;

            // checkme
            if (eva > 16) eva = 16;
            if (evb > 16) evb = 16;

            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    // TODO: check what happens when alpha=0

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    val = srcB[srcBaddr];

                    u32 rB = val & 0x1F;
                    u32 gB = (val >> 5) & 0x1F;
                    u32 bB = (val >> 10) & 0x1F;
                    u32 aB = val >> 15;

                    u32 rD = ((rA * aA * eva) + (rB * aB * evb) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + (gB * aB * evb) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + (bB * aB * evb) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0) | (evb>0 ? aB : 0);

                    if (rD > 0x1F) rD = 0x1F;
                    if (gD > 0x1F) gD = 0x1F;
                    if (bD > 0x1F) bD = 0x1F;

                    const u16 packed = rD | (gD << 5) | (bD << 10) | (aD << 15);
                    structuredCaptureOutputPixels[static_cast<size_t>(i)] = packed;
                    dst[dstaddr] = packed;
                    if (packed != 0)
                        captureDestinationHasNonZeroPixel = true;
                    srcBaddr = (srcBaddr + 1) & 0xFFFF;
                    dstaddr = (dstaddr + 1) & 0xFFFF;
                }
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    // TODO: check what happens when alpha=0

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    u32 rD = ((rA * aA * eva) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0);

                    const u16 packed = rD | (gD << 5) | (bD << 10) | (aD << 15);
                    structuredCaptureOutputPixels[static_cast<size_t>(i)] = packed;
                    dst[dstaddr] = packed;
                    if (packed != 0)
                        captureDestinationHasNonZeroPixel = true;
                    dstaddr = (dstaddr + 1) & 0xFFFF;
                }
            }
        }
        break;
    }

    if (captureBlendsStructuredSourceB)
    {
        const u32 mergeWidth = std::min<u32>(width, 256u);
        for (u32 i = 0; i < mergeWidth; i++)
        {
            const u32 overlayPixel = structuredSourceBOverlayPixels[static_cast<size_t>(i)];
            if (overlayPixel == 0u)
                continue;
            const u16 overlayPacked = packCaptureColor(overlayPixel);
            const u16 outputPacked = structuredCaptureOutputPixels[static_cast<size_t>(i)];
            if (!captureColorsClose(overlayPacked, outputPacked))
                continue;

            MergeStructuredVulkan2DCapture2DOverlayPixel(
                dstvram,
                (structuredCaptureDstBase + i) & 0xFFFFu,
                overlayPixel,
                structuredSourceBOverlayControlAlpha[static_cast<size_t>(i)]);
        }
    }

    if (captureMetadataEnabled && captureLineUses3d)
        SapphireDebugCaptureStats.CaptureLineUses3dLines++;

    if (captureDebugEnabled)
    {
        if (captureLineHasUseful3dAlpha)
            SapphireDebugCaptureStats.CaptureLineUsefulAlphaLines++;
        if (!captureDestinationHasNonZeroPixel)
            SapphireDebugCaptureStats.CaptureDestinationBlankLines++;
    }
}

bool SoftRenderer::CurrentUnitTargetsTopScreen() const noexcept
{
    return ScreenIndexForEngine(StructuredVulkan2DCurrentEngine) == 0u;
}

GPU2D& SoftRenderer::CaptureUnit() noexcept
{
    return GPU.GPU2D_A;
}

const GPU2D& SoftRenderer::CaptureUnit() const noexcept
{
    return GPU.GPU2D_A;
}

bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GetRenderer3D().UsesStructured2DMetadata();
}

void SoftRenderer::BeginStructuredVulkan2DLine(u32 engine, u32 line) noexcept
{
    StructuredVulkan2DCurrentEngine = engine;
    StructuredVulkan2DCurrentLineTargetsTop = ScreenIndexForEngine(engine) == 0u;
    ClearStructuredVulkan2DLine(line);
}

u32* SoftRenderer::GetCaptureBGOBJLine() noexcept
{
    auto* rend2d = dynamic_cast<SoftRenderer2D*>(Rend2D_A.get());
    return rend2d != nullptr ? rend2d->BGOBJLineForCapture() : nullptr;
}

} // namespace melonDS

#endif
