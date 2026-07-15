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
    Renderer.ClearStructuredVulkan2DState();
    UnitA.Reset();
    UnitB.Reset();
}

bool SapphireGpu2DState::IsActiveForRendering(const GPU& gpu) const noexcept
{
    return gpu.GPU3D.HasCurrentRenderer()
        && gpu.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();
}

} // namespace melonDS

#endif
