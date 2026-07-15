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

#pragma once

#include <cstddef>

#include "SapphireGPU2DRenderer2D.h"

namespace melonDS
{
class GPU;

namespace SapphireGPU2DCore
{
namespace GPU2D
{

class SoftRenderer : public Renderer2D
{
public:
    struct DebugCaptureStats
    {
        u32 CaptureLines = 0;
        u32 CaptureWidth = 0;
        u32 CaptureMode = 0;
        u32 CaptureBit24 = 0;
        u32 Direct3DLines = 0;
        u32 SourceACompositeLines = 0;
        u32 CaptureLineUses3dLines = 0;
        u32 CaptureLineUsefulAlphaLines = 0;
        u32 CaptureDestinationBlankLines = 0;
        u32 Opaque3DSourcePixels = 0;
        u32 Opaque3DBackdropPixels = 0;
        u32 SourceAOutputUsefulPixels = 0;
        u32 SourceAOutputVisiblePixels = 0;
        u32 SourceAOutputOpaqueBlackPixels = 0;
        u32 StructuredCopyLines = 0;
        u32 StructuredCopyPlane0UsefulPixels = 0;
        u32 StructuredCopyPlane1UsefulPixels = 0;
        u32 StructuredCopySlotPixels = 0;
        u32 StructuredCopyAbovePixels = 0;
        u32 StructuredCopy2DOnlyPixels = 0;
        u32 StructuredCopySourceBOverlayPixels = 0;
        u32 CaptureBacked3DLines = 0;
        u32 CaptureBacked3DNoBestClassLines = 0;
        u32 CaptureBacked3DExplicitSlotLines = 0;
        u32 CaptureBacked3DBestClassCounts[17] {};
        u32 CompModeCounts[8] {};
    };

    SoftRenderer(melonDS::GPU& gpu);
    ~SoftRenderer() override {}

    void DrawScanline(u32 line, Unit* unit) override;
    void DrawSprites(u32 line, Unit* unit) override;
    void VBlankEnd(Unit* unitA, Unit* unitB) override;
    [[nodiscard]] const DebugCaptureStats& GetDebugCaptureStats() const noexcept { return LastDebugCaptureStats; }
    [[nodiscard]] const u32* GetDebugCapture3dSource() const noexcept { return HasLastDebugCapture3dSource ? LastDebugCapture3dSource : nullptr; }
    [[nodiscard]] const std::array<u8, 192>& GetDebugCaptureLineUses3dMask() const noexcept { return CaptureLineUses3d; }
    [[nodiscard]] const u32* GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept;
    void ClearStructuredVulkan2DState() noexcept;
private:
    static constexpr size_t kStructuredScreenWidth = 256;
    static constexpr size_t kStructuredScreenHeight = 192;
    static constexpr size_t kStructuredPixelCount = kStructuredScreenWidth * kStructuredScreenHeight;
    static constexpr size_t kStructuredPlaneCount = 3;
    static constexpr size_t kStructuredScreenCount = 2;

    melonDS::GPU& GPU;
    alignas(8) u32 BGOBJLine[256*3];
    u32* _3DLine;

    alignas(8) u8 WindowMask[256];

    alignas(8) u32 OBJLine[2][256];
    alignas(8) u8 OBJWindow[2][256];

    u32 NumSprites[2];

    u8* CurBGXMosaicTable;
    array2d<u8, 16, 256> MosaicTable = []() constexpr
    {
        array2d<u8, 16, 256> table {};
        // initialize mosaic table
        for (int m = 0; m < 16; m++)
        {
            for (int x = 0; x < 256; x++)
            {
                int offset = x % (m+1);
                table[m][x] = offset;
            }
        }

        return table;
    }();

