/*
    Sapphire 0.7.0.rc4 GPU2D::SoftRenderer API facade for Vulkan latch parity.
    Source: SapphireRhodonite/melonDS-android-lib @ d77944275fa61f9b79cfcead2c3e98993429a023
*/

#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>

#include "GPU2D.h"

namespace melonDS
{
class SoftRenderer;
}

namespace melonDS::SapphireGPU2D
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
        u32 CaptureBacked3DBestClassCounts[17]{};
        u32 CompModeCounts[8]{};
    };

    explicit SoftRenderer(melonDS::GPU2D& gpu2D, melonDS::SoftRenderer& owner);

    bool Init() override { return true; }
    void Reset() override {}
    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override {}
    void VBlankEnd() override {}

    [[nodiscard]] const DebugCaptureStats& GetDebugCaptureStats() const noexcept;
    [[nodiscard]] const u32* GetStructuredVulkan2DPlane(
        bool topScreen, u32 plane) const noexcept;
    void ClearStructuredVulkan2DState() noexcept;

    [[nodiscard]] const u32* GetDebugCapture3dSource() const noexcept;
    [[nodiscard]] const std::array<u8, 192>& GetDebugCaptureLineUses3dMask() const noexcept;

private:
    melonDS::SoftRenderer& Owner;
};

} // namespace melonDS::SapphireGPU2D

#endif
