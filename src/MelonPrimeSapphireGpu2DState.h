#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireGpu2DState requires the Vulkan build gate"
#endif

namespace melonDS
{
class GPU;

class SapphireGpu2DState
{
public:
    void Reset(GPU& gpu);
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
};

} // namespace melonDS
