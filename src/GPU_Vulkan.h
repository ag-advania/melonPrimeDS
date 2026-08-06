#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU_Soft.h"

namespace melonDS
{

class VulkanRenderer3D;

class VulkanRenderer final : public SoftRenderer
{
public:
    explicit VulkanRenderer(melonDS::NDS& nds);
    ~VulkanRenderer() override;

    bool Init() override;
    void Stop() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void VBlank() override;
    RendererOutput GetOutput() override;

    [[nodiscard]] VulkanRenderer3D* GetVulkanRenderer3D() noexcept;
    [[nodiscard]] const VulkanRenderer3D* GetVulkanRenderer3D() const noexcept;
    [[nodiscard]] int GetNvidiaReflexMode() const noexcept { return NvidiaReflexMode; }
    [[nodiscard]] bool GetAmdAntiLag2Enabled() const noexcept { return AmdAntiLag2Enabled; }

private:
    int NvidiaReflexMode = 1;
    bool AmdAntiLag2Enabled = true;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
