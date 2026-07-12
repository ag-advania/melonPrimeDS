// MelonPrimeDS - experimental Metal 2D renderer scaffold (Metal-plan Phase 4)

#ifndef GPU2D_METAL_H
#define GPU2D_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <memory>

#include "GPU2D.h"

namespace melonDS
{

class MetalRenderer2D final : public Renderer2D
{
public:
    explicit MetalRenderer2D(melonDS::GPU2D& gpu2D) noexcept;
    ~MetalRenderer2D();

    bool Init() override { return true; }
    void Reset() noexcept override;
    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override {}
    void VBlankEnd() override {}

    bool Configure(void* preferredDevice, void* preferredQueue, int scale) noexcept;
    void CaptureScanlineState(int line) noexcept;
    void CaptureSpriteScanlineState(int line) noexcept;
    [[nodiscard]] bool SegmentedSnapshotReady() const noexcept;
    [[nodiscard]] bool SegmentedFrameComplete() const noexcept;
    [[nodiscard]] bool SegmentedRenderReady() const noexcept;
    bool RenderSegmentedGpuFrame(
        void* high3DTexture,
        void* capture128Texture,
        void* capture256Texture,
        bool allowCaptureTextures) noexcept;
    // MELONPRIME_METAL_2D_SCANLINE_SNAPSHOT_V1
    // MELONPRIME_METAL_2D_SHADOW_PATH_REMOVAL_V1
    // MELONPRIME_METAL_2D_LEGACY_FULL_FRAME_REMOVAL_V1
    [[nodiscard]] bool FullGpuReady() const noexcept;
    // MELONPRIME_METAL_GPU_RESIDENT_2D_V1
    // MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
    bool UploadRawVRAMInputs() noexcept;
    bool UploadPaletteInputs() noexcept;
    bool RefreshLayerConfig() noexcept;
    bool RefreshSpriteConfig(int ystart, int yend) noexcept;
    bool RefreshScanlineConfig(int line) noexcept;
    bool RefreshCompositorConfig() noexcept;

    [[nodiscard]] void* GetOutputTexture() const noexcept;
    [[nodiscard]] void* GetOBJLayerTexture() const noexcept;
    [[nodiscard]] void* GetOBJDepthTexture() const noexcept;
    [[nodiscard]] void* GetBGLayerTexture(int index) const noexcept;
    [[nodiscard]] void* GetBGVRAMTexture() const noexcept;
    [[nodiscard]] void* GetOBJVRAMTexture() const noexcept;
    [[nodiscard]] void* GetBGPaletteTexture() const noexcept;
    [[nodiscard]] void* GetOBJPaletteTexture() const noexcept;
    [[nodiscard]] void* GetMosaicTexture() const noexcept;
    [[nodiscard]] void* GetSpriteTexture() const noexcept;
    [[nodiscard]] int GetScaleFactor() const noexcept;
    [[nodiscard]] int GetTargetWidth() const noexcept;
    [[nodiscard]] int GetTargetHeight() const noexcept;

private:
    struct Metal2DState;

    bool BuildLayerPipeline() noexcept;
    bool BuildFullGpuPipelines() noexcept;
    bool PrerenderConfiguredLayers() noexcept;
    void BeginSegmentSnapshotFrameIfNeeded(int line) noexcept;
    void MaybeReportSegmentSnapshotFrame() noexcept;

    std::unique_ptr<Metal2DState> State;
    int ScaleFactor = 1;
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU2D_METAL_H
