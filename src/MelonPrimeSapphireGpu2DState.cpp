#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeSapphireGpu2DState.h"

#include "GPU.h"
#include "SapphireGPU2DCore/GPU2D_Soft.h"

namespace melonDS
{

void SapphireGpu2DState::Reset(GPU& gpu)
{
    Deactivate();
    if (auto* renderer = gpu.TryGetGpu2DSoftRenderer())
        renderer->ClearStructuredVulkan2DState();
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
