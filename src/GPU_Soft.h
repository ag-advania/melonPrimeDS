/*
    Copyright 2016-2026 melonDS team

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

#ifndef GPU_SOFT_H
#define GPU_SOFT_H

#include "GPU.h"
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"

#ifdef MELONPRIME_DS
// Defines MELONPRIME_HAS_STRUCTURED_SOFT_2D, shared with the GPU2D_Soft
// producer side.
#include "MelonPrimeStructuredComposition.h"
#endif

namespace melonDS
{

class SoftRenderer : public Renderer
{
public:
    explicit SoftRenderer(melonDS::NDS& nds);
    ~SoftRenderer() override;
    bool Init() override { return true; }
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override {};
    void VBlankEnd() override {};

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;
    void InvalidateVRAMCapture(u32 bank, u32 start, u32 len) override;

    bool GetFramebuffers(void** top, void** bottom) override;

protected:
    [[nodiscard]] const u32* GetSoftwareCaptureSourceLine(bool source3D) const noexcept
    {
        return source3D ? Output3D : Output2D[0];
    }

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
public:
    [[nodiscard]] u32 GetCaptureTextureReference(
        u32 bank, u32 address) const noexcept override;

    struct StructuredVulkanFrameView
    {
        const u32* Plane[2][StructuredComposition::kPlaneCount]{};
        const u32* CaptureSourcePlane[StructuredComposition::kPlaneCount]{};
        const u32* CaptureSourceBNative = nullptr;
        const u32* CaptureSourceBReference = nullptr;
        const u32* CaptureCommands = nullptr;
        const u32* LineMeta[2]{};
        bool NativeMenuHeld = false;
        bool Valid = false;
        u64 Generation = 0;
    };

    [[nodiscard]] bool GetStructuredVulkanFrame(StructuredVulkanFrameView& view) const noexcept;
    // Published to the frontend so the Custom HUD can tell when MPH's native
    // START menu is held. It never selects composition behaviour.
    void SetNativeMenuHeldForFrame(bool held) noexcept
    {
        NativeMenuHeldForFrame = held;
    }
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    static constexpr std::size_t StructuredPixelCount = 256u * 192u;
    static constexpr std::size_t StructuredCapturePixelCount = 256u * 256u;
    static constexpr std::size_t StructuredCaptureLineCount = 256u;
    std::array<u32, 2u * StructuredComposition::kPlaneCount * StructuredPixelCount> StructuredEnginePlanes{};
    std::array<u32, 2u * StructuredComposition::kPlaneCount * StructuredPixelCount> StructuredScreenPlanes{};
    std::array<u32, 2u * 192u> StructuredScreenLineMeta{};
    std::array<u32, 4u * 3u * StructuredCapturePixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * StructuredCaptureLineCount> StructuredCaptureLineValid{};
    std::array<u8, 4u * StructuredCapturePixelCount> StructuredCapturePixelValid{};
    std::array<u8, 4u * StructuredCapturePixelCount> StructuredCapturePixelVersion{};
    std::array<u8, 4u> StructuredCaptureBankVersion{};
    std::array<u8, 4u> StructuredCaptureBankWrittenThisFrame{};
    std::array<u32, StructuredPixelCount> StructuredCaptureSourceBNative{};
    std::array<u32, StructuredPixelCount> StructuredCaptureSourceBReference{};
    std::array<u32, 192u * StructuredComposition::kCaptureCommandWords> StructuredCaptureCommands{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;
    bool StructuredCapture3DValid = false;
    bool StructuredFrameNativeMenuHeld = false;
    bool NativeMenuHeldForFrame = false;
    u64 StructuredFrameGeneration = 0;

    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    inline void StoreStructuredEnginePixel(
        u32 engine,
        u32 line,
        u32 x,
        u32 val1,
        u32 val2,
        u32 composed,
        u32 compositionMode,
        u32 eva,
        u32 evb,
        u32 reference1 = 0,
        u32 reference2 = 0)
    {
        namespace Contract = StructuredComposition;
        if (engine >= 2u || line >= 192u || x >= 256u)
            return;

        const std::size_t pixelIndex = static_cast<std::size_t>(line) * 256u + x;
        const std::size_t engineBase = static_cast<std::size_t>(engine)
            * Contract::kPlaneCount * StructuredPixelCount;
        u32 plane0 = composed;
        u32 plane1 = 0;
        u32 captureReference = 0;
        u32 controlAlpha = Contract::kControlPlain2D;
        // These are the software 2D engine's BG/OBJ flags, not structured
        // control bits. 0x40 identifies the 3D layer and 0x80 distinguishes a
        // blend-flagged sprite that only resembles it.
        const u32 alpha1 = val1 >> 24u;
        const u32 alpha2 = val2 >> 24u;
        const bool val1Is3D = (alpha1 & 0x40u) != 0u && (alpha1 & 0x80u) == 0u;
        const bool val2Is3D = (alpha2 & 0x40u) != 0u && (alpha2 & 0x80u) == 0u;

        const bool val1IsCapture = reference1 != 0u;
        const bool val2IsCapture = reference2 != 0u;

        if (val1Is3D || val1IsCapture)
        {
            plane0 = val2;
            captureReference = reference1;
            controlAlpha = Contract::kControlHas3DSlot
                | (compositionMode & Contract::kControlCompositionModeMask);
            if ((plane0 & 0x00FFFFFFu) == 0 && (plane0 >> 24u) != 0)
                controlAlpha |= Contract::kControlOpaqueBlackBelow;
        }
        else if ((val2Is3D || val2IsCapture) && compositionMode == Contract::kCompositionModeBlend4)
        {
            plane0 = 0;
            plane1 = val1;
            controlAlpha = Contract::kControlHas3DSlot
                | Contract::kControlAbovePlane
                | Contract::kCompositionModeBlend4;
            if ((plane1 & 0x00FFFFFFu) == 0 && (plane1 >> 24u) != 0)
                controlAlpha |= Contract::kControlOpaqueBlackBelow;
            captureReference = reference2;
        }

        StructuredEnginePlanes[engineBase + pixelIndex] = plane0;
        StructuredEnginePlanes[engineBase + StructuredPixelCount + pixelIndex] = plane1;
        StructuredEnginePlanes[engineBase + (2u * StructuredPixelCount) + pixelIndex] =
            ((controlAlpha & Contract::kControlFlagMask) << Contract::kControlFlagShift)
            | ((evb & 0xFFu) << Contract::kControlEvbShift)
            | ((eva & 0xFFu) << Contract::kControlEvaShift);
        StructuredEnginePlanes[engineBase + (3u * StructuredPixelCount) + pixelIndex] =
            captureReference;
    }
    void PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine);
    void StoreStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        const u16* captureOutput);
    [[nodiscard]] u32 GetStructuredCaptureReference(
        u32 engine,
        u32 flatByteAddress,
        u16 nativeColor,
        bool object = false) const;
    void RecordStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        u32 captureCnt,
        const u16* sourceB,
        u32 sourceBBank,
        u32 sourceBAddress);
    [[nodiscard]] bool SnapshotStructuredVramDisplayLine(
        u32 screen, u32 outputLine, u32 sourceLine);
    void BuildStructuredScreenLine(
        u32 engine,
        u32 screen,
        u32 line,
        const u32* output,
        bool forcePlain = false,
        bool preserveVramSnapshot = false);
    void InvalidateStructuredCaptureBlocks(u32 bank, u32 start, u32 len);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
