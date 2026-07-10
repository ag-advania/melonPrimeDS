// MelonPrimeDS - Metal renderer with native 3D GetLine integration

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <cstdio>

#include "GPU2D_Metal.h"
#include "GPU3D_Metal.h"
#include "NDS.h"

namespace melonDS
{

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
    Metal2D_A = std::make_unique<MetalRenderer2D>(GPU.GPU2D_A);
    Metal2D_B = std::make_unique<MetalRenderer2D>(GPU.GPU2D_B);
    Rend3D = std::make_unique<MetalRenderer3D>(GPU.GPU3D, *this);
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: initializing native Metal 3D GetLine integration path\n");
    if (!Rend3D->Init())
        return false;

    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    void* preferredDevice = nullptr;
    if (rend3d)
    {
        id<MTLTexture> native3D = (__bridge id<MTLTexture>)rend3d->GetColorTargetTexture();
        preferredDevice = native3D ? (__bridge void*)native3D.device : nullptr;
    }
    ConfigureMetal2DMirror(preferredDevice);
    return true;
}

void MetalRenderer::PreSavestate()
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (rend3d && rend3d->IsThreaded())
        rend3d->SetupRenderThread();
}

void MetalRenderer::PostSavestate()
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (rend3d && rend3d->IsThreaded())
        rend3d->EnableRenderThread();
}

void MetalRenderer::SetRenderSettings(RendererSettings& settings)
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (!rend3d)
        return;

    const int scale = std::max(1, settings.ScaleFactor);
    ScaleFactor = scale;

    rend3d->SetThreaded(settings.Threaded);
    rend3d->SetScaleFactor(scale);
    rend3d->SetBetterPolygons(settings.BetterPolygons);

    id<MTLTexture> native3D = (__bridge id<MTLTexture>)rend3d->GetColorTargetTexture();
    void* preferredDevice = native3D ? (__bridge void*)native3D.device : nullptr;
    ConfigureMetal2DMirror(preferredDevice);
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
