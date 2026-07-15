#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireGpu2DState requires the Vulkan build gate"
#endif

#include "SapphireGPU2DCore/GPU2D_Soft.h"

namespace melonDS
{
class GPU;

class SapphireGpu2DState
{
public:
    explicit SapphireGpu2DState(GPU& gpu);
    void Reset();

    void Activate(u64 rendererGeneration) noexcept;
    void Deactivate() noexcept;

    [[nodiscard]] bool IsActiveForRendering(const GPU& gpu) const noexcept;
    [[nodiscard]] bool IsRenderingActive() const noexcept { return SapphireRenderingActive; }
    [[nodiscard]] u64 ActiveRendererGeneration() const noexcept
    {
        return ActiveSapphireRendererGeneration;
    }

private:
    bool SapphireRenderingActive = false;
    u64 ActiveSapphireRendererGeneration = 0;

public:

    SapphireGPU2DCore::GPU2D::Unit UnitA;
    SapphireGPU2DCore::GPU2D::Unit UnitB;
    SapphireGPU2DCore::GPU2D::SoftRenderer Renderer;
};

} // namespace melonDS
