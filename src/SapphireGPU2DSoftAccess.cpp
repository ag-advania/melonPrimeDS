#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "SapphireGPU2DSoftAccess.h"

#include "SapphireGPU2DCore/GPU2D_Soft.h"

namespace melonDS::SapphireGPU2D
{

SoftRenderer::SoftRenderer(SapphireGPU2DCore::GPU2D::SoftRenderer& owner) noexcept
    : Owner(owner)
{
}

const SoftRenderer::DebugCaptureStats& SoftRenderer::GetDebugCaptureStats() const noexcept
{
    static_assert(sizeof(DebugCaptureStats) == sizeof(SapphireGPU2DCore::GPU2D::SoftRenderer::DebugCaptureStats));
    return reinterpret_cast<const DebugCaptureStats&>(Owner.GetDebugCaptureStats());
}

const u32* SoftRenderer::GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept
{
    return Owner.GetStructuredVulkan2DPlane(topScreen, plane);
}

void SoftRenderer::ClearStructuredVulkan2DState() noexcept
{
    Owner.ClearStructuredVulkan2DState();
}

const u32* SoftRenderer::GetDebugCapture3dSource() const noexcept
{
    return Owner.GetDebugCapture3dSource();
}

const std::array<u8, 192>& SoftRenderer::GetDebugCaptureLineUses3dMask() const noexcept
{
    return Owner.GetDebugCaptureLineUses3dMask();
}

} // namespace melonDS::SapphireGPU2D

#endif
