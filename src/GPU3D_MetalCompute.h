// MelonPrimeDS - Metal compute renderer texture-variant contract stage and raster-reference bridge

#ifndef GPU3D_METAL_COMPUTE_H
#define GPU3D_METAL_COMPUTE_H

// MELONPRIME_METAL_COMPUTE_TEXTURE_VARIANTS_V6
// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1
// MELONPRIME_METAL_COMPUTE_COMPLETE_DEPTH_BLEND_V1
// MELONPRIME_METAL_COMPUTE_FINAL_PASS_V1
// MELONPRIME_METAL_COMPUTE_VISIBLE_CUTOVER_V1
// MELONPRIME_METAL_COMPUTE_LEGACY_SUMMARY_RETIREMENT_V1

#if defined(MELONPRIME_ENABLE_METAL)

#include <cstdint>
#include <memory>

#include "GPU3D_Metal.h"

namespace melonDS
{

// Phase 7F+ (MELONPRIME_METAL_COMPUTE_VISIBLE_CUTOVER_V1). The renderer
// executes real-frame span preparation, Metal InterpSpans/BinCombined, work
// sorting, texture/depth/blend, and the final pass, writing the visible
// output into GetComputeFinalTexture(). RasterReference (the validated Metal
// raster renderer) is kept only as a per-frame fallback for when compute
// output is not ready/valid (see RenderFrame()), not as the normal visible
// source -- do not read comments elsewhere as implying otherwise.
class MetalComputeRenderer3D final : public Renderer3D
{
public:
    MetalComputeRenderer3D(melonDS::GPU3D& gpu3D, MetalRendererHost& parent) noexcept;
    ~MetalComputeRenderer3D() override;

    bool Init() override;
    void Reset() override;

    void SetThreaded(bool threaded) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept;
    void SetScaleFactor(int scale) noexcept;
    void SetHighResolutionCoordinates(bool enabled) noexcept;
    void SetBetterPolygons(bool betterPolygons) noexcept;
    void SetCpuReadbackRequired(bool required) noexcept;
    void SetCaptureTextures(
        void* capture128Texture,
        void* capture256Texture) noexcept;
    // MELONPRIME_METAL_GPU_RESIDENT_2D_V1

    void RenderFrame() override;
    void FinishRendering() override;
    void RestartFrame() override;
    u32* GetLine(int line) override;

    void* GetColorTargetTexture() const noexcept;
    void* GetNativeResolveTexture() const noexcept;
    void* GetCommandQueue() const noexcept;
    int GetTargetWidth() const noexcept;
    int GetTargetHeight() const noexcept;
    int GetScaleFactor() const noexcept;
    [[nodiscard]] bool LastFrameUsesHighResolution3D() const noexcept;
    [[nodiscard]] uint32_t GetLastFrameEngineALayer() const noexcept;
    [[nodiscard]] int GetLastFrameRenderedScale() const noexcept;
    // MELONPRIME_METAL_COMPUTE_HIRES_LATCH_V1
    Metal3DDiagnostics GetLastDiagnostics() const noexcept;

    void SetupRenderThread();
    void EnableRenderThread();

    [[nodiscard]] bool FoundationReady() const noexcept;
    void* GetComputeFinalTexture() const noexcept;
    [[nodiscard]] uint64_t GetComputeFinalSerial() const noexcept;
    [[nodiscard]] bool ComputeFinalReady() const noexcept;

private:
    struct MetalComputeState;

    MetalRenderer3D RasterReference;
    std::unique_ptr<MetalComputeState> State;

    bool CreateComputeFoundation();
    bool RunFoundationSelfTest();
    bool ConfigureSpanBinResources(int scale);
    bool RunSpanBinSelfTest();
    bool RunTextureVariantTileSelfTest();
    bool RunCompleteDepthBlendSelfTest();
    bool RunFinalPassSelfTest();
    bool SubmitRealFrameSpanBin();
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_METAL_COMPUTE_H
