// MelonPrimeDS - Metal compute renderer foundation and raster-reference bridge

#ifndef GPU3D_METAL_COMPUTE_H
#define GPU3D_METAL_COMPUTE_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <memory>

#include "GPU3D_Metal.h"

namespace melonDS
{

class SoftRenderer;

// Phase 7A foundation. The class already occupies the final Renderer3D slot and
// owns the future compute resources, while visible rendering remains delegated
// to MetalRenderer3D until the Interp/Bin/Rasterise/DepthBlend/FinalPass kernels
// are ported and validated. Selection is developer-only through
// MELONPRIME_METAL_COMPUTE_FOUNDATION=1.
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
    void SetBetterPolygons(bool betterPolygons) noexcept;

    void RenderFrame() override;
    void FinishRendering() override;
    void RestartFrame() override;
    u32* GetLine(int line) override;

    void* GetColorTargetTexture() const noexcept;
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
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_METAL_COMPUTE_H
