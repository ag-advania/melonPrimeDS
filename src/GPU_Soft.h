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

#if defined(MELONPRIME_DS) \
    && (defined(MELONPRIME_ENABLE_VULKAN) \
        || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
#define MELONPRIME_HAS_STRUCTURED_SOFT_2D 1
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

    bool GetFramebuffers(void** top, void** bottom) override;

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    struct StructuredVulkanFrameView
    {
        const u32* Plane[2][3]{};
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
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredEnginePlanes{};
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredScreenPlanes{};
    std::array<u32, 2u * 192u> StructuredScreenLineMeta{};
    std::array<u32, 4u * 3u * StructuredCapturePixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * StructuredCaptureLineCount> StructuredCaptureLineValid{};
    std::array<u8, 4u * StructuredCaptureLineCount> StructuredCaptureLineUses3D{};
    std::array<u8, 2u * 192u> StructuredEngineLineUsesCapture3D{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;
    bool StructuredFrameNativeMenuHeld = false;
    bool NativeMenuHeldForFrame = false;
    u64 StructuredFrameGeneration = 0;

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
        const u16* captureOutput);
    [[nodiscard]] bool DrawStructuredCapturePixel(
        u32 engine,
        u32 line,
        u32* destination,
        u32 flatByteAddress);
    void BuildStructuredScreenLine(u32 engine, u32 screen, u32 line, const u32* output, bool forcePlain = false);
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
