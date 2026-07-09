// MelonPrimeDS - experimental Metal 3D renderer scaffold (Metal-plan Phase 8)

#ifndef GPU3D_METAL_H
#define GPU3D_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <memory>

#include "GPU3D.h"
#include "GPU3D_Soft.h"

namespace melonDS
{

class SoftRenderer;

class MetalRenderer3D final : public Renderer3D
{
public:
    MetalRenderer3D(melonDS::GPU3D& gpu3D, SoftRenderer& parent) noexcept;
    ~MetalRenderer3D() override;

    bool Init() override;
    void Reset() override;

    void SetThreaded(bool threaded) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept;
    void SetScaleFactor(int scale) noexcept;

    void RenderFrame() override;
    void FinishRendering() override;
    void RestartFrame() override;

    u32* GetLine(int line) override;

    void SetupRenderThread();
    void EnableRenderThread();

private:
    struct MetalState;

    SoftRenderer3D Delegate;
    std::unique_ptr<MetalState> State;
    int ScaleFactor = 1;

    bool CreateDeviceObjects();
    bool BuildClearPipeline();
    bool BuildOpaqueRenderPipelines();
    bool ResizeTargets();
    bool ClearNativeTarget();

    // Phase 8 "port order" step 2/3 (melonprime-metal-backend-plan.md,
    // design doc S14): uploads GPU3D::RenderPolygonRAM into native Metal
    // buffers and rasterizes opaque, untextured (vertex-color only)
    // polygons into ColorTarget/AttrTarget/DepthStencilTarget every frame.
    // This is a real, GPU-executed draw -- not a placeholder -- but its
    // output is not yet wired to GetLine()/display (still Delegate-backed),
    // and it does not yet implement texturing, translucency, shadow masks,
    // edge marking, or fog. See GPU3D_Metal.mm for the itemized scope.
    void RenderNativeOpaquePolygons();
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_METAL_H
