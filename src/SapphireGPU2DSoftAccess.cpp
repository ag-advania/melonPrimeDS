#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "SapphireGPU2DSoftAccess.h"

#include "GPU_Soft.h"

namespace melonDS::SapphireGPU2D
{

SoftRenderer::SoftRenderer(melonDS::GPU2D& gpu2D, melonDS::SoftRenderer& owner)
    : Renderer2D(gpu2D)
    , Owner(owner)
{
}

void SoftRenderer::DrawScanline(u32 line)
{
    if (GPU2D.Num == 0)
        Owner.DrawScanline(line);
}

void SoftRenderer::DrawSprites(u32 line)
{
    if (GPU2D.Num == 0)
        Owner.DrawSprites(line);
}

const SoftRenderer::DebugCaptureStats& SoftRenderer::GetDebugCaptureStats() const noexcept
{
    return Owner.GetSapphireDebugCaptureStats();
}

const u32* SoftRenderer::GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept
{
    return Owner.GetStructuredVulkan2DPlane(topScreen, plane);
}

void SoftRenderer::ClearStructuredVulkan2DState() noexcept
{
    Owner.ClearStructuredVulkan2DState();
}

} // namespace melonDS::SapphireGPU2D

#endif
