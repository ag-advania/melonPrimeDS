// MelonPrimeDS - Metal renderer with native 3D GetLine integration

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "GPU2D_Metal.h"
#include "GPU3D_Metal.h"
#include "GPU3D_MetalCompute.h"
#include "NDS.h"

namespace melonDS
{

namespace
{

bool MetalComputeFoundationEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("MELONPRIME_METAL_COMPUTE_FOUNDATION");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void* Metal3DColorTarget(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetColorTargetTexture();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetColorTargetTexture();
    return nullptr;
}

void* Metal3DPreferredDevice(Renderer3D* renderer) noexcept
{
    id<MTLTexture> texture = (__bridge id<MTLTexture>)Metal3DColorTarget(renderer);
    return texture ? (__bridge void*)texture.device : nullptr;
}

bool Metal3DIsThreaded(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->IsThreaded();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->IsThreaded();
    return false;
}

void Metal3DSetupRenderThread(Renderer3D* renderer)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        compute->SetupRenderThread();
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        raster->SetupRenderThread();
}

void Metal3DEnableRenderThread(Renderer3D* renderer)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        compute->EnableRenderThread();
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        raster->EnableRenderThread();
}

void ConfigureMetal3DRenderer(
    Renderer3D* renderer,
    bool threaded,
    int scale,
    bool betterPolygons)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
    {
        compute->SetThreaded(threaded);
        compute->SetScaleFactor(scale);
        compute->SetBetterPolygons(betterPolygons);
    }
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
    {
        raster->SetThreaded(threaded);
        raster->SetScaleFactor(scale);
        raster->SetBetterPolygons(betterPolygons);
    }
}

} // namespace

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
    Metal2D_A = std::make_unique<MetalRenderer2D>(GPU.GPU2D_A);
    Metal2D_B = std::make_unique<MetalRenderer2D>(GPU.GPU2D_B);

    if (MetalComputeFoundationEnabled())
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: selected developer foundation mode\n");
        Rend3D = std::make_unique<MetalComputeRenderer3D>(GPU.GPU3D, *this);
    }
    else
    {
        Rend3D = std::make_unique<MetalRenderer3D>(GPU.GPU3D, *this);
    }
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: initializing native Metal 3D GetLine integration path\n");
    if (!Rend3D->Init())
        return false;

    ConfigureMetal2DMirror(Metal3DPreferredDevice(Rend3D.get()));
    return true;
}

void MetalRenderer::PreSavestate()
{
    if (Metal3DIsThreaded(Rend3D.get()))
        Metal3DSetupRenderThread(Rend3D.get());
}

void MetalRenderer::PostSavestate()
{
    if (Metal3DIsThreaded(Rend3D.get()))
        Metal3DEnableRenderThread(Rend3D.get());
}

void MetalRenderer::SetRenderSettings(RendererSettings& settings)
{
    const int scale = std::max(1, settings.ScaleFactor);
    ScaleFactor = scale;

    ConfigureMetal3DRenderer(
        Rend3D.get(), settings.Threaded, scale, settings.BetterPolygons);
    ConfigureMetal2DMirror(Metal3DPreferredDevice(Rend3D.get()));
}

void MetalRenderer::ConfigureMetal2DMirror(void* preferredDevice)
{
    bool okA = !Metal2D_A || Metal2D_A->Configure(preferredDevice, ScaleFactor);
    bool okB = !Metal2D_B || Metal2D_B->Configure(preferredDevice, ScaleFactor);
    if (!okA || !okB)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: scaffold allocation failed; keeping Phase 2/3 CPU-composited output visible\n");
    }
}

void MetalRenderer::VBlank()
{
    SoftRenderer::VBlank();
}

RendererOutput MetalRenderer::GetOutput()
{
    return SoftRenderer::GetOutput();
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
