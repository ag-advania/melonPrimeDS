// MelonPrimeDS - experimental Metal renderer (Metal-plan Phase 8)

#ifndef GPU_METAL_H
#define GPU_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

// MELONPRIME_METAL_HIRES_VISIBLE_OUTPUT_V1

#include "GPU_Soft.h"

#include <array>
#include <memory>

namespace melonDS
{

class MetalRenderer2D;

class MetalRenderer : public SoftRenderer
{
public:
    explicit MetalRenderer(melonDS::NDS& nds, bool useComputeRenderer = false) noexcept;
    ~MetalRenderer() override;

    bool Init() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void Start3DRendering() override;
    void Finish3DRendering() override;
    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override;
    void VBlankEnd() override;
    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        bool complete) override;
    void SwapBuffers() override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

private:
    struct MetalOutputState;
    struct MetalFullGpuState;
    struct MetalCaptureState;

    std::unique_ptr<MetalRenderer2D> Metal2D_A;
    std::unique_ptr<MetalRenderer2D> Metal2D_B;
    int ScaleFactor = 1;
    bool ComputeRendererSelected = false;
    u16 FrameMasterBrightnessA = 0;
    u16 FrameMasterBrightnessB = 0;
    // MELONPRIME_METAL_HIRES_SCALE_AUTHORITY_V2
    // MELONPRIME_METAL_OUTPUT_LEASE_V1
    // MELONPRIME_METAL_MASTER_BRIGHTNESS_V1
    std::shared_ptr<MetalOutputState> OutputState;
    std::unique_ptr<MetalFullGpuState> FullGpuState;
    std::unique_ptr<MetalCaptureState> CaptureState;
    // MELONPRIME_METAL_GPU_RESIDENT_2D_V1
    // MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1

    void ConfigureMetal2DMirror(void* preferredDevice);
    bool ConfigureMetalVisibleOutput(void* preferredDevice);
    bool InitializeMetalFullGpuOutput();
    bool IsMetalFullGpuFrameEligible() const;
    bool ComposeMetalFullGpuOutput();

    bool ConfigureMetalCaptureState(void* preferredDevice);
    void BeginMetalCaptureFrame();
    void CaptureMetalDisplayCaptureLine(u32 line);
    bool EncodeMetalDisplayCapture(
        void* engineA2DTexture,
        void* high3DTexture);
    bool UploadCpuCompletedCaptures();
    [[nodiscard]] bool MetalCaptureReady() const;
    [[nodiscard]] bool MetalCaptureFrameHadCapture() const;
    [[nodiscard]] bool MetalCaptureFrameSupported() const;
    [[nodiscard]] bool MetalCaptureResourcesCoherent() const;
    [[nodiscard]] void* GetMetalCapture128Texture() const;
    [[nodiscard]] void* GetMetalCapture256Texture() const;

    void CaptureMetalVisible3DFrame();
    void ComposeMetalVisibleOutput();
    // MELONPRIME_METAL_FRAME_SNAPSHOT_V1
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_H
