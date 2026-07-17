// MelonPrimeDS - experimental Metal 3D renderer scaffold (Metal-plan Phase 8)

#ifndef GPU3D_METAL_H
#define GPU3D_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <cstdint>
#include <memory>

#include "GPU3D.h"
#include "GPU3D_Soft.h"
#include "GPU_MetalHost.h"

namespace melonDS
{

struct Metal3DDiagnostics
{
    uint64_t NonzeroPixels = 0;
    uint64_t Checksum = 0;
    int FirstNonzeroX = -1;
    int FirstNonzeroY = -1;
    uint8_t FirstNonzeroBGRA[4] = {};
    uint32_t ConsideredPolygons = 0;
    uint32_t TexturedPolygons = 0;
    uint32_t Groups = 0;
    uint32_t Draws = 0;
};

class MetalRenderer3D final : public Renderer3D
{
public:
    MetalRenderer3D(melonDS::GPU3D& gpu3D, MetalRendererHost& parent) noexcept;
    ~MetalRenderer3D() override;

    bool Init() override;
    void Reset() override;

    void SetThreaded(bool threaded) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept;
    void SetScaleFactor(int scale) noexcept;
    [[nodiscard]] bool ForceScaleFactor(int scale) noexcept;
    void SetHighResolutionCoordinates(bool enabled) noexcept;
    void SetBetterPolygons(bool betterPolygons) noexcept;
    void SetCpuReadbackRequired(bool required) noexcept;
    // MELONPRIME_METAL_GPU_RESIDENT_2D_V1

    [[nodiscard]] bool LastFrameUsesHighResolution3D() const noexcept;
    [[nodiscard]] uint32_t GetLastFrameEngineALayer() const noexcept;
    [[nodiscard]] int GetLastFrameRenderedScale() const noexcept;
    // MELONPRIME_METAL_COMPUTE_HIRES_LATCH_V1

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

private:
    struct MetalState;

    SoftRenderer3D Delegate;
    std::unique_ptr<MetalState> State;
    int ScaleFactor = 1;
    bool HiresCoordinates = false;
    bool BetterPolygons = false;
    bool CpuReadbackRequired = true;
    // MELONPRIME_METAL_RENDER_OPTIONS_V1

    bool CreateDeviceObjects();
    bool BuildClearPipeline();
    bool BuildOpaqueRenderPipelines();
    bool BuildFinalPassPipelines();
    bool ResizeTargets();
    bool ClearNativeTarget();
    bool CreateClearBitmapTextures();
    bool UpdateClearBitmapTextures(u8 clrBitmapDirty);
    bool RenderFinalPostPass();
    bool DrawSolidNative3DDiagnostic();
    bool ReadbackNativeColorTargetToLineBuffer();

    // Phase 8 "port order" steps 2-4 (melonprime-metal-backend-plan.md,
    // design doc S14): uploads GPU3D::RenderPolygonRAM into native Metal
    // buffers, resolves textures via the shared Texcache<> template (same
    // decode logic GLRenderer3D uses, GPU3D_Texcache.h), and rasterizes
    // visible polygons into ColorTarget/AttrTarget/DepthStencilTarget every
    // frame. This is a real, GPU-executed draw -- not a placeholder -- and
    // its native ColorTarget is read back through GetLine(), but it is not yet
    // a complete GLRenderer3D mirror. Exact visual parity, hires composition,
    // and later full-GPU composition work remain tracked in
    // GPU3D_Metal.mm and the Metal backend plan.
    void RenderNativeOpaquePolygons();
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_METAL_H
