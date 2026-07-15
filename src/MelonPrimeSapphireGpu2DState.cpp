#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeSapphireGpu2DState.h"

#include "GPU.h"

namespace melonDS
{

SapphireGpu2DState::SapphireGpu2DState(GPU& gpu)
    : UnitA(0, gpu)
    , UnitB(1, gpu)
    , Renderer(gpu)
{
}

void SapphireGpu2DState::Reset()
{
    Deactivate();
    Renderer.ClearStructuredVulkan2DState();
    UnitA.Reset();
    UnitB.Reset();
}

void SapphireGpu2DState::Activate(u64 rendererGeneration) noexcept
{
    ActiveSapphireRendererGeneration = rendererGeneration;
    SapphireRenderingActive = rendererGeneration != 0;
}

void SapphireGpu2DState::Deactivate() noexcept
{
    SapphireRenderingActive = false;
    ActiveSapphireRendererGeneration = 0;
}

bool SapphireGpu2DState::IsActiveForRendering(const GPU& gpu) const noexcept
{
    return SapphireRenderingActive
        && gpu.GPU3D.HasCurrentRenderer()
        && ActiveSapphireRendererGeneration == gpu.GPU3D.GetCurrentRendererGeneration()
        && gpu.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();
}

} // namespace melonDS

#endif
