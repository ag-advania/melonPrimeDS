// MelonPrimeDS - experimental Metal renderer (Metal-plan Phase 8)

#if defined(MELONPRIME_ENABLE_METAL)

#include "GPU_Metal.h"

#include <cstdio>

#include "GPU3D_Metal.h"
#include "NDS.h"

namespace melonDS
{

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
    Rend3D = std::make_unique<MetalRenderer3D>(GPU.GPU3D, *this);
}

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: initializing Metal 3D scaffold with software raster delegate\n");
    return Rend3D->Init();
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

    rend3d->SetThreaded(settings.Threaded);
    rend3d->SetScaleFactor(settings.ScaleFactor);
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
