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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include "SapphireGPU2DSoftAccess.h"
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

    void AllocCapture(u32 bank, u32 start, u32 len) override {};
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};

    bool GetFramebuffers(void** top, void** bottom) override;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    static constexpr size_t kStructuredScreenWidth = 256u;
    static constexpr size_t kStructuredScreenHeight = 192u;
    static constexpr size_t kStructuredPixelCount = kStructuredScreenWidth * kStructuredScreenHeight;
    static constexpr size_t kStructuredPlaneCount = 3u;
    static constexpr size_t kStructuredScreenCount = 2u;
    static constexpr size_t kPackedStride = kStructuredScreenWidth * 3u + 1u;
    static constexpr size_t kPackedFramebufferPixels = kPackedStride * kStructuredScreenHeight;

    [[nodiscard]] const SapphireGPU2D::SoftRenderer::DebugCaptureStats& GetSapphireDebugCaptureStats() const noexcept
    {
        return SapphireDebugCaptureStats;
    }

    [[nodiscard]] const u32* GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept;
    [[nodiscard]] const u32* GetSapphireDebugCapture3dSource() const noexcept;
    [[nodiscard]] const std::array<u8, kStructuredScreenHeight>& GetSapphireCaptureLineUses3dMask() const noexcept;
    void ClearStructuredVulkan2DState() noexcept;
    void SyncSapphireFramebufferBindings() noexcept;
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    alignas(64) u32 StructuredPlane0[2][256];
    alignas(64) u32 StructuredPlane1[2][256];
    alignas(64) u32 StructuredControl[2][256];
    alignas(64) u32 StructuredNativeFinal[2][256];
    alignas(64) std::array<u32, kStructuredScreenCount * kStructuredPlaneCount * kStructuredPixelCount>
        StructuredVulkan2DPlanes{};

    bool StructuredVulkan2DCurrentLineTargetsTop = false;
    u32 StructuredVulkan2DCurrentEngine = 0;
    bool CurrentLineRegularCaptureUses3d = false;
    std::array<u32, kStructuredPlaneCount * kStructuredScreenWidth> StructuredVulkan2DCaptureSourceLine{};
    bool StructuredVulkan2DCaptureSourceLineValid = false;
    u32 StructuredVulkan2DCaptureSourceLineY = 0;
    alignas(64) std::array<u32, 4 * kStructuredPlaneCount * kStructuredPixelCount>
        StructuredVulkan2DCapturePlanes{};
    std::array<u8, 4 * kStructuredScreenHeight> StructuredVulkan2DCaptureLineValid{};

    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    void BeginStructuredVulkan2DLine(u32 engine, u32 line) noexcept;
    [[nodiscard]] bool CurrentUnitTargetsTopScreen() const noexcept;
    [[nodiscard]] GPU2D& CaptureUnit() noexcept;
    [[nodiscard]] const GPU2D& CaptureUnit() const noexcept;
    [[nodiscard]] u32* GetCaptureBGOBJLine() noexcept;

    void ClearStructuredVulkan2DLine(u32 line);
    void ClearStructuredVulkan2DCapture(u32 vramBank);
    void ClearStructuredVulkan2DCaptureRange(u32 vramBank, u32 dstAddress, u32 width);
    void SaveStructuredVulkan2DCaptureSourceLine(u32 line);
    void CopyStructuredVulkan2DCaptureSourceLineToCapture(
        u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCurrentLineToCapture(
        u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCaptureLineToCurrentScreen(u32 line, u32 vramBank);
    [[nodiscard]] bool ReadStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank, u32 vramAddress, u32& overlayPixel, u32& overlayControlAlpha) const noexcept;
    void MergeStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank, u32 vramAddress, u32 overlayPixel, u32 overlayControlAlpha);
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
    void DoCaptureStructured(u32 line, u32 width, u32 sourceLine);

    void WriteAcceleratedPackedRow(
        u32* dstRow,
        u32 engine,
        u32 line,
        u16 masterBrightness,
        u32 dispCnt,
        bool forcedBlank,
        bool engineEnabled);
    size_t ScreenIndexForEngine(u32 engine) const noexcept;

    SapphireGPU2D::SoftRenderer::DebugCaptureStats SapphireDebugCaptureStats{};
    bool HasLastDebugCapture3dSource = false;
    alignas(8) u32 LastDebugCapture3dSource[kStructuredPixelCount]{};
    std::array<u8, kStructuredScreenHeight> CaptureLineUses3d{};
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