    static constexpr u32 ColorBlend4(u32 val1, u32 val2, u32 eva, u32 evb) noexcept
    {
        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000008) >> 4;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x000800) >> 4) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x080000) >> 4) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBlend5(u32 val1, u32 val2) noexcept
    {
        u32 eva = ((val1 >> 24) & 0x1F) + 1;
        u32 evb = 32 - eva;

        if (eva == 32) return val1;

        u32 r =  (((val1 & 0x00003F) * eva) + ((val2 & 0x00003F) * evb) + 0x000010) >> 5;
        u32 g = ((((val1 & 0x003F00) * eva) + ((val2 & 0x003F00) * evb) + 0x001000) >> 5) & 0x007F00;
        u32 b = ((((val1 & 0x3F0000) * eva) + ((val2 & 0x3F0000) * evb) + 0x100000) >> 5) & 0x7F0000;

        if (r > 0x00003F) r = 0x00003F;
        if (g > 0x003F00) g = 0x003F00;
        if (b > 0x3F0000) b = 0x3F0000;

        return r | g | b | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessUp(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb += (((((0x3F003F - rb) * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g +=  (((((0x003F00 - g ) * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }

    static constexpr u32 ColorBrightnessDown(u32 val, u32 factor, u32 bias) noexcept
    {
        u32 rb = val & 0x3F003F;
        u32 g = val & 0x003F00;

        rb -= ((((rb * factor) + (bias*0x010001)) >> 4) & 0x3F003F);
        g -=  ((((g  * factor) + (bias*0x000100)) >> 4) & 0x003F00);

        return rb | g | 0xFF000000;
    }
    u32 ColorComposite(int i, u32 val1, u32 val2) const;
    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    void ClearStructuredVulkan2DLine(u32 line);
    void ClearStructuredVulkan2DCapture(u32 vramBank);
    void ClearStructuredVulkan2DCaptureRange(u32 vramBank, u32 dstAddress, u32 width);
    void SaveStructuredVulkan2DCaptureSourceLine(u32 line);
    void CopyStructuredVulkan2DCaptureSourceLineToCapture(u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCurrentLineToCapture(u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCaptureLineToCurrentScreen(u32 line, u32 vramBank);
    bool ReadStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank,
        u32 vramAddress,
        u32& overlayPixel,
        u32& overlayControlAlpha) const noexcept;
    void MergeStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank,
        u32 vramAddress,
        u32 overlayPixel,
        u32 overlayControlAlpha);
    [[nodiscard]] bool CurrentUnitTargetsTopScreen() const noexcept;
    void StoreStructuredVulkan2DPixel(
        u32 line,
        u32 x,
        u32 originalVal1,
        u32 originalVal2,
        u32 originalVal3,
        u32 legacyVal1,
        u32 legacyVal2,
        u32 legacyControl,
        u32 captureBacked3DSourceClass);
    void StoreStructuredVulkan2DCapturePixel(
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
        bool allowUnclassifiedExternal3DSlot);

    template<u32 bgmode> void DrawScanlineBGMode(u32 line);
    void DrawScanlineBGMode6(u32 line);
    void DrawScanlineBGMode7(u32 line);
    void DrawScanline_BGOBJ(u32 line);

    static void DrawPixel_Normal(u32* dst, u16 color, u32 flag);
    static void DrawPixel_Accel(u32* dst, u16 color, u32 flag);
    static void PushRawPixel_Accel(u32* dst, u32 value);
    bool TryDrawStructuredVulkan2DCapturePixel(u32* dst, u32 flatByteAddress);

    typedef void (*DrawPixel)(u32* dst, u16 color, u32 flag);

    void DrawBG_3D();
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Text(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Affine(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Extended(u32 line, u32 bgnum);
    template<bool mosaic, DrawPixel drawPixel> void DrawBG_Large(u32 line);

    void ApplySpriteMosaicX();
    template<DrawPixel drawPixel>
    void InterleaveSprites(u32 prio);
    template<bool window> void DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos);
    template<bool window> void DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos);

    void DoCapture(u32 line, u32 width, u32 sourceLine);

    DebugCaptureStats LastDebugCaptureStats {};
    bool HasLastDebugCapture3dSource = false;
    alignas(8) u32 LastDebugCapture3dSource[256 * 192] {};
    std::array<u8, 192> CaptureLineUses3d {};
    bool CurrentLineRegularCaptureUses3d = false;
    std::array<u32, kStructuredScreenCount * kStructuredPlaneCount * kStructuredPixelCount> StructuredVulkan2DPlanes {};
    std::array<u32, kStructuredPlaneCount * kStructuredScreenWidth> StructuredVulkan2DCaptureSourceLine {};
    bool StructuredVulkan2DCaptureSourceLineValid = false;
    u32 StructuredVulkan2DCaptureSourceLineY = 0;
    bool StructuredVulkan2DCurrentLineTargetsTop = false;
    std::array<u32, 4 * kStructuredPlaneCount * kStructuredPixelCount> StructuredVulkan2DCapturePlanes {};
    std::array<u8, 4 * kStructuredScreenHeight> StructuredVulkan2DCaptureLineValid {};
};

}

}

}
