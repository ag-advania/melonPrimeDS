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
    struct StructuredVulkanFrameView
    {
        const u32* Plane[2][3]{};
        const u32* LineMeta[2]{};
        const u32* Capture3DSource = nullptr;
        const u8* CaptureLineUses3D = nullptr;
        bool HasCapture3DSource = false;
        bool CaptureScreenSwap = false;
        bool Valid = false;
    };

    [[nodiscard]] bool GetStructuredVulkanFrame(StructuredVulkanFrameView& view) const noexcept;
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    static constexpr std::size_t StructuredPixelCount = 256u * 192u;
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredEnginePlanes{};
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredScreenPlanes{};
    std::array<u32, 2u * 192u> StructuredScreenLineMeta{};
    std::array<u32, 4u * 3u * StructuredPixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * 192u> StructuredCaptureLineValid{};
    std::array<u8, 4u * 192u> StructuredCaptureLineUses3D{};
    std::array<u8, 2u * 192u> StructuredEngineLineUsesCapture3D{};
    std::array<u32, StructuredPixelCount> StructuredCapture3DSource{};
    std::array<u8, 192u> StructuredCapture3DSourceLineValid{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCapture3DSourceValid = false;
    bool StructuredCaptureScreenSwap = false;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;

    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    void StoreStructuredEnginePixel(
        u32 engine,
        u32 line,
        u32 x,
        u32 val1,
        u32 val2,
        u32 composed,
        u32 compositionMode,
        u32 eva,
        u32 evb);
    void PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine);
    void StoreStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        u32 sourceBAddress,
        u32 sourceBBank,
        bool sourceBFromVram,
        const u16* captureOutput);
    [[nodiscard]] bool DrawStructuredCapturePixel(u32 engine, u32* destination, u32 flatByteAddress);
    void BuildStructuredScreenLine(u32 engine, u32 screen, u32 line, const u32* output, bool forcePlain = false);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
