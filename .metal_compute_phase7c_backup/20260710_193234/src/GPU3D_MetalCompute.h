// MelonPrimeDS - Metal compute renderer span/bin foundation and raster-reference bridge

#ifndef GPU3D_METAL_COMPUTE_H
#define GPU3D_METAL_COMPUTE_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <memory>

#include "GPU3D_Metal.h"

namespace melonDS
{

class SoftRenderer;

// Phase 7B. The renderer owns the final Renderer3D slot and now executes the
// real-frame CPU span preparation plus Metal InterpSpans/BinCombined mirror.
// Visible output remains the validated MetalRenderer3D reference until the
// Rasterise/DepthBlend/FinalPass kernels are complete and pixel-diff validated.
class MetalComputeRenderer3D final : public Renderer3D
{
public:
    MetalComputeRenderer3D(melonDS::GPU3D& gpu3D, SoftRenderer& parent) noexcept;
    ~MetalComputeRenderer3D() override;

    bool Init() override;
    void Reset() override;

    void SetThreaded(bool threaded) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept;
    void SetScaleFactor(int scale) noexcept;
    void SetHighResolutionCoordinates(bool enabled) noexcept;
    void SetBetterPolygons(bool betterPolygons) noexcept;

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
    Metal3DDiagnostics GetLastDiagnostics() const noexcept;

    void SetupRenderThread();
    void EnableRenderThread();

    [[nodiscard]] bool FoundationReady() const noexcept;

private:
    struct MetalComputeState;

    MetalRenderer3D RasterReference;
    std::unique_ptr<MetalComputeState> State;

    bool CreateComputeFoundation();
    bool RunFoundationSelfTest();
    bool ConfigureSpanBinResources(int scale);
    bool RunSpanBinSelfTest();
    bool SubmitRealFrameSpanBin();
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_METAL_COMPUTE_H
