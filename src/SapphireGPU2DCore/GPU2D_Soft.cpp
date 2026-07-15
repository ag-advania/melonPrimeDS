/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "GPU2D_Soft.h"
#include "GPU.h"
#include "GPU3D.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanDesktopCompat.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace
{
struct RendererDebugSamplePoint
{
    const char* label;
    melonDS::u32 x;
    melonDS::u32 y;
};

static constexpr RendererDebugSamplePoint kRendererDebugSamplePoints[] = {
    {"seamA", 85u, 14u},
    {"goodA", 84u, 14u},
    {"seamB", 75u, 58u},
    {"goodB", 74u, 58u},
    {"seamC", 150u, 81u},
    {"goodC", 149u, 81u},
};

const RendererDebugSamplePoint* findRendererDebugSamplePoint(melonDS::u32 x, melonDS::u32 y)
{
    for (const RendererDebugSamplePoint& sample : kRendererDebugSamplePoints)
    {
        if (sample.x == x && sample.y == y)
            return &sample;
    }
    return nullptr;
}
}

namespace melonDS
{
namespace SapphireGPU2DCore
{
namespace GPU2D
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

constexpr u32 kRenderer2DDebugFeatureStaticBackground = 1u << 0u;
constexpr u32 kRenderer2DDebugFeatureAffineBackground = 1u << 1u;
constexpr u32 kRenderer2DDebugFeatureAffineExtendedTiledBackground = 1u << 2u;
constexpr u32 kRenderer2DDebugFeatureAffineExtendedBitmap256Background = 1u << 3u;
constexpr u32 kRenderer2DDebugFeatureAffineExtendedDirectColorBackground = 1u << 4u;
constexpr u32 kRenderer2DDebugFeatureLargeScreenBackground = 1u << 5u;
constexpr u32 kRenderer2DDebugFeature3DBackground = 1u << 6u;
constexpr u32 kRenderer2DDebugFeatureRegularObject = 1u << 8u;
constexpr u32 kRenderer2DDebugFeatureAffineObject = 1u << 9u;
constexpr u32 kRenderer2DDebugFeatureTiled4BppObject = 1u << 10u;
constexpr u32 kRenderer2DDebugFeatureTiled8BppObject = 1u << 11u;
constexpr u32 kRenderer2DDebugFeatureBitmapObject = 1u << 12u;
constexpr u32 kRenderer2DDebugFeatureBlendedObject = 1u << 13u;
constexpr u32 kRenderer2DDebugFeatureWindowObject = 1u << 14u;
constexpr u32 kRenderer2DDebugFeatureMosaicObject = 1u << 15u;
constexpr u32 kRenderer2DDebugFeatureObjectUpperBand = 1u << 16u;
constexpr u32 kRenderer2DDebugFeatureObjectMiddleBand = 1u << 17u;
constexpr u32 kRenderer2DDebugFeatureObjectLowerBand = 1u << 18u;

bool Renderer2DDebugShouldDrawLayer(u32 unit, u32 bgnum)
{
    return MelonDSAndroid::isRenderer2DDebugBgLayerEnabled(unit, bgnum);
}

bool Renderer2DDebugShouldDrawFeature(u32 featureFlag)
{
    return MelonDSAndroid::isRenderer2DDebugBackgroundKindEnabled(featureFlag);
}

bool Renderer2DDebugShouldDrawTextBg(u32 unit, u32 bgnum, u16 bgcnt)
{
    return Renderer2DDebugShouldDrawLayer(unit, bgnum)
        && MelonDSAndroid::isRenderer2DDebugBgPriorityEnabled(unit, bgcnt & 0x3u)
        && Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureStaticBackground);
}

bool Renderer2DDebugShouldDrawAffineBg(u32 unit, u32 bgnum, u16 bgcnt)
{
    return Renderer2DDebugShouldDrawLayer(unit, bgnum)
        && MelonDSAndroid::isRenderer2DDebugBgPriorityEnabled(unit, bgcnt & 0x3u)
        && Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureAffineBackground);
}

bool Renderer2DDebugShouldDrawExtendedBg(u32 unit, u32 bgnum, u16 bgcnt)
{
    if (!Renderer2DDebugShouldDrawLayer(unit, bgnum))
        return false;
    if (!MelonDSAndroid::isRenderer2DDebugBgPriorityEnabled(unit, bgcnt & 0x3u))
        return false;

    if ((bgcnt & 0x0080u) == 0u)
        return Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureAffineExtendedTiledBackground);

    if ((bgcnt & 0x0004u) != 0u)
        return Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureAffineExtendedDirectColorBackground);

    return Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureAffineExtendedBitmap256Background);
}

bool Renderer2DDebugShouldDrawLargeBg(u32 unit, u16 bgcnt)
{
    return Renderer2DDebugShouldDrawLayer(unit, 2)
        && MelonDSAndroid::isRenderer2DDebugBgPriorityEnabled(unit, bgcnt & 0x3u)
        && Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeatureLargeScreenBackground);
}

bool Renderer2DDebugShouldDraw3DBg(u32 unit, u16 bgcnt)
{
    return Renderer2DDebugShouldDrawLayer(unit, 0)
        && MelonDSAndroid::isRenderer2DDebugBgPriorityEnabled(unit, bgcnt & 0x3u)
        && Renderer2DDebugShouldDrawFeature(kRenderer2DDebugFeature3DBackground);
}

bool Renderer2DDebugShouldInterleaveObjects(u32 unit, u32 priority)
{
    return MelonDSAndroid::areRenderer2DDebugObjectsEnabled(unit)
        && MelonDSAndroid::isRenderer2DDebugObjectPriorityEnabled(unit, priority);
}

bool Renderer2DDebugShouldDrawObjectLine(u32 line)
{
    if (line < 64u)
        return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureObjectUpperBand);
    if (line < 128u)
        return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureObjectMiddleBand);
    return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureObjectLowerBand);
}

bool Renderer2DDebugShouldDrawObject(u32 unit, u32 sprnum, const u16* attrib)
{
    if (!MelonDSAndroid::areRenderer2DDebugObjectsEnabled(unit))
        return false;

    if (!MelonDSAndroid::isRenderer2DDebugObjectOrderEnabled(unit, sprnum / 32u))
        return false;

    const u32 priority = (attrib[2] >> 10u) & 0x3u;
    if (!MelonDSAndroid::isRenderer2DDebugObjectPriorityEnabled(unit, priority))
        return false;

    const u32 objectMode = (attrib[0] >> 10u) & 0x3u;
    if (objectMode == 2u)
        return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureWindowObject);

    if ((attrib[0] & 0x0100u) != 0u)
    {
        if (!MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureAffineObject))
            return false;
    }
    else if (!MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureRegularObject))
    {
        return false;
    }

    if ((attrib[0] & 0x1000u) != 0u
        && !MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureMosaicObject))
    {
        return false;
    }

    if (objectMode == 1u
        && !MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureBlendedObject))
    {
        return false;
    }

    if (objectMode == 3u)
        return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(kRenderer2DDebugFeatureBitmapObject);

    const u32 tiledFeature = (attrib[0] & 0x2000u) != 0u
        ? kRenderer2DDebugFeatureTiled8BppObject
        : kRenderer2DDebugFeatureTiled4BppObject;
    return MelonDSAndroid::isRenderer2DDebugObjectFeatureEnabled(tiledFeature);
}
}

SoftRenderer::SoftRenderer(melonDS::GPU& gpu)
    : Renderer2D(), GPU(gpu)
{
    // mosaic table is initialized at compile-time
}

u32 SoftRenderer::ColorComposite(int i, u32 val1, u32 val2) const
{
    u32 coloreffect = 0;
    u32 eva, evb;

    u32 flag1 = val1 >> 24;
    u32 flag2 = val2 >> 24;

    u32 blendCnt = CurUnit->BlendCnt;

    u32 target2;
    if      (flag2 & 0x80) target2 = 0x1000;
    else if (flag2 & 0x40) target2 = 0x0100;
    else                   target2 = flag2 << 8;

    if ((flag1 & 0x80) && (blendCnt & target2))
    {
        // sprite blending

        coloreffect = 1;

        if (flag1 & 0x40)
        {
            eva = flag1 & 0x1F;
            evb = 16 - eva;
        }
        else
        {
            eva = CurUnit->EVA;
            evb = CurUnit->EVB;
        }
    }
    else if ((flag1 & 0x40) && (blendCnt & target2))
    {
        // 3D layer blending

        coloreffect = 4;
    }
    else
    {
        if      (flag1 & 0x80) flag1 = 0x10;
        else if (flag1 & 0x40) flag1 = 0x01;

        if ((blendCnt & flag1) && (WindowMask[i] & 0x20))
        {
            coloreffect = (blendCnt >> 6) & 0x3;

            if (coloreffect == 1)
            {
                if (blendCnt & target2)
                {
                    eva = CurUnit->EVA;
                    evb = CurUnit->EVB;
                }
                else
                    coloreffect = 0;
            }
        }
    }

    switch (coloreffect)
    {
    case 0: return val1;
    case 1: return ColorBlend4(val1, val2, eva, evb);
    case 2: return ColorBrightnessUp(val1, CurUnit->EVY, 0x8);
    case 3: return ColorBrightnessDown(val1, CurUnit->EVY, 0x7);
    case 4: return ColorBlend5(val1, val2);
    }

    return val1;
}

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
    LastDebugCaptureStats = {};
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

bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();
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
    LastDebugCaptureStats.StructuredCopyLines++;
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
            LastDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
        if (sourcePlane1 != 0u)
            LastDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
        const u32 sourceControlAlpha = sourceControl >> 24u;
        const bool structuredSlot = (sourceControlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
        if (structuredSlot)
            LastDebugCaptureStats.StructuredCopySlotPixels++;
        if (structuredSlot && (sourceControlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
            LastDebugCaptureStats.StructuredCopyAbovePixels++;
        if (!structuredSlot && (sourceControlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
            LastDebugCaptureStats.StructuredCopy2DOnlyPixels++;

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
    LastDebugCaptureStats.StructuredCopyLines++;
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
            LastDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
        if (sourcePlane1 != 0u)
            LastDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
        const u32 sourceControlAlpha = sourceControl >> 24u;
        const bool structuredSlot = (sourceControlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
        if (structuredSlot)
            LastDebugCaptureStats.StructuredCopySlotPixels++;
        if (structuredSlot && (sourceControlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
            LastDebugCaptureStats.StructuredCopyAbovePixels++;
        if (!structuredSlot && (sourceControlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
            LastDebugCaptureStats.StructuredCopy2DOnlyPixels++;
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
    LastDebugCaptureStats.StructuredCopySourceBOverlayPixels++;
}

bool SoftRenderer::CurrentUnitTargetsTopScreen() const noexcept
{
    if (CurUnit == nullptr)
        return false;

    const u32* currentFramebuffer = Framebuffer[CurUnit->Num];
    for (int buffer = 0; buffer < 2; buffer++)
    {
        if (currentFramebuffer == GPU.Framebuffer[buffer][0])
            return true;
        if (currentFramebuffer == GPU.Framebuffer[buffer][1])
            return false;
    }

    const bool unitAWritesTop = (GPU.NDS.PowerControl9 & (1u << 15u)) != 0u;
    return CurUnit->Num == 0 ? unitAWritesTop : !unitAWritesTop;
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
        LastDebugCaptureStats.StructuredCopyLines++;
    if (belowPlane != 0u)
        LastDebugCaptureStats.StructuredCopyPlane0UsefulPixels++;
    if (abovePlane != 0u)
        LastDebugCaptureStats.StructuredCopyPlane1UsefulPixels++;
    const u32 controlAlpha = control >> 24u;
    const bool structuredSlot = (controlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
    if (structuredSlot)
        LastDebugCaptureStats.StructuredCopySlotPixels++;
    if (structuredSlot && (controlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u)
        LastDebugCaptureStats.StructuredCopyAbovePixels++;
    if (!structuredSlot && (controlAlpha & kStructuredVulkan2DOnlyFlag) != 0u)
        LastDebugCaptureStats.StructuredCopy2DOnlyPixels++;

    StructuredVulkan2DCapturePlanes[screenIndex] = belowPlane;
    StructuredVulkan2DCapturePlanes[screenBase + kStructuredPixelCount + (screenIndex - screenBase)] = abovePlane;
    StructuredVulkan2DCapturePlanes[screenBase + (kStructuredPixelCount * 2u) + (screenIndex - screenBase)] = control;
    StructuredVulkan2DCaptureLineValid[lineValidIndex] = 1u;
}

void SoftRenderer::DrawScanline(u32 line, Unit* unit)
{
    CurUnit = unit;
    _3DLine = nullptr;
    CurrentLineRegularCaptureUses3d = false;

    int stride = GPU.GPU3D.IsRendererAccelerated() ? (256*3 + 1) : 256;
    u32* dst = &Framebuffer[CurUnit->Num][stride * line];

    int n3dline = line;
    line = GPU.VCount;
    StructuredVulkan2DCurrentLineTargetsTop = CurrentUnitTargetsTopScreen();
    ClearStructuredVulkan2DLine(line);

    if (CurUnit->Num == 0)
    {
        auto bgDirty = GPU.VRAMDirty_ABG.DeriveState(GPU.VRAMMap_ABG, GPU);
        GPU.MakeVRAMFlat_ABGCoherent(bgDirty);
        auto bgExtPalDirty = GPU.VRAMDirty_ABGExtPal.DeriveState(GPU.VRAMMap_ABGExtPal, GPU);
        GPU.MakeVRAMFlat_ABGExtPalCoherent(bgExtPalDirty);
        auto objExtPalDirty = GPU.VRAMDirty_AOBJExtPal.DeriveState(&GPU.VRAMMap_AOBJExtPal, GPU);
        GPU.MakeVRAMFlat_AOBJExtPalCoherent(objExtPalDirty);
    }
    else
    {
        auto bgDirty = GPU.VRAMDirty_BBG.DeriveState(GPU.VRAMMap_BBG, GPU);
        GPU.MakeVRAMFlat_BBGCoherent(bgDirty);
        auto bgExtPalDirty = GPU.VRAMDirty_BBGExtPal.DeriveState(GPU.VRAMMap_BBGExtPal, GPU);
        GPU.MakeVRAMFlat_BBGExtPalCoherent(bgExtPalDirty);
        auto objExtPalDirty = GPU.VRAMDirty_BOBJExtPal.DeriveState(&GPU.VRAMMap_BOBJExtPal, GPU);
        GPU.MakeVRAMFlat_BOBJExtPalCoherent(objExtPalDirty);
    }

    bool forceblank = false;

    // scanlines that end up outside of the GPU drawing range
    // (as a result of writing to VCount) are filled white
    if (line > 192) forceblank = true;

    // GPU B can be completely disabled by POWCNT1
    // oddly that's not the case for GPU A
    if (CurUnit->Num && !CurUnit->Enabled) forceblank = true;

    const bool useStructuredVulkan2D = UseStructuredVulkan2D();

    if (useStructuredVulkan2D && CurUnit->Num == 0 && line == 0)
        CaptureLineUses3d.fill(0);

    if (line == 0 && CurUnit->CaptureCnt & (1 << 31) && !forceblank)
        CurUnit->CaptureLatch = true;

    if (CurUnit->Num == 0)
    {
        if (!GPU.GPU3D.IsRendererAccelerated())
            _3DLine = GPU.GPU3D.GetLine(n3dline);
        else if (!useStructuredVulkan2D && CurUnit->CaptureLatch && (((CurUnit->CaptureCnt >> 29) & 0x3) != 1))
            _3DLine = GPU.GPU3D.GetLine(n3dline);
    }

    if (forceblank)
    {
        for (int i = 0; i < 256; i++)
            dst[i] = 0xFFFFFFFF;

        if (GPU.GPU3D.IsRendererAccelerated())
        {
            dst[256*3] = 0;
        }
        return;
    }

    u32 dispmode = CurUnit->DispCnt >> 16;
    dispmode &= (CurUnit->Num ? 0x1 : 0x3);

    // always render regular graphics
    DrawScanline_BGOBJ(line);
    CurUnit->UpdateMosaicCounters(line);
    if (useStructuredVulkan2D && CurUnit->Num == 0 && CurUnit->CaptureLatch)
        SaveStructuredVulkan2DCaptureSourceLine(line);

    switch (dispmode)
    {
    case 0: // screen off
        {
            for (int i = 0; i < 256; i++)
                dst[i] = 0x003F3F3F;
        }
        break;

    case 1: // regular display
        {
            int i = 0;
            for (; i < (stride & ~1); i+=2)
                *(u64*)&dst[i] = *(u64*)&BGOBJLine[i];
        }
        break;

    case 2: // VRAM display
        {
            u32 vrambank = (CurUnit->DispCnt >> 18) & 0x3;
            if (GPU.VRAMMap_LCDC & (1<<vrambank))
            {
                u16* vram = (u16*)GPU.VRAM[vrambank];
                vram = &vram[line * 256];

                for (int i = 0; i < 256; i++)
                {
                    u16 color = vram[i];
                    u8 r = (color & 0x001F) << 1;
                    u8 g = (color & 0x03E0) >> 4;
                    u8 b = (color & 0x7C00) >> 9;

                    dst[i] = r | (g << 8) | (b << 16);
                }
                if (useStructuredVulkan2D)
                    CopyStructuredVulkan2DCaptureLineToCurrentScreen(line, vrambank);
            }
            else
            {
                for (int i = 0; i < 256; i++)
                {
                    dst[i] = 0;
                }
            }
        }
        break;

    case 3: // FIFO display
        {
            for (int i = 0; i < 256; i++)
            {
                u16 color = CurUnit->DispFIFOBuffer[i];
                u8 r = (color & 0x001F) << 1;
                u8 g = (color & 0x03E0) >> 4;
                u8 b = (color & 0x7C00) >> 9;

                dst[i] = r | (g << 8) | (b << 16);
            }
        }
        break;
    }

    // capture
    if ((CurUnit->Num == 0) && CurUnit->CaptureLatch)
    {
        u32 capwidth, capheight;
        switch ((CurUnit->CaptureCnt >> 20) & 0x3)
        {
        case 0: capwidth = 128; capheight = 128; break;
        case 1: capwidth = 256; capheight = 64;  break;
        case 2: capwidth = 256; capheight = 128; break;
        case 3: capwidth = 256; capheight = 192; break;
        }

        if (line < capheight)
            DoCapture(line, capwidth, static_cast<u32>(n3dline));
    }

    u32 masterBrightness = CurUnit->MasterBrightness;

    if (GPU.GPU3D.IsRendererAccelerated())
    {
        constexpr u32 kMetaFlagRegularCaptureUses3d = 1u << 21u;
        constexpr u32 kMetaFlagVramCaptureUses3d = 1u << 22u;
        constexpr u32 kMetaFlagExactRegularCaptureUses3d = 1u << 19u;
        u32 xpos = GPU.GPU3D.GetRenderXPos();
        u32 rendererMetaFlags = 0;
        const u32 engineACaptureCnt = GPU.CaptureCnt;
        const bool captureConfiguredFullScreen =
            (engineACaptureCnt & (1u << 31u)) != 0u
            && ((engineACaptureCnt >> 20u) & 0x3u) == 3u;

        if (useStructuredVulkan2D && dispmode == 2)
        {
            if (line < CaptureLineUses3d.size() && CaptureLineUses3d[line] != 0)
            {
                rendererMetaFlags |= kMetaFlagVramCaptureUses3d;
            }
        }
        else if (useStructuredVulkan2D && dispmode == 1)
        {
            const bool broadCaptureLineUses3d =
                CurUnit->Num == 1
                && captureConfiguredFullScreen
                && line < CaptureLineUses3d.size()
                && CaptureLineUses3d[line] != 0;
            if (CurrentLineRegularCaptureUses3d || broadCaptureLineUses3d)
            {
                rendererMetaFlags |= kMetaFlagRegularCaptureUses3d;
                if (CurrentLineRegularCaptureUses3d)
                    rendererMetaFlags |= kMetaFlagExactRegularCaptureUses3d;
            }
        }

        dst[256*3] = masterBrightness |
                     (CurUnit->DispCnt & 0x30000) |
                     rendererMetaFlags |
                     (xpos << 24) | ((xpos & 0x100) << 15);
        return;
    }

    // master brightness
    if (dispmode != 0)
    {
        if ((masterBrightness >> 14) == 1)
        {
            // up
            u32 factor = masterBrightness & 0x1F;
            if (factor > 16) factor = 16;

            for (int i = 0; i < 256; i++)
            {
                dst[i] = ColorBrightnessUp(dst[i], factor, 0x0);
            }
        }
        else if ((masterBrightness >> 14) == 2)
        {
            // down
            u32 factor = masterBrightness & 0x1F;
            if (factor > 16) factor = 16;

            for (int i = 0; i < 256; i++)
            {
                dst[i] = ColorBrightnessDown(dst[i], factor, 0xF);
            }
        }
    }

    // convert to 32-bit BGRA
    // note: 32-bit RGBA would be more straightforward, but
    // BGRA seems to be more compatible (Direct2D soft, cairo...)
    for (int i = 0; i < 256; i+=2)
    {
        u64 c = *(u64*)&dst[i];

        u64 r = (c << 18) & 0xFC000000FC0000;
        u64 g = (c << 2) & 0xFC000000FC00;
        u64 b = (c >> 14) & 0xFC000000FC;
        c = r | g | b;

        *(u64*)&dst[i] = c | ((c & 0x00C0C0C000C0C0C0) >> 6) | 0xFF000000FF000000;
    }
}

void SoftRenderer::VBlankEnd(Unit* unitA, Unit* unitB)
{
#ifdef OGLRENDERER_ENABLED
    if (Renderer3D& renderer3d = GPU.GPU3D.GetCurrentRenderer(); renderer3d.UsesStructured2DMetadata())
    {
        const u32 captureCnt = unitA->CaptureCnt;
        const u32 captureMode = (captureCnt >> 29u) & 0x3u;
        const bool captureEnabled = (captureCnt & (1u << 31u)) != 0u;
        if (!renderer3d.UsesStructured2DMetadata())
        {
            if (captureEnabled && captureMode != 1u)
                renderer3d.PrepareCaptureFrame();
            return;
        }

        const bool captureUsesDirect3D = (captureCnt & (1u << 24u)) != 0u;
        const bool sourceAContributes = captureMode == 0u
            || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
        const bool bg0Uses3D = (unitA->DispCnt & 0x0108u) == 0x0108u;
        if (captureEnabled
            && captureMode != 1u
            && (captureUsesDirect3D || (bg0Uses3D && sourceAContributes)))
        {
            renderer3d.SetCaptureScreenSwapHint((GPU.NDS.PowerControl9 & (1u << 15u)) != 0u);
            renderer3d.BeginCaptureFrame();
            renderer3d.PrepareCaptureFrame();
        }
    }
#endif
}

void SoftRenderer::DoCapture(u32 line, u32 width, u32 sourceLine)
{
    u32 captureCnt = CurUnit->CaptureCnt;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool captureUsesDirect3D = (captureCnt & (1u << 24u)) != 0u;
    bool captureLineUses3d = false;
    bool captureLineHasUseful3dAlpha = false;
    bool captureDestinationHasNonZeroPixel = false;
    bool debugCaptureSourceReady = false;
    const bool useStructuredVulkan2D = UseStructuredVulkan2D();
    if (useStructuredVulkan2D && CurUnit->Num == 0 && line < CaptureLineUses3d.size())
        CaptureLineUses3d[line] = 0;
    const bool captureScreenSwap = (GPU.NDS.PowerControl9 & (1u << 15u)) != 0u;
    const bool captureDebugEnabled = MelonDSAndroid::areRendererDebugToolsEnabled();
    const bool captureMetadataEnabled = captureDebugEnabled || useStructuredVulkan2D;
    const bool logCaptureSamples = MelonDSAndroid::areRendererDebugBgObjLogsEnabled();
    if (line == 0)
    {
        HasLastDebugCapture3dSource = false;
        std::memset(LastDebugCapture3dSource, 0, sizeof(LastDebugCapture3dSource));
        LastDebugCaptureStats = {};
        LastDebugCaptureStats.CaptureWidth = width;
        LastDebugCaptureStats.CaptureMode = captureMode;
        LastDebugCaptureStats.CaptureBit24 = (captureCnt & (1u << 24u)) != 0u ? 1u : 0u;
    }
    LastDebugCaptureStats.CaptureLines++;
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
            srcA = _3DLine;
        }
        else
        {
            srcA = BGOBJLine;
            if (GPU.GPU3D.IsRendererAccelerated())
            {
                for (int i = 0; i < 256; i++)
                {
                    u32 val1 = BGOBJLine[i];
                    u32 val2 = BGOBJLine[256+i];
                    u32 val3 = BGOBJLine[512+i];

                    u32 compmode = (val3 >> 24) & 0xF;

                    if (compmode == 4)
                    {
                        u32 _3dval = _3DLine[i];
                        if ((_3dval >> 24) > 0)
                            val1 = ColorBlend5(_3dval, val1);
                        else
                            val1 = val2;
                    }
                    else if (compmode == 1)
                    {
                        u32 _3dval = _3DLine[i];
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
                        u32 _3dval = _3DLine[i];
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

                    BGOBJLine[i] = val1;
                }
            }
        }

        u16* srcB = NULL;
        u32 srcBaddr = line * 256;

        if (captureCnt & (1<<25))
        {
            srcB = &CurUnit->DispFIFOBuffer[0];
            srcBaddr = 0;
        }
        else
        {
            u32 srcvram = (CurUnit->DispCnt >> 18) & 0x3;
            if (GPU.VRAMMap_LCDC & (1<<srcvram))
                srcB = (u16*)GPU.VRAM[srcvram];

            if (((CurUnit->DispCnt >> 16) & 0x3) != 2)
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
        srcB = &CurUnit->DispFIFOBuffer[0];
        srcBaddr = 0;
    }
    else
    {
        u32 srcvram = (CurUnit->DispCnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcB = (u16*)GPU.VRAM[srcvram];
            structuredSourceBVram = srcvram;
            structuredSourceBFromVram = true;
        }

        if (((CurUnit->DispCnt >> 16) & 0x3) != 2)
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
            LastDebugCaptureStats.Direct3DLines++;
        if (GPU.GPU3D.IsRendererAccelerated())
            GPU.GPU3D.GetCurrentRenderer().SetCaptureScreenSwapHint(captureScreenSwap);
        if (GPU.GPU3D.IsRendererAccelerated())
            _3DLine = GPU.GPU3D.GetLine(static_cast<int>(sourceLine));
        srcA = _3DLine;
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
        srcA = BGOBJLine;
        if (GPU.GPU3D.IsRendererAccelerated())
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
                    const u32 compmode = (BGOBJLine[512 + i] >> 24) & 0xF;
                    if (captureDebugEnabled && compmode < 8u)
                        LastDebugCaptureStats.CompModeCounts[compmode]++;
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
                    LastDebugCaptureStats.SourceACompositeLines++;
                GPU.GPU3D.GetCurrentRenderer().SetCaptureScreenSwapHint(captureScreenSwap);
                _3DLine = GPU.GPU3D.GetLine(static_cast<int>(sourceLine));
                if (_3DLine)
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
                        const u32 sourceClass = StructuredVulkan2DSourceClass(BGOBJLine[i]);
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
                            if ((_3DLine[i] >> 24) != 0u)
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
                        const u32 originalVal1 = BGOBJLine[i];
                        const u32 originalVal2 = BGOBJLine[256+i];
                        const u32 originalVal3 = BGOBJLine[512+i];
                        u32 val1 = originalVal1;
                        u32 val2 = originalVal2;
                        u32 val3 = originalVal3;

                        u32 compmode = (val3 >> 24) & 0xF;
                        const u32 _3dval = _3DLine[i];
                        if (captureDebugEnabled && (_3dval >> 24) > 0)
                        {
                            LastDebugCaptureStats.Opaque3DSourcePixels++;
                            if ((val1 & 0xFF000000u) == 0x20000000u)
                                LastDebugCaptureStats.Opaque3DBackdropPixels++;
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

                        BGOBJLine[i] = val1;
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

    if (useStructuredVulkan2D && CurUnit->Num == 0 && line < CaptureLineUses3d.size())
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
                if (GPU.GPU3D.IsRendererAccelerated())
                {
                    if (packed != 0)
                    {
                        LastDebugCaptureStats.SourceAOutputUsefulPixels++;
                        if ((packed & 0x7FFFu) != 0u)
                            LastDebugCaptureStats.SourceAOutputVisiblePixels++;
                        else
                            LastDebugCaptureStats.SourceAOutputOpaqueBlackPixels++;
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
        LastDebugCaptureStats.CaptureLineUses3dLines++;

    if (captureDebugEnabled)
    {
        if (captureLineHasUseful3dAlpha)
            LastDebugCaptureStats.CaptureLineUsefulAlphaLines++;
        if (!captureDestinationHasNonZeroPixel)
            LastDebugCaptureStats.CaptureDestinationBlankLines++;
    }
}

#define DoDrawBG(type, line, num) \
    do \
    { \
        if (!Renderer2DDebugShouldDraw##type##Bg(CurUnit->Num, num, bgCnt[num])) \
            break; \
        if ((bgCnt[num] & 0x0040) && (CurUnit->BGMosaicSize[0] > 0)) \
        { \
            if (GPU.GPU3D.IsRendererAccelerated()) DrawBG_##type<true, DrawPixel_Accel>(line, num); \
            else DrawBG_##type<true, DrawPixel_Normal>(line, num); \
        } \
        else \
        { \
            if (GPU.GPU3D.IsRendererAccelerated()) DrawBG_##type<false, DrawPixel_Accel>(line, num); \
            else DrawBG_##type<false, DrawPixel_Normal>(line, num); \
        } \
    } while (false)

#define DoDrawBG_Large(line) \
    do \
    { \
        if (!Renderer2DDebugShouldDrawLargeBg(CurUnit->Num, bgCnt[2])) \
            break; \
        if ((bgCnt[2] & 0x0040) && (CurUnit->BGMosaicSize[0] > 0)) \
        { \
            if (GPU.GPU3D.IsRendererAccelerated()) DrawBG_Large<true, DrawPixel_Accel>(line); \
            else DrawBG_Large<true, DrawPixel_Normal>(line); \
        } \
        else \
        { \
            if (GPU.GPU3D.IsRendererAccelerated()) DrawBG_Large<false, DrawPixel_Accel>(line); \
            else DrawBG_Large<false, DrawPixel_Normal>(line); \
        } \
    } while (false)

#define DoInterleaveSprites(prio) \
    if (Renderer2DDebugShouldInterleaveObjects(CurUnit->Num, ((prio) >> 16) & 0x3u)) { if (GPU.GPU3D.IsRendererAccelerated()) InterleaveSprites<DrawPixel_Accel>(prio); else InterleaveSprites<DrawPixel_Normal>(prio); }

template<u32 bgmode>
void SoftRenderer::DrawScanlineBGMode(u32 line)
{
    u32 dispCnt = CurUnit->DispCnt;
    u16* bgCnt = CurUnit->BGCnt;
    for (int i = 3; i >= 0; i--)
    {
        if ((bgCnt[3] & 0x3) == i)
        {
            if (dispCnt & 0x0800)
            {
                if (bgmode >= 3)
                    DoDrawBG(Extended, line, 3);
                else if (bgmode >= 1)
                    DoDrawBG(Affine, line, 3);
                else
                    DoDrawBG(Text, line, 3);
            }
        }
        if ((bgCnt[2] & 0x3) == i)
        {
            if (dispCnt & 0x0400)
            {
                if (bgmode == 5)
                    DoDrawBG(Extended, line, 2);
                else if (bgmode == 4 || bgmode == 2)
                    DoDrawBG(Affine, line, 2);
                else
                    DoDrawBG(Text, line, 2);
            }
        }
        if ((bgCnt[1] & 0x3) == i)
        {
            if (dispCnt & 0x0200)
            {
                DoDrawBG(Text, line, 1);
            }
        }
        if ((bgCnt[0] & 0x3) == i)
        {
            if (dispCnt & 0x0100)
            {
                if (!CurUnit->Num && (dispCnt & 0x8))
                {
                    if (Renderer2DDebugShouldDraw3DBg(CurUnit->Num, bgCnt[0]))
                        DrawBG_3D();
                }
                else
                {
                    DoDrawBG(Text, line, 0);
                }
            }
        }
        if ((dispCnt & 0x1000) && NumSprites[CurUnit->Num])
        {
            DoInterleaveSprites(0x40000 | (i<<16));
        }

    }
}

void SoftRenderer::DrawScanlineBGMode6(u32 line)
{
    u32 dispCnt = CurUnit->DispCnt;
    u16* bgCnt = CurUnit->BGCnt;
    for (int i = 3; i >= 0; i--)
    {
        if ((bgCnt[2] & 0x3) == i)
        {
            if (dispCnt & 0x0400)
            {
                DoDrawBG_Large(line);
            }
        }
        if ((bgCnt[0] & 0x3) == i)
        {
            if (dispCnt & 0x0100)
            {
                if ((!CurUnit->Num) && (dispCnt & 0x8))
                {
                    if (Renderer2DDebugShouldDraw3DBg(CurUnit->Num, bgCnt[0]))
                        DrawBG_3D();
                }
            }
        }
        if ((dispCnt & 0x1000) && NumSprites[CurUnit->Num])
        {
            DoInterleaveSprites(0x40000 | (i<<16))
        }
    }
}

void SoftRenderer::DrawScanlineBGMode7(u32 line)
{
    u32 dispCnt = CurUnit->DispCnt;
    u16* bgCnt = CurUnit->BGCnt;
    // mode 7 only has text-mode BG0 and BG1

    for (int i = 3; i >= 0; i--)
    {
        if ((bgCnt[1] & 0x3) == i)
        {
            if (dispCnt & 0x0200)
            {
                DoDrawBG(Text, line, 1);
            }
        }
        if ((bgCnt[0] & 0x3) == i)
        {
            if (dispCnt & 0x0100)
            {
                if (!CurUnit->Num && (dispCnt & 0x8))
                {
                    if (Renderer2DDebugShouldDraw3DBg(CurUnit->Num, bgCnt[0]))
                        DrawBG_3D();
                }
                else
                {
                    DoDrawBG(Text, line, 0);
                }
            }
        }
        if ((dispCnt & 0x1000) && NumSprites[CurUnit->Num])
        {
            DoInterleaveSprites(0x40000 | (i<<16))
        }
    }
}

void SoftRenderer::DrawScanline_BGOBJ(u32 line)
{
    if (!UseStructuredVulkan2D() && !MelonDSAndroid::areRendererDebugToolsEnabled())
    {
        if (CurUnit->DispCnt & (1<<7))
        {
            for (int i = 0; i < 256; i++)
                BGOBJLine[i] = 0xFF3F3F3F;

            return;
        }

        u64 backdrop;
        if (CurUnit->Num) backdrop = *(u16*)&GPU.Palette[0x400];
        else     backdrop = *(u16*)&GPU.Palette[0];

        {
            u8 r = (backdrop & 0x001F) << 1;
            u8 g = (backdrop & 0x03E0) >> 4;
            u8 b = (backdrop & 0x7C00) >> 9;

            backdrop = r | (g << 8) | (b << 16) | 0x20000000;
            backdrop |= (backdrop << 32);

            for (int i = 0; i < 256; i+=2)
                *(u64*)&BGOBJLine[i] = backdrop;
        }

        if (CurUnit->DispCnt & 0xE000)
            CurUnit->CalculateWindowMask(line, WindowMask, OBJWindow[CurUnit->Num]);
        else
            memset(WindowMask, 0xFF, 256);

        ApplySpriteMosaicX();
        CurBGXMosaicTable = MosaicTable[CurUnit->BGMosaicSize[0]].data();

        switch (CurUnit->DispCnt & 0x7)
        {
        case 0: DrawScanlineBGMode<0>(line); break;
        case 1: DrawScanlineBGMode<1>(line); break;
        case 2: DrawScanlineBGMode<2>(line); break;
        case 3: DrawScanlineBGMode<3>(line); break;
        case 4: DrawScanlineBGMode<4>(line); break;
        case 5: DrawScanlineBGMode<5>(line); break;
        case 6: DrawScanlineBGMode6(line); break;
        case 7: DrawScanlineBGMode7(line); break;
        }

        if (!GPU.GPU3D.IsRendererAccelerated())
        {
            for (int i = 0; i < 256; i++)
            {
                u32 val1 = BGOBJLine[i];
                u32 val2 = BGOBJLine[256+i];

                BGOBJLine[i] = ColorComposite(i, val1, val2);
            }
        }
        else
        {
            if (CurUnit->Num == 0)
            {
                for (int i = 0; i < 256; i++)
                {
                    u32 val1 = BGOBJLine[i];
                    u32 val2 = BGOBJLine[256+i];
                    u32 val3 = BGOBJLine[512+i];

                    u32 flag1 = val1 >> 24;
                    u32 flag2 = val2 >> 24;

                    u32 bldcnteffect = (CurUnit->BlendCnt >> 6) & 0x3;

                    u32 target1;
                    if      (flag1 & 0x80) target1 = 0x0010;
                    else if (flag1 & 0x40) target1 = 0x0001;
                    else                   target1 = flag1;

                    u32 target2;
                    if      (flag2 & 0x80) target2 = 0x1000;
                    else if (flag2 & 0x40) target2 = 0x0100;
                    else                   target2 = flag2 << 8;

                    if (((flag1 & 0xC0) == 0x40) && (CurUnit->BlendCnt & target2))
                    {
                        BGOBJLine[i]     = val2;
                        BGOBJLine[256+i] = ColorComposite(i, val2, val3);
                        BGOBJLine[512+i] = 0x04000000;
                    }
                    else if ((flag1 & 0xC0) == 0x40)
                    {
                        if (bldcnteffect == 1)             bldcnteffect = 0;
                        if (!(CurUnit->BlendCnt & 0x0001)) bldcnteffect = 0;
                        if (!(WindowMask[i] & 0x20))       bldcnteffect = 0;

                        BGOBJLine[i]     = val2;
                        BGOBJLine[256+i] = ColorComposite(i, val2, val3);
                        BGOBJLine[512+i] = (bldcnteffect << 24) | (CurUnit->EVY << 8);
                    }
                    else if (((flag2 & 0xC0) == 0x40) && ((CurUnit->BlendCnt & 0x01C0) == 0x0140))
                    {
                        u32 eva, evb;
                        if ((flag1 & 0xC0) == 0xC0)
                        {
                            eva = flag1 & 0x1F;
                            evb = 16 - eva;
                        }
                        else if (((CurUnit->BlendCnt & target1) && (WindowMask[i] & 0x20)) ||
                                ((flag1 & 0xC0) == 0x80))
                        {
                            eva = CurUnit->EVA;
                            evb = CurUnit->EVB;
                        }
                        else
                            bldcnteffect = 7;

                        BGOBJLine[i]     = val1;
                        BGOBJLine[256+i] = ColorComposite(i, val1, val3);
                        BGOBJLine[512+i] = (bldcnteffect << 24) | (CurUnit->EVB << 16) | (CurUnit->EVA << 8);
                    }
                    else
                    {
                        BGOBJLine[i]     = ColorComposite(i, val1, val2);
                        BGOBJLine[256+i] = 0;
                        BGOBJLine[512+i] = 0x07000000;
                    }
                }
            }
            else
            {
                for (int i = 0; i < 256; i++)
                {
                    u32 val1 = BGOBJLine[i];
                    u32 val2 = BGOBJLine[256+i];

                    BGOBJLine[i]     = ColorComposite(i, val1, val2);
                    BGOBJLine[256+i] = 0;
                    BGOBJLine[512+i] = 0x07000000;
                }
            }
        }

        if (CurUnit->BGMosaicY >= CurUnit->BGMosaicYMax)
        {
            CurUnit->BGMosaicY = 0;
            CurUnit->BGMosaicYMax = CurUnit->BGMosaicSize[1];
        }
        else
            CurUnit->BGMosaicY++;

        return;
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
        {"hud_gem_l",   8u,  8u},
        {"hud_gem_r",  16u, 10u},
        {"hud_text",   24u,  8u},
        {"hud_can",    50u,  8u},
        {"hud_arrow",  35u, 18u},
        {"hud_lawn",    5u, 65u},
    };
    const bool logCaptureSamples = MelonDSAndroid::areRendererDebugBgObjLogsEnabled();
    const bool useStructuredVulkan2D = UseStructuredVulkan2D();

    auto logHudStageAfterBGMode =
        [&]() {
            if (!logCaptureSamples)
                return;
            if (CurUnit->Num != 0)
                return;
            for (const CaptureSamplePoint& sample : kCaptureSamplePoints)
            {
                if (sample.y != line)
                    continue;
                const u32 i = sample.x;
                if (i >= 256)
                    continue;
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "RendererDebug[BGOBJ]: stage=after_bgmode unit=%u label=%s line=%u x=%u v0=%08X v1=%08X v2=%08X",
                    CurUnit->Num,
                    sample.label,
                    line,
                    i,
                    BGOBJLine[i],
                    BGOBJLine[256+i],
                    BGOBJLine[512+i]
                );
            }
        };

    // forced blank disables BG/OBJ compositing
    if (CurUnit->DispCnt & (1<<7))
    {
        for (int i = 0; i < 256; i++)
            BGOBJLine[i] = 0xFF3F3F3F;

        return;
    }

    u64 backdrop;
    if (CurUnit->Num) backdrop = *(u16*)&GPU.Palette[0x400];
    else     backdrop = *(u16*)&GPU.Palette[0];

    {
        u8 r = (backdrop & 0x001F) << 1;
        u8 g = (backdrop & 0x03E0) >> 4;
        u8 b = (backdrop & 0x7C00) >> 9;

        backdrop = r | (g << 8) | (b << 16) | 0x20000000;
        backdrop |= (backdrop << 32);

        for (int i = 0; i < 256; i+=2)
        {
            *(u64*)&BGOBJLine[i] = backdrop;
            if (useStructuredVulkan2D)
            {
                *(u64*)&BGOBJLine[256 + i] = 0;
                *(u64*)&BGOBJLine[512 + i] = 0;
            }
        }
    }

    if (CurUnit->DispCnt & 0xE000)
        CurUnit->CalculateWindowMask(line, WindowMask, OBJWindow[CurUnit->Num]);
    else
        memset(WindowMask, 0xFF, 256);

    ApplySpriteMosaicX();
    CurBGXMosaicTable = MosaicTable[CurUnit->BGMosaicSize[0]].data();

    u32 bgMode = CurUnit->DispCnt & 0x7;
    const int forcedBgMode = MelonDSAndroid::getRenderer2DDebugForcedMode(CurUnit->Num);
    if (forcedBgMode >= 0 && forcedBgMode <= 6)
        bgMode = static_cast<u32>(forcedBgMode);

    switch (bgMode)
    {
    case 0: DrawScanlineBGMode<0>(line); break;
    case 1: DrawScanlineBGMode<1>(line); break;
    case 2: DrawScanlineBGMode<2>(line); break;
    case 3: DrawScanlineBGMode<3>(line); break;
    case 4: DrawScanlineBGMode<4>(line); break;
    case 5: DrawScanlineBGMode<5>(line); break;
    case 6: DrawScanlineBGMode6(line); break;
    case 7: DrawScanlineBGMode7(line); break;
    }

    logHudStageAfterBGMode();

    // color special effects
    // can likely be optimized

    if (!GPU.GPU3D.IsRendererAccelerated())
    {
        for (int i = 0; i < 256; i++)
        {
            u32 val1 = BGOBJLine[i];
            u32 val2 = BGOBJLine[256+i];

            BGOBJLine[i] = ColorComposite(i, val1, val2);
        }
    }
    else
    {
        const u32 displayMode =
            (CurUnit->DispCnt >> 16u) & (CurUnit->Num ? 0x1u : 0x3u);
        const bool captureBacked3DLine =
            useStructuredVulkan2D
            &&
            CurUnit->Num == 1
            && displayMode == 1u
            && line < CaptureLineUses3d.size()
            && CaptureLineUses3d[line] != 0u;
        u32 captureBacked3DSourceClass = 0u;
        if (captureBacked3DLine)
        {
            LastDebugCaptureStats.CaptureBacked3DLines++;
            u32 sourceCounts[17] {};
            bool lineHasExplicit3DSlot = false;
            for (int i = 0; i < 256; i++)
            {
                lineHasExplicit3DSlot =
                    lineHasExplicit3DSlot
                    || StructuredVulkan2DHas3DSlot(BGOBJLine[i])
                    || StructuredVulkan2DHas3DSlot(BGOBJLine[256+i])
                    || StructuredVulkan2DHas3DSlot(BGOBJLine[512+i]);
                const u32 sourceClass = StructuredVulkan2DSourceClass(BGOBJLine[i]);
                if (sourceClass <= 16u)
                    sourceCounts[sourceClass]++;
            }

            if (!lineHasExplicit3DSlot)
            {
                constexpr u32 sourceClasses[] = {1u, 2u, 4u, 8u};
                u32 bestSourceClass = 0u;
                u32 bestSourceCount = 0u;
                for (u32 sourceClass : sourceClasses)
                {
                    if (sourceCounts[sourceClass] > bestSourceCount)
                    {
                        bestSourceCount = sourceCounts[sourceClass];
                        bestSourceClass = sourceClass;
                    }
                }

                if (bestSourceCount >= 128u)
                    captureBacked3DSourceClass = bestSourceClass;
                else
                    LastDebugCaptureStats.CaptureBacked3DNoBestClassLines++;
            }
            else
            {
                LastDebugCaptureStats.CaptureBacked3DExplicitSlotLines++;
            }

            if (captureBacked3DSourceClass < (sizeof(LastDebugCaptureStats.CaptureBacked3DBestClassCounts) / sizeof(LastDebugCaptureStats.CaptureBacked3DBestClassCounts[0])))
                LastDebugCaptureStats.CaptureBacked3DBestClassCounts[captureBacked3DSourceClass]++;
        }

        if (CurUnit->Num == 0)
        {
            for (int i = 0; i < 256; i++)
            {
                const u32 originalVal1 = BGOBJLine[i];
                const u32 originalVal2 = BGOBJLine[256+i];
                const u32 originalVal3 = BGOBJLine[512+i];

                u32 val1 = originalVal1;
                u32 val2 = originalVal2;
                u32 val3 = originalVal3;

                u32 flag1 = val1 >> 24;
                u32 flag2 = val2 >> 24;

                u32 bldcnteffect = (CurUnit->BlendCnt >> 6) & 0x3;

                u32 target1;
                if      (flag1 & 0x80) target1 = 0x0010;
                else if (flag1 & 0x40) target1 = 0x0001;
                else                   target1 = flag1;

                u32 target2;
                if      (flag2 & 0x80) target2 = 0x1000;
                else if (flag2 & 0x40) target2 = 0x0100;
                else                   target2 = flag2 << 8;

                if (((flag1 & 0xC0) == 0x40) && (CurUnit->BlendCnt & target2))
                {
                    // 3D on top, blending

                    BGOBJLine[i]     = val2;
                    BGOBJLine[256+i] = ColorComposite(i, val2, val3);
                    BGOBJLine[512+i] = 0x04000000;
                }
                else if ((flag1 & 0xC0) == 0x40)
                {
                    // 3D on top, normal/fade

                    if (bldcnteffect == 1)             bldcnteffect = 0;
                    if (!(CurUnit->BlendCnt & 0x0001)) bldcnteffect = 0;
                    if (!(WindowMask[i] & 0x20))       bldcnteffect = 0;

                    BGOBJLine[i]     = val2;
                    BGOBJLine[256+i] = ColorComposite(i, val2, val3);
                    BGOBJLine[512+i] = (bldcnteffect << 24) | (CurUnit->EVY << 8);
                }
                else if (((flag2 & 0xC0) == 0x40) && ((CurUnit->BlendCnt & 0x01C0) == 0x0140))
                {
                    // 3D on bottom, blending

                    u32 eva, evb;
                    if ((flag1 & 0xC0) == 0xC0)
                    {
                        eva = flag1 & 0x1F;
                        evb = 16 - eva;
                    }
                    else if (((CurUnit->BlendCnt & target1) && (WindowMask[i] & 0x20)) ||
                            ((flag1 & 0xC0) == 0x80))
                    {
                        eva = CurUnit->EVA;
                        evb = CurUnit->EVB;
                    }
                    else
                        bldcnteffect = 7;

                    BGOBJLine[i]     = val1;
                    BGOBJLine[256+i] = ColorComposite(i, val1, val3);
                    BGOBJLine[512+i] = (bldcnteffect << 24) | (CurUnit->EVB << 16) | (CurUnit->EVA << 8);
                }
                else
                {
                    // no potential 3D pixel involved

                    const u32 flag3 = originalVal3 >> 24;
                    const bool overlayOver3d = useStructuredVulkan2D && (flag3 & 0x40u) != 0;

                    BGOBJLine[i]     = ColorComposite(i, val1, val2);
                    BGOBJLine[256+i] = 0;
                    BGOBJLine[512+i] = overlayOver3d ? 0x87000000u : 0x07000000u;
                }

                StoreStructuredVulkan2DPixel(
                    line,
                    static_cast<u32>(i),
                    originalVal1,
                    originalVal2,
                    originalVal3,
                    BGOBJLine[i],
                    BGOBJLine[256+i],
                    BGOBJLine[512+i],
                    captureBacked3DSourceClass);

                if (logCaptureSamples)
                {
                    for (const CaptureSamplePoint& sample : kCaptureSamplePoints)
                    {
                        if (sample.y != line || sample.x != static_cast<u32>(i))
                            continue;

                        Platform::Log(
                            Platform::LogLevel::Warn,
                            "RendererDebug[BGOBJ]: unit=%u label=%s line=%u x=%u pre0=%08X pre1=%08X pre2=%08X out0=%08X out1=%08X out2=%08X",
                            CurUnit->Num,
                            sample.label,
                            line,
                            static_cast<u32>(i),
                            originalVal1,
                            originalVal2,
                            originalVal3,
                            BGOBJLine[i],
                            BGOBJLine[256+i],
                            BGOBJLine[512+i]
                        );
                        break;
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < 256; i++)
            {
                const u32 originalVal1 = BGOBJLine[i];
                const u32 originalVal2 = BGOBJLine[256+i];
                const u32 originalVal3 = BGOBJLine[512+i];

                u32 val1 = originalVal1;
                u32 val2 = originalVal2;

                const u32 flag3 = originalVal3 >> 24;
                const bool overlayOver3d = useStructuredVulkan2D && (flag3 & 0x40u) != 0;

                BGOBJLine[i]     = ColorComposite(i, val1, val2);
                BGOBJLine[256+i] = 0;
                BGOBJLine[512+i] = overlayOver3d ? 0x87000000u : 0x07000000u;

                StoreStructuredVulkan2DPixel(
                    line,
                    static_cast<u32>(i),
                    originalVal1,
                    originalVal2,
                    originalVal3,
                    BGOBJLine[i],
                    BGOBJLine[256+i],
                    BGOBJLine[512+i],
                    captureBacked3DSourceClass);

                if (logCaptureSamples)
                {
                    for (const CaptureSamplePoint& sample : kCaptureSamplePoints)
                    {
                        if (sample.y != line || sample.x != static_cast<u32>(i))
                            continue;

                        Platform::Log(
                            Platform::LogLevel::Warn,
                            "RendererDebug[BGOBJ]: unit=%u label=%s line=%u x=%u pre0=%08X pre1=%08X pre2=%08X out0=%08X out1=%08X out2=%08X",
                            CurUnit->Num,
                            sample.label,
                            line,
                            static_cast<u32>(i),
                            originalVal1,
                            originalVal2,
                            originalVal3,
                            BGOBJLine[i],
                            BGOBJLine[256+i],
                            BGOBJLine[512+i]
                        );
                        break;
                    }
                }
            }
        }
    }

    if (CurUnit->BGMosaicY >= CurUnit->BGMosaicYMax)
    {
        CurUnit->BGMosaicY = 0;
        CurUnit->BGMosaicYMax = CurUnit->BGMosaicSize[1];
    }
    else
        CurUnit->BGMosaicY++;

    /*if (OBJMosaicY >= OBJMosaicYMax)
    {
        OBJMosaicY = 0;
        OBJMosaicYMax = OBJMosaicSize[1];
    }
    else
        OBJMosaicY++;*/
}


void SoftRenderer::DrawPixel_Normal(u32* dst, u16 color, u32 flag)
{
    u8 r = (color & 0x001F) << 1;
    u8 g = (color & 0x03E0) >> 4;
    u8 b = (color & 0x7C00) >> 9;
    //g |= ((color & 0x8000) >> 15);

    *(dst+256) = *dst;
    *dst = r | (g << 8) | (b << 16) | flag;
}

void SoftRenderer::DrawPixel_Accel(u32* dst, u16 color, u32 flag)
{
    u8 r = (color & 0x001F) << 1;
    u8 g = (color & 0x03E0) >> 4;
    u8 b = (color & 0x7C00) >> 9;

    *(dst+512) = *(dst+256);
    *(dst+256) = *dst;
    *dst = r | (g << 8) | (b << 16) | flag;
}

void SoftRenderer::PushRawPixel_Accel(u32* dst, u32 value)
{
    *(dst+512) = *(dst+256);
    *(dst+256) = *dst;
    *dst = value;
}

bool SoftRenderer::TryDrawStructuredVulkan2DCapturePixel(u32* dst, u32 flatByteAddress)
{
    if (!UseStructuredVulkan2D())
        return false;

    const u32 displayMode =
        (CurUnit->DispCnt >> 16u) & (CurUnit->Num ? 0x1u : 0x3u);
    if (displayMode != 1u)
        return false;

    const u32 maskedByteAddress = flatByteAddress & (CurUnit->Num ? 0x1FFFFu : 0x7FFFFu);
    const u32 mapMask = CurUnit->Num
        ? GPU.VRAMMap_BBG[(maskedByteAddress >> 14u) & 0x7u]
        : GPU.VRAMMap_ABG[(maskedByteAddress >> 14u) & 0x1Fu];

    for (u32 vramBank = 0; vramBank < 4u; vramBank++)
    {
        if ((mapMask & (1u << vramBank)) == 0u)
            continue;

        const u32 captureAddress = (maskedByteAddress & 0x1FFFFu) >> 1u;
        if (captureAddress >= kStructuredPixelCount)
            continue;

        const size_t lineValidIndex =
            (static_cast<size_t>(vramBank) * kStructuredScreenHeight)
                + (captureAddress / kStructuredScreenWidth);
        if (StructuredVulkan2DCaptureLineValid[lineValidIndex] == 0u)
            continue;

        const size_t captureBase =
            static_cast<size_t>(vramBank) * kStructuredPlaneCount * kStructuredPixelCount;
        const size_t captureIndex = static_cast<size_t>(captureAddress);
        const u32 belowPlane = StructuredVulkan2DCapturePlanes[captureBase + captureIndex];
        const u32 abovePlane =
            StructuredVulkan2DCapturePlanes[captureBase + kStructuredPixelCount + captureIndex];
        const u32 control =
            StructuredVulkan2DCapturePlanes[captureBase + (kStructuredPixelCount * 2u) + captureIndex];
        const u32 controlAlpha = control >> 24u;
        if (controlAlpha == 0u)
            continue;

        const bool structuredSlot = (controlAlpha & kStructuredVulkan2DSlot3DFlag) != 0u;
        if (structuredSlot)
        {
            if (belowPlane != 0u)
                PushRawPixel_Accel(dst, belowPlane);
            PushRawPixel_Accel(dst, 0x40000000u);
            if ((controlAlpha & kStructuredVulkan2DAbove3DFlag) != 0u && abovePlane != 0u)
                PushRawPixel_Accel(dst, abovePlane);
            CurrentLineRegularCaptureUses3d = true;
            return true;
        }

        if ((controlAlpha & kStructuredVulkan2DOnlyFlag) != 0u && belowPlane != 0u)
        {
            PushRawPixel_Accel(dst, belowPlane);
            return true;
        }
    }

    return false;
}

void SoftRenderer::DrawBG_3D()
{
    if (!Renderer2DDebugShouldDraw3DBg(CurUnit->Num, CurUnit->BGCnt[0]))
        return;

    int i = 0;

    if (GPU.GPU3D.IsRendererAccelerated())
    {
        for (i = 0; i < 256; i++)
        {
            if (!(WindowMask[i] & 0x01)) continue;

            BGOBJLine[i+512] = BGOBJLine[i+256];
            BGOBJLine[i+256] = BGOBJLine[i];
            BGOBJLine[i] = 0x40000000; // 3D-layer placeholder
        }
    }
    else
    {
        for (i = 0; i < 256; i++)
        {
            u32 c = _3DLine[i];

            if ((c >> 24) == 0) continue;
            if (!(WindowMask[i] & 0x01)) continue;

            BGOBJLine[i+256] = BGOBJLine[i];
            BGOBJLine[i] = c | 0x40000000;
        }
    }
}

template<bool mosaic, SoftRenderer::DrawPixel drawPixel>
void SoftRenderer::DrawBG_Text(u32 line, u32 bgnum)
{
    // workaround for backgrounds missing on aarch64 with lto build
    asm volatile ("" : : : "memory");

    u16 bgcnt = CurUnit->BGCnt[bgnum];

    u32 tilesetaddr, tilemapaddr;
    u16* pal;
    u32 extpal, extpalslot;

    u16 xoff = CurUnit->BGXPos[bgnum];
    u16 yoff = CurUnit->BGYPos[bgnum] + line;

    if (bgcnt & 0x0040)
    {
        // vertical mosaic
        yoff -= CurUnit->BGMosaicY;
    }

    u32 widexmask = (bgcnt & 0x4000) ? 0x100 : 0;

    extpal = (CurUnit->DispCnt & 0x40000000);
    if (extpal) extpalslot = ((bgnum<2) && (bgcnt&0x2000)) ? (2+bgnum) : bgnum;

    u8* bgvram;
    u32 bgvrammask;
    CurUnit->GetBGVRAM(bgvram, bgvrammask);
    if (CurUnit->Num)
    {
        tilesetaddr = ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((bgcnt & 0x1F00) << 3);

        pal = (u16*)&GPU.Palette[0x400];
    }
    else
    {
        tilesetaddr = ((CurUnit->DispCnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((CurUnit->DispCnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);

        pal = (u16*)&GPU.Palette[0];
    }

    // adjust Y position in tilemap
    if (bgcnt & 0x8000)
    {
        tilemapaddr += ((yoff & 0x1F8) << 3);
        if (bgcnt & 0x4000)
            tilemapaddr += ((yoff & 0x100) << 3);
    }
    else
        tilemapaddr += ((yoff & 0xF8) << 3);

    u16 curtile;
    u16* curpal;
    u32 pixelsaddr;
    u8 color;
    u32 lastxpos;

    if (bgcnt & 0x0080)
    {
        // 256-color

        // preload shit as needed
        if ((xoff & 0x7) || mosaic)
        {
            curtile = *(u16*)&bgvram[(tilemapaddr + ((xoff & 0xF8) >> 2) + ((xoff & widexmask) << 3)) & bgvrammask];

            if (extpal) curpal = CurUnit->GetBGExtPal(extpalslot, curtile>>12);
            else        curpal = pal;

            pixelsaddr = tilesetaddr + ((curtile & 0x03FF) << 6)
                                     + (((curtile & 0x0800) ? (7-(yoff&0x7)) : (yoff&0x7)) << 3);
        }

        if (mosaic) lastxpos = xoff;

        for (int i = 0; i < 256; i++)
        {
            u32 xpos;
            if (mosaic) xpos = xoff - CurBGXMosaicTable[i];
            else        xpos = xoff;

            if ((!mosaic && (!(xpos & 0x7))) ||
                (mosaic && ((xpos >> 3) != (lastxpos >> 3))))
            {
                // load a new tile
                curtile = *(u16*)&bgvram[(tilemapaddr + ((xpos & 0xF8) >> 2) + ((xpos & widexmask) << 3)) & bgvrammask];

                if (extpal) curpal = CurUnit->GetBGExtPal(extpalslot, curtile>>12);
                else        curpal = pal;

                pixelsaddr = tilesetaddr + ((curtile & 0x03FF) << 6)
                                         + (((curtile & 0x0800) ? (7-(yoff&0x7)) : (yoff&0x7)) << 3);

                if (mosaic) lastxpos = xpos;
            }

            // draw pixel
            if (WindowMask[i] & (1<<bgnum))
            {
                u32 tilexoff = (curtile & 0x0400) ? (7-(xpos&0x7)) : (xpos&0x7);
                color = bgvram[(pixelsaddr + tilexoff) & bgvrammask];

                if (color)
                    drawPixel(&BGOBJLine[i], curpal[color], 0x01000000<<bgnum);
            }

            xoff++;
        }
    }
    else
    {
        // 16-color

        // preload shit as needed
        if ((xoff & 0x7) || mosaic)
        {
            curtile = *(u16*)&bgvram[((tilemapaddr + ((xoff & 0xF8) >> 2) + ((xoff & widexmask) << 3))) & bgvrammask];
            curpal = pal + ((curtile & 0xF000) >> 8);
            pixelsaddr = tilesetaddr + ((curtile & 0x03FF) << 5)
                                     + (((curtile & 0x0800) ? (7-(yoff&0x7)) : (yoff&0x7)) << 2);
        }

        if (mosaic) lastxpos = xoff;

        for (int i = 0; i < 256; i++)
        {
            u32 xpos;
            if (mosaic) xpos = xoff - CurBGXMosaicTable[i];
            else        xpos = xoff;

            if ((!mosaic && (!(xpos & 0x7))) ||
                (mosaic && ((xpos >> 3) != (lastxpos >> 3))))
            {
                // load a new tile
                curtile = *(u16*)&bgvram[(tilemapaddr + ((xpos & 0xF8) >> 2) + ((xpos & widexmask) << 3)) & bgvrammask];
                curpal = pal + ((curtile & 0xF000) >> 8);
                pixelsaddr = tilesetaddr + ((curtile & 0x03FF) << 5)
                                         + (((curtile & 0x0800) ? (7-(yoff&0x7)) : (yoff&0x7)) << 2);

                if (mosaic) lastxpos = xpos;
            }

            // draw pixel
            if (WindowMask[i] & (1<<bgnum))
            {
                u32 tilexoff = (curtile & 0x0400) ? (7-(xpos&0x7)) : (xpos&0x7);
                if (tilexoff & 0x1)
                {
                    color = bgvram[(pixelsaddr + (tilexoff >> 1)) & bgvrammask] >> 4;
                }
                else
                {
                    color = bgvram[(pixelsaddr + (tilexoff >> 1)) & bgvrammask] & 0x0F;
                }

                if (color)
                    drawPixel(&BGOBJLine[i], curpal[color], 0x01000000<<bgnum);
            }

            xoff++;
        }
    }
}

template<bool mosaic, SoftRenderer::DrawPixel drawPixel>
void SoftRenderer::DrawBG_Affine(u32 line, u32 bgnum)
{
    u16 bgcnt = CurUnit->BGCnt[bgnum];

    u32 tilesetaddr, tilemapaddr;
    u16* pal;

    u32 coordmask;
    u32 yshift;
    switch (bgcnt & 0xC000)
    {
    case 0x0000: coordmask = 0x07800; yshift = 7; break;
    case 0x4000: coordmask = 0x0F800; yshift = 8; break;
    case 0x8000: coordmask = 0x1F800; yshift = 9; break;
    case 0xC000: coordmask = 0x3F800; yshift = 10; break;
    }

    u32 overflowmask;
    if (bgcnt & 0x2000) overflowmask = 0;
    else                overflowmask = ~(coordmask | 0x7FF);

    s16 rotA = CurUnit->BGRotA[bgnum-2];
    s16 rotB = CurUnit->BGRotB[bgnum-2];
    s16 rotC = CurUnit->BGRotC[bgnum-2];
    s16 rotD = CurUnit->BGRotD[bgnum-2];

    s32 rotX = CurUnit->BGXRefInternal[bgnum-2];
    s32 rotY = CurUnit->BGYRefInternal[bgnum-2];

    if (bgcnt & 0x0040)
    {
        // vertical mosaic
        rotX -= (CurUnit->BGMosaicY * rotB);
        rotY -= (CurUnit->BGMosaicY * rotD);
    }

    u8* bgvram;
    u32 bgvrammask;
    CurUnit->GetBGVRAM(bgvram, bgvrammask);

    if (CurUnit->Num)
    {
        tilesetaddr = ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((bgcnt & 0x1F00) << 3);

        pal = (u16*)&GPU.Palette[0x400];
    }
    else
    {
        tilesetaddr = ((CurUnit->DispCnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
        tilemapaddr = ((CurUnit->DispCnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);

        pal = (u16*)&GPU.Palette[0];
    }

    u16 curtile;
    u8 color;

    yshift -= 3;

    for (int i = 0; i < 256; i++)
    {
        if (WindowMask[i] & (1<<bgnum))
        {
            s32 finalX, finalY;
            if (mosaic)
            {
                int im = CurBGXMosaicTable[i];
                finalX = rotX - (im * rotA);
                finalY = rotY - (im * rotC);
            }
            else
            {
                finalX = rotX;
                finalY = rotY;
            }

            if ((!((finalX|finalY) & overflowmask)))
            {
                curtile = bgvram[(tilemapaddr + ((((finalY & coordmask) >> 11) << yshift) + ((finalX & coordmask) >> 11))) & bgvrammask];

                // draw pixel
                u32 tilexoff = (finalX >> 8) & 0x7;
                u32 tileyoff = (finalY >> 8) & 0x7;

                color = bgvram[(tilesetaddr + (curtile << 6) + (tileyoff << 3) + tilexoff) & bgvrammask];

                if (color)
                    drawPixel(&BGOBJLine[i], pal[color], 0x01000000<<bgnum);
            }
        }

        rotX += rotA;
        rotY += rotC;
    }

    CurUnit->BGXRefInternal[bgnum-2] += rotB;
    CurUnit->BGYRefInternal[bgnum-2] += rotD;
}

template<bool mosaic, SoftRenderer::DrawPixel drawPixel>
void SoftRenderer::DrawBG_Extended(u32 line, u32 bgnum)
{
    u16 bgcnt = CurUnit->BGCnt[bgnum];

    u32 tilesetaddr, tilemapaddr;
    u16* pal;
    u32 extpal;

    u8* bgvram;
    u32 bgvrammask;
    CurUnit->GetBGVRAM(bgvram, bgvrammask);

    extpal = (CurUnit->DispCnt & 0x40000000);

    s16 rotA = CurUnit->BGRotA[bgnum-2];
    s16 rotB = CurUnit->BGRotB[bgnum-2];
    s16 rotC = CurUnit->BGRotC[bgnum-2];
    s16 rotD = CurUnit->BGRotD[bgnum-2];

    s32 rotX = CurUnit->BGXRefInternal[bgnum-2];
    s32 rotY = CurUnit->BGYRefInternal[bgnum-2];

    if (bgcnt & 0x0040)
    {
        // vertical mosaic
        rotX -= (CurUnit->BGMosaicY * rotB);
        rotY -= (CurUnit->BGMosaicY * rotD);
    }

    if (bgcnt & 0x0080)
    {
        // bitmap modes

        u32 xmask, ymask;
        u32 yshift;
        switch (bgcnt & 0xC000)
        {
        case 0x0000: xmask = 0x07FFF; ymask = 0x07FFF; yshift = 7; break;
        case 0x4000: xmask = 0x0FFFF; ymask = 0x0FFFF; yshift = 8; break;
        case 0x8000: xmask = 0x1FFFF; ymask = 0x0FFFF; yshift = 9; break;
        case 0xC000: xmask = 0x1FFFF; ymask = 0x1FFFF; yshift = 9; break;
        }

        u32 ofxmask, ofymask;
        if (bgcnt & 0x2000)
        {
            ofxmask = 0;
            ofymask = 0;
        }
        else
        {
            ofxmask = ~xmask;
            ofymask = ~ymask;
        }

        if (CurUnit->Num) tilemapaddr = ((bgcnt & 0x1F00) << 6);
        else              tilemapaddr = ((bgcnt & 0x1F00) << 6);

        if (bgcnt & 0x0004)
        {
            // direct color bitmap

            u16 color;

            for (int i = 0; i < 256; i++)
            {
                if (WindowMask[i] & (1<<bgnum))
                {
                    s32 finalX, finalY;
                    if (mosaic)
                    {
                        int im = CurBGXMosaicTable[i];
                        finalX = rotX - (im * rotA);
                        finalY = rotY - (im * rotC);
                    }
                    else
                    {
                        finalX = rotX;
                        finalY = rotY;
                    }

                    if (!(finalX & ofxmask) && !(finalY & ofymask))
                    {
                        const u32 pixelByteAddress =
                            (tilemapaddr + (((((finalY & ymask) >> 8) << yshift) + ((finalX & xmask) >> 8)) << 1)) & bgvrammask;
                        if (TryDrawStructuredVulkan2DCapturePixel(&BGOBJLine[i], pixelByteAddress))
                        {
                            rotX += rotA;
                            rotY += rotC;
                            continue;
                        }

                        color = *(u16*)&bgvram[pixelByteAddress];

                        if (color & 0x8000)
                            drawPixel(&BGOBJLine[i], color, 0x01000000<<bgnum);
                    }
                }

                rotX += rotA;
                rotY += rotC;
            }
        }
        else
        {
            // 256-color bitmap

            if (CurUnit->Num) pal = (u16*)&GPU.Palette[0x400];
            else              pal = (u16*)&GPU.Palette[0];

            u8 color;

            for (int i = 0; i < 256; i++)
            {
                if (WindowMask[i] & (1<<bgnum))
                {
                    s32 finalX, finalY;
                    if (mosaic)
                    {
                        int im = CurBGXMosaicTable[i];
                        finalX = rotX - (im * rotA);
                        finalY = rotY - (im * rotC);
                    }
                    else
                    {
                        finalX = rotX;
                        finalY = rotY;
                    }

                    if (!(finalX & ofxmask) && !(finalY & ofymask))
                    {
                        color = bgvram[(tilemapaddr + (((finalY & ymask) >> 8) << yshift) + ((finalX & xmask) >> 8)) & bgvrammask];

                        if (color)
                            drawPixel(&BGOBJLine[i], pal[color], 0x01000000<<bgnum);
                    }
                }

                rotX += rotA;
                rotY += rotC;
            }
        }
    }
    else
    {
        // mixed affine/text mode

        u32 coordmask;
        u32 yshift;
        switch (bgcnt & 0xC000)
        {
        case 0x0000: coordmask = 0x07800; yshift = 7; break;
        case 0x4000: coordmask = 0x0F800; yshift = 8; break;
        case 0x8000: coordmask = 0x1F800; yshift = 9; break;
        case 0xC000: coordmask = 0x3F800; yshift = 10; break;
        }

        u32 overflowmask;
        if (bgcnt & 0x2000) overflowmask = 0;
        else                overflowmask = ~(coordmask | 0x7FF);

        if (CurUnit->Num)
        {
            tilesetaddr = ((bgcnt & 0x003C) << 12);
            tilemapaddr = ((bgcnt & 0x1F00) << 3);

            pal = (u16*)&GPU.Palette[0x400];
        }
        else
        {
            tilesetaddr = ((CurUnit->DispCnt & 0x07000000) >> 8) + ((bgcnt & 0x003C) << 12);
            tilemapaddr = ((CurUnit->DispCnt & 0x38000000) >> 11) + ((bgcnt & 0x1F00) << 3);

            pal = (u16*)&GPU.Palette[0];
        }

        u16 curtile;
        u16* curpal;
        u8 color;

        yshift -= 3;

        for (int i = 0; i < 256; i++)
        {
            if (WindowMask[i] & (1<<bgnum))
            {
                s32 finalX, finalY;
                if (mosaic)
                {
                    int im = CurBGXMosaicTable[i];
                    finalX = rotX - (im * rotA);
                    finalY = rotY - (im * rotC);
                }
                else
                {
                    finalX = rotX;
                    finalY = rotY;
                }

                if ((!((finalX|finalY) & overflowmask)))
                {
                    curtile = *(u16*)&bgvram[(tilemapaddr + (((((finalY & coordmask) >> 11) << yshift) + ((finalX & coordmask) >> 11)) << 1)) & bgvrammask];

                    if (extpal) curpal = CurUnit->GetBGExtPal(bgnum, curtile>>12);
                    else        curpal = pal;

                    // draw pixel
                    u32 tilexoff = (finalX >> 8) & 0x7;
                    u32 tileyoff = (finalY >> 8) & 0x7;

                    if (curtile & 0x0400) tilexoff = 7-tilexoff;
                    if (curtile & 0x0800) tileyoff = 7-tileyoff;

                    color = bgvram[(tilesetaddr + ((curtile & 0x03FF) << 6) + (tileyoff << 3) + tilexoff) & bgvrammask];

                    if (color)
                        drawPixel(&BGOBJLine[i], curpal[color], 0x01000000<<bgnum);
                }
            }

            rotX += rotA;
            rotY += rotC;
        }
    }

    CurUnit->BGXRefInternal[bgnum-2] += rotB;
    CurUnit->BGYRefInternal[bgnum-2] += rotD;
}

template<bool mosaic, SoftRenderer::DrawPixel drawPixel>
void SoftRenderer::DrawBG_Large(u32 line) // BG is always BG2
{
    u16 bgcnt = CurUnit->BGCnt[2];

    u16* pal;

    // large BG sizes:
    // 0: 512x1024
    // 1: 1024x512
    // 2: 512x256
    // 3: 512x512
    u32 xmask, ymask;
    u32 yshift;
    switch (bgcnt & 0xC000)
    {
    case 0x0000: xmask = 0x1FFFF; ymask = 0x3FFFF; yshift = 9; break;
    case 0x4000: xmask = 0x3FFFF; ymask = 0x1FFFF; yshift = 10; break;
    case 0x8000: xmask = 0x1FFFF; ymask = 0x0FFFF; yshift = 9; break;
    case 0xC000: xmask = 0x1FFFF; ymask = 0x1FFFF; yshift = 9; break;
    }

    u32 ofxmask, ofymask;
    if (bgcnt & 0x2000)
    {
        ofxmask = 0;
        ofymask = 0;
    }
    else
    {
        ofxmask = ~xmask;
        ofymask = ~ymask;
    }

    s16 rotA = CurUnit->BGRotA[0];
    s16 rotB = CurUnit->BGRotB[0];
    s16 rotC = CurUnit->BGRotC[0];
    s16 rotD = CurUnit->BGRotD[0];

    s32 rotX = CurUnit->BGXRefInternal[0];
    s32 rotY = CurUnit->BGYRefInternal[0];

    if (bgcnt & 0x0040)
    {
        // vertical mosaic
        rotX -= (CurUnit->BGMosaicY * rotB);
        rotY -= (CurUnit->BGMosaicY * rotD);
    }

    u8* bgvram;
    u32 bgvrammask;
    CurUnit->GetBGVRAM(bgvram, bgvrammask);

    // 256-color bitmap

    if (CurUnit->Num) pal = (u16*)&GPU.Palette[0x400];
    else     pal = (u16*)&GPU.Palette[0];

    u8 color;

    for (int i = 0; i < 256; i++)
    {
        if (WindowMask[i] & (1<<2))
        {
            s32 finalX, finalY;
            if (mosaic)
            {
                int im = CurBGXMosaicTable[i];
                finalX = rotX - (im * rotA);
                finalY = rotY - (im * rotC);
            }
            else
            {
                finalX = rotX;
                finalY = rotY;
            }

            if (!(finalX & ofxmask) && !(finalY & ofymask))
            {
                color = bgvram[((((finalY & ymask) >> 8) << yshift) + ((finalX & xmask) >> 8)) & bgvrammask];

                if (color)
                    drawPixel(&BGOBJLine[i], pal[color], 0x01000000<<2);
            }
        }

        rotX += rotA;
        rotY += rotC;
    }

    CurUnit->BGXRefInternal[0] += rotB;
    CurUnit->BGYRefInternal[0] += rotD;
}

// OBJ line buffer:
// * bit0-15: color (bit15=1: direct color, bit15=0: palette index, bit12=0 to indicate extpal)
// * bit16-17: BG-relative priority
// * bit18: non-transparent sprite pixel exists here
// * bit19: X mosaic should be applied here
// * bit24-31: compositor flags

void SoftRenderer::ApplySpriteMosaicX()
{
    // apply X mosaic if needed
    // X mosaic for sprites is applied after all sprites are rendered

    if (CurUnit->OBJMosaicSize[0] == 0) return;

    u32* objLine = OBJLine[CurUnit->Num];

    u8* curOBJXMosaicTable = MosaicTable[CurUnit->OBJMosaicSize[0]].data();

    u32 lastcolor = objLine[0];

    for (u32 i = 1; i < 256; i++)
    {
        u32 currentcolor = objLine[i];

        if (!(lastcolor & currentcolor & 0x100000) || curOBJXMosaicTable[i] == 0)
            lastcolor = currentcolor;
        else
            objLine[i] = lastcolor;
    }
}

template <SoftRenderer::DrawPixel drawPixel>
void SoftRenderer::InterleaveSprites(u32 prio)
{
    u32* objLine = OBJLine[CurUnit->Num];
    u16* pal = (u16*)&GPU.Palette[CurUnit->Num ? 0x600 : 0x200];

    if (CurUnit->DispCnt & 0x80000000)
    {
        u16* extpal = CurUnit->GetOBJExtPal();

        for (u32 i = 0; i < 256; i++)
        {
            if ((objLine[i] & 0x70000) != prio) continue;
            if (!(WindowMask[i] & 0x10))        continue;

            u16 color;
            u32 pixel = objLine[i];

            if (pixel & 0x8000)
                color = pixel & 0x7FFF;
            else if (pixel & 0x1000)
                color = pal[pixel & 0xFF];
            else
                color = extpal[pixel & 0xFFF];

            drawPixel(&BGOBJLine[i], color, pixel & 0xFF000000);
        }
    }
    else
    {
        // optimized no-extpal version

        for (u32 i = 0; i < 256; i++)
        {
            if ((objLine[i] & 0x70000) != prio) continue;
            if (!(WindowMask[i] & 0x10))        continue;

            u16 color;
            u32 pixel = objLine[i];

            if (pixel & 0x8000)
                color = pixel & 0x7FFF;
            else
                color = pal[pixel & 0xFF];

            drawPixel(&BGOBJLine[i], color, pixel & 0xFF000000);
        }
    }
}

#define DoDrawSprite(type, ...) \
    if (iswin) \
    { \
        DrawSprite_##type<true>(__VA_ARGS__); \
    } \
    else \
    { \
        DrawSprite_##type<false>(__VA_ARGS__); \
    }

void SoftRenderer::DrawSprites(u32 line, Unit* unit)
{
    CurUnit = unit;

    if (line == 0)
    {
        // reset those counters here
        // TODO: find out when those are supposed to be reset
        // it would make sense to reset them at the end of VBlank
        // however, sprites are rendered one scanline in advance
        // so they need to be reset a bit earlier

        CurUnit->OBJMosaicY = 0;
        CurUnit->OBJMosaicYCount = 0;
    }

    if (CurUnit->Num == 0)
    {
        auto objDirty = GPU.VRAMDirty_AOBJ.DeriveState(GPU.VRAMMap_AOBJ, GPU);
        GPU.MakeVRAMFlat_AOBJCoherent(objDirty);
    }
    else
    {
        auto objDirty = GPU.VRAMDirty_BOBJ.DeriveState(GPU.VRAMMap_BOBJ, GPU);
        GPU.MakeVRAMFlat_BOBJCoherent(objDirty);
    }

    NumSprites[CurUnit->Num] = 0;
    memset(OBJLine[CurUnit->Num], 0, 256*4);
    memset(OBJWindow[CurUnit->Num], 0, 256);
    if (!MelonDSAndroid::areRenderer2DDebugObjectsEnabled(CurUnit->Num)) return;
    if (!Renderer2DDebugShouldDrawObjectLine(line)) return;
    if (!(CurUnit->DispCnt & 0x1000)) return;

    u16* oam = (u16*)&GPU.OAM[CurUnit->Num ? 0x400 : 0];

    const s32 spritewidth[16] =
    {
        8, 16, 8, 8,
        16, 32, 8, 8,
        32, 32, 16, 8,
        64, 64, 32, 8
    };
    const s32 spriteheight[16] =
    {
        8, 8, 16, 8,
        16, 8, 32, 8,
        32, 16, 32, 8,
        64, 32, 64, 8
    };

    for (int bgnum = 0x0C00; bgnum >= 0x0000; bgnum -= 0x0400)
    {
        if (!MelonDSAndroid::isRenderer2DDebugObjectPriorityEnabled(CurUnit->Num, static_cast<u32>(bgnum) >> 10u))
            continue;

        for (int sprnum = 127; sprnum >= 0; sprnum--)
        {
            u16* attrib = &oam[sprnum*4];

            if ((attrib[2] & 0x0C00) != bgnum)
                continue;
            if (!Renderer2DDebugShouldDrawObject(CurUnit->Num, static_cast<u32>(sprnum), attrib))
                continue;

            bool iswin = (((attrib[0] >> 10) & 0x3) == 2);

            u32 sprline;
            if ((attrib[0] & 0x1000) && !iswin)
            {
                // apply Y mosaic
                sprline = CurUnit->OBJMosaicY;
            }
            else
                sprline = line;

            if (attrib[0] & 0x0100)
            {
                u32 sizeparam = (attrib[0] >> 14) | ((attrib[1] & 0xC000) >> 12);
                s32 width = spritewidth[sizeparam];
                s32 height = spriteheight[sizeparam];
                s32 boundwidth = width;
                s32 boundheight = height;

                if (attrib[0] & 0x0200)
                {
                    boundwidth <<= 1;
                    boundheight <<= 1;
                }

                u32 ypos = attrib[0] & 0xFF;
                if (((line - ypos) & 0xFF) >= (u32)boundheight)
                    continue;
                ypos = (sprline - ypos) & 0xFF;

                s32 xpos = (s32)(attrib[1] << 23) >> 23;
                if (xpos <= -boundwidth)
                    continue;

                u32 rotparamgroup = (attrib[1] >> 9) & 0x1F;

                DoDrawSprite(Rotscale, sprnum, boundwidth, boundheight, width, height, xpos, ypos);

                NumSprites[CurUnit->Num]++;
            }
            else
            {
                if (attrib[0] & 0x0200)
                    continue;

                u32 sizeparam = (attrib[0] >> 14) | ((attrib[1] & 0xC000) >> 12);
                s32 width = spritewidth[sizeparam];
                s32 height = spriteheight[sizeparam];

                u32 ypos = attrib[0] & 0xFF;
                if (((line - ypos) & 0xFF) >= (u32)height)
                    continue;
                ypos = (sprline - ypos) & 0xFF;

                s32 xpos = (s32)(attrib[1] << 23) >> 23;
                if (xpos <= -width)
                    continue;

                DoDrawSprite(Normal, sprnum, width, height, xpos, ypos);

                NumSprites[CurUnit->Num]++;
            }
        }
    }
}

template<bool window>
void SoftRenderer::DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos)
{
    u16* oam = (u16*)&GPU.OAM[CurUnit->Num ? 0x400 : 0];
    u16* attrib = &oam[num * 4];
    u16* rotparams = &oam[(((attrib[1] >> 9) & 0x1F) * 16) + 3];

    u32 pixelattr = ((attrib[2] & 0x0C00) << 6) | 0xC0000;
    u32 tilenum = attrib[2] & 0x03FF;
    u32 spritemode = window ? 0 : ((attrib[0] >> 10) & 0x3);

    u32 ytilefactor;

    u8* objvram;
    u32 objvrammask;
    CurUnit->GetOBJVRAM(objvram, objvrammask);

    u32* objLine = OBJLine[CurUnit->Num];
    u8* objWindow = OBJWindow[CurUnit->Num];

    s32 centerX = boundwidth >> 1;
    s32 centerY = boundheight >> 1;

    if ((attrib[0] & 0x1000) && !window)
    {
        // apply Y mosaic
        pixelattr |= 0x100000;
    }

    u32 xoff;
    if (xpos >= 0)
    {
        xoff = 0;
        if ((xpos+boundwidth) > 256)
            boundwidth = 256-xpos;
    }
    else
    {
        xoff = -xpos;
        xpos = 0;
    }

    s16 rotA = (s16)rotparams[0];
    s16 rotB = (s16)rotparams[4];
    s16 rotC = (s16)rotparams[8];
    s16 rotD = (s16)rotparams[12];

    s32 rotX = ((xoff-centerX) * rotA) + ((ypos-centerY) * rotB) + (width << 7);
    s32 rotY = ((xoff-centerX) * rotC) + ((ypos-centerY) * rotD) + (height << 7);

    width <<= 8;
    height <<= 8;

    u16 color = 0; // transparent in all cases

    if (spritemode == 3)
    {
        u32 alpha = attrib[2] >> 12;
        if (!alpha) return;
        alpha++;

        pixelattr |= (0xC0000000 | (alpha << 24));

        u32 pixelsaddr;
        if (CurUnit->DispCnt & 0x40)
        {
            if (CurUnit->DispCnt & 0x20)
            {
                // 'reserved'
                // draws nothing

                return;
            }
            else
            {
                pixelsaddr = tilenum << (7 + ((CurUnit->DispCnt >> 22) & 0x1));
                ytilefactor = ((width >> 8) * 2);
            }
        }
        else
        {
            if (CurUnit->DispCnt & 0x20)
            {
                pixelsaddr = ((tilenum & 0x01F) << 4) + ((tilenum & 0x3E0) << 7);
                ytilefactor = (256 * 2);
            }
            else
            {
                pixelsaddr = ((tilenum & 0x00F) << 4) + ((tilenum & 0x3F0) << 7);
                ytilefactor = (128 * 2);
            }
        }

        for (; xoff < boundwidth;)
        {
            if ((u32)rotX < width && (u32)rotY < height)
            {
                const u32 sampleAddr = (pixelsaddr + ((rotY >> 8) * ytilefactor) + ((rotX >> 8) << 1)) & objvrammask;
                color = *(u16*)&objvram[sampleAddr];

                const u32 currentLine = (GPU.VCount + 1u) & 0xFFu;
                if (!window && CurUnit->Num == 1 && MelonDSAndroid::areRendererDebugBgObjLogsEnabled())
                {
                    if (const RendererDebugSamplePoint* sample = findRendererDebugSamplePoint(static_cast<u32>(xpos), currentLine))
                    {
                        Platform::Log(
                            Platform::LogLevel::Warn,
                            "RendererDebug[SpriteBitmap]: unit=%u label=%s sprite=%u rotscale=1 line=%u x=%u color=%04X old=%08X pixelattr=%08X pixelsaddr=%u tilenum=%u srcX=%u srcY=%u attr0=%04X attr1=%04X attr2=%04X",
                            CurUnit->Num,
                            sample->label,
                            num,
                            currentLine,
                            static_cast<u32>(xpos),
                            color,
                            objLine[xpos],
                            pixelattr,
                            sampleAddr,
                            tilenum,
                            static_cast<u32>(rotX >> 8),
                            static_cast<u32>(rotY >> 8),
                            attrib[0],
                            attrib[1],
                            attrib[2]
                        );
                    }
                }

                if (color & 0x8000)
                {
                    if (window) objWindow[xpos] = 1;
                    else        objLine[xpos] = color | pixelattr;
                }
                else if (!window)
                {
                    if (objLine[xpos] == 0)
                        objLine[xpos] = pixelattr & 0x180000;
                }
            }

            rotX += rotA;
            rotY += rotC;
            xoff++;
            xpos++;
        }
    }
    else
    {
        u32 pixelsaddr = tilenum;
        if (CurUnit->DispCnt & 0x10)
        {
            pixelsaddr <<= ((CurUnit->DispCnt >> 20) & 0x3);
            ytilefactor = (width >> 11) << ((attrib[0] & 0x2000) ? 1:0);
        }
        else
        {
            ytilefactor = 0x20;
        }

        if (spritemode == 1) pixelattr |= 0x80000000;
        else                 pixelattr |= 0x10000000;

        ytilefactor <<= 5;
        pixelsaddr <<= 5;

        if (attrib[0] & 0x2000)
        {
            // 256-color

            if (!window)
            {
                if (!(CurUnit->DispCnt & 0x80000000))
                    pixelattr |= 0x1000;
                else
                    pixelattr |= ((attrib[2] & 0xF000) >> 4);
            }

            for (; xoff < boundwidth;)
            {
                if ((u32)rotX < width && (u32)rotY < height)
                {
                    color = objvram[(pixelsaddr + ((rotY>>11)*ytilefactor) + ((rotY&0x700)>>5) + ((rotX>>11)*64) + ((rotX&0x700)>>8)) & objvrammask];

                    if (color)
                    {
                        if (window) objWindow[xpos] = 1;
                        else        objLine[xpos] = color | pixelattr;
                    }
                    else if (!window)
                    {
                        if (objLine[xpos] == 0)
                            objLine[xpos] = pixelattr & 0x180000;
                    }
                }

                rotX += rotA;
                rotY += rotC;
                xoff++;
                xpos++;
            }
        }
        else
        {
            // 16-color
            if (!window)
            {
                pixelattr |= 0x1000;
                pixelattr |= ((attrib[2] & 0xF000) >> 8);
            }

            for (; xoff < boundwidth;)
            {
                if ((u32)rotX < width && (u32)rotY < height)
                {
                    color = objvram[(pixelsaddr + ((rotY>>11)*ytilefactor) + ((rotY&0x700)>>6) + ((rotX>>11)*32) + ((rotX&0x700)>>9)) & objvrammask];
                    if (rotX & 0x100)
                        color >>= 4;
                    else
                        color &= 0x0F;

                    if (color)
                    {
                        if (window) objWindow[xpos] = 1;
                        else        objLine[xpos] = color | pixelattr;
                    }
                    else if (!window)
                    {
                        if (objLine[xpos] == 0)
                            objLine[xpos] = pixelattr & 0x180000;
                    }
                }

                rotX += rotA;
                rotY += rotC;
                xoff++;
                xpos++;
            }
        }
    }
}

template<bool window>
void SoftRenderer::DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos)
{
    u16* oam = (u16*)&GPU.OAM[CurUnit->Num ? 0x400 : 0];
    u16* attrib = &oam[num * 4];

    u32 pixelattr = ((attrib[2] & 0x0C00) << 6) | 0xC0000;
    u32 tilenum = attrib[2] & 0x03FF;
    u32 spritemode = window ? 0 : ((attrib[0] >> 10) & 0x3);

    u32 wmask = width - 8; // really ((width - 1) & ~0x7)

    if ((attrib[0] & 0x1000) && !window)
    {
        // apply Y mosaic
        pixelattr |= 0x100000;
    }

    u8* objvram;
    u32 objvrammask;
    CurUnit->GetOBJVRAM(objvram, objvrammask);

    u32* objLine = OBJLine[CurUnit->Num];
    u8* objWindow = OBJWindow[CurUnit->Num];

    // yflip
    if (attrib[1] & 0x2000)
        ypos = height-1 - ypos;

    u32 xoff;
    u32 xend = width;
    if (xpos >= 0)
    {
        xoff = 0;
        if ((xpos+xend) > 256)
            xend = 256-xpos;
    }
    else
    {
        xoff = -xpos;
        xpos = 0;
    }

    u16 color = 0; // transparent in all cases

    if (spritemode == 3)
    {
        // bitmap sprite

        u32 alpha = attrib[2] >> 12;
        if (!alpha) return;
        alpha++;

        pixelattr |= (0xC0000000 | (alpha << 24));

        u32 pixelsaddr = tilenum;
        if (CurUnit->DispCnt & 0x40)
        {
            if (CurUnit->DispCnt & 0x20)
            {
                // 'reserved'
                // draws nothing

                return;
            }
            else
            {
                pixelsaddr <<= (7 + ((CurUnit->DispCnt >> 22) & 0x1));
                pixelsaddr += (ypos * width * 2);
            }
        }
        else
        {
            if (CurUnit->DispCnt & 0x20)
            {
                pixelsaddr = ((tilenum & 0x01F) << 4) + ((tilenum & 0x3E0) << 7);
                pixelsaddr += (ypos * 256 * 2);
            }
            else
            {
                pixelsaddr = ((tilenum & 0x00F) << 4) + ((tilenum & 0x3F0) << 7);
                pixelsaddr += (ypos * 128 * 2);
            }
        }

        s32 pixelstride;

        if (attrib[1] & 0x1000) // xflip
        {
            pixelsaddr += ((width-1) << 1);
            pixelsaddr -= (xoff << 1);
            pixelstride = -2;
        }
        else
        {
            pixelsaddr += (xoff << 1);
            pixelstride = 2;
        }

        for (; xoff < xend;)
        {
            const u32 sampleAddr = pixelsaddr & objvrammask;
            color = *(u16*)&objvram[sampleAddr];

            const u32 currentLine = (GPU.VCount + 1u) & 0xFFu;
            if (!window && CurUnit->Num == 1 && MelonDSAndroid::areRendererDebugBgObjLogsEnabled())
            {
                if (const RendererDebugSamplePoint* sample = findRendererDebugSamplePoint(static_cast<u32>(xpos), currentLine))
                {
                    Platform::Log(
                        Platform::LogLevel::Warn,
                        "RendererDebug[SpriteBitmap]: unit=%u label=%s sprite=%u rotscale=0 line=%u x=%u color=%04X old=%08X pixelattr=%08X pixelsaddr=%u tilenum=%u ypos=%u xoff=%u attr0=%04X attr1=%04X attr2=%04X",
                        CurUnit->Num,
                        sample->label,
                        num,
                        currentLine,
                        static_cast<u32>(xpos),
                        color,
                        objLine[xpos],
                        pixelattr,
                        sampleAddr,
                        tilenum,
                        ypos,
                        xoff,
                        attrib[0],
                        attrib[1],
                        attrib[2]
                    );
                }
            }

            pixelsaddr += pixelstride;

            if (color & 0x8000)
            {
                if (window) objWindow[xpos] = 1;
                else        objLine[xpos] = color | pixelattr;
            }
            else if (!window)
            {
                if (objLine[xpos] == 0)
                    objLine[xpos] = pixelattr & 0x180000;
            }

            xoff++;
            xpos++;
        }
    }
    else
    {
        u32 pixelsaddr = tilenum;
        if (CurUnit->DispCnt & 0x10)
        {
            pixelsaddr <<= ((CurUnit->DispCnt >> 20) & 0x3);
            pixelsaddr += ((ypos >> 3) * (width >> 3)) << ((attrib[0] & 0x2000) ? 1:0);
        }
        else
        {
            pixelsaddr += ((ypos >> 3) * 0x20);
        }

        if (spritemode == 1) pixelattr |= 0x80000000;
        else                 pixelattr |= 0x10000000;

        if (attrib[0] & 0x2000)
        {
            // 256-color
            pixelsaddr <<= 5;
            pixelsaddr += ((ypos & 0x7) << 3);
            s32 pixelstride;

            if (!window)
            {
                if (!(CurUnit->DispCnt & 0x80000000))
                    pixelattr |= 0x1000;
                else
                    pixelattr |= ((attrib[2] & 0xF000) >> 4);
            }

            if (attrib[1] & 0x1000) // xflip
            {
                pixelsaddr += (((width-1) & wmask) << 3);
                pixelsaddr += ((width-1) & 0x7);
                pixelsaddr -= ((xoff & wmask) << 3);
                pixelsaddr -= (xoff & 0x7);
                pixelstride = -1;
            }
            else
            {
                pixelsaddr += ((xoff & wmask) << 3);
                pixelsaddr += (xoff & 0x7);
                pixelstride = 1;
            }

            for (; xoff < xend;)
            {
                color = objvram[pixelsaddr & objvrammask];

                pixelsaddr += pixelstride;

                if (color)
                {
                    if (window) objWindow[xpos] = 1;
                    else        objLine[xpos] = color | pixelattr;
                }
                else if (!window)
                {
                    if (objLine[xpos] == 0)
                        objLine[xpos] = pixelattr & 0x180000;
                }

                xoff++;
                xpos++;
                if (!(xoff & 0x7)) pixelsaddr += (56 * pixelstride);
            }
        }
        else
        {
            // 16-color
            pixelsaddr <<= 5;
            pixelsaddr += ((ypos & 0x7) << 2);
            s32 pixelstride;

            if (!window)
            {
                pixelattr |= 0x1000;
                pixelattr |= ((attrib[2] & 0xF000) >> 8);
            }

            // TODO: optimize VRAM access!!
            // TODO: do xflip better? the 'two pixels per byte' thing makes it a bit shitty

            if (attrib[1] & 0x1000) // xflip
            {
                pixelsaddr += (((width-1) & wmask) << 2);
                pixelsaddr += (((width-1) & 0x7) >> 1);
                pixelsaddr -= ((xoff & wmask) << 2);
                pixelsaddr -= ((xoff & 0x7) >> 1);
                pixelstride = -1;
            }
            else
            {
                pixelsaddr += ((xoff & wmask) << 2);
                pixelsaddr += ((xoff & 0x7) >> 1);
                pixelstride = 1;
            }

            for (; xoff < xend;)
            {
                if (attrib[1] & 0x1000)
                {
                    if (xoff & 0x1) { color = objvram[pixelsaddr & objvrammask] & 0x0F; pixelsaddr--; }
                    else              color = objvram[pixelsaddr & objvrammask] >> 4;
                }
                else
                {
                    if (xoff & 0x1) { color = objvram[pixelsaddr & objvrammask] >> 4; pixelsaddr++; }
                    else              color = objvram[pixelsaddr & objvrammask] & 0x0F;
                }

                if (color)
                {
                    if (window) objWindow[xpos] = 1;
                    else        objLine[xpos] = color | pixelattr;
                }
                else if (!window)
                {
                    if (objLine[xpos] == 0)
                        objLine[xpos] = pixelattr & 0x180000;
                }

                xoff++;
                xpos++;
                if (!(xoff & 0x7)) pixelsaddr += ((attrib[1] & 0x1000) ? -28 : 28);
            }
        }
    }
}

}

}

}
