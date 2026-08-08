#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <functional>

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

    // Runs at VBlank, right after the 3D frame is finished and before the GPU
    // reaches VCount 215. That scanline starts the NEXT 3D render into the same
    // color target, so this is the only point where the current frame's
    // structured 2D metadata and the current frame's 3D image both exist.
    // Composing anywhere later pairs this frame's 2D with the next frame's 3D,
    // which on Metroid Prime Hunters means the 3D lands on the wrong LCD every
    // other frame, because the game alternates which screen it renders 3D for.
    // DX12Renderer::VBlank() composes at exactly this point for the same reason.
    using VBlankHook = std::function<void()>;
    void SetVBlankHook(VBlankHook hook) noexcept { VBlankComposeHook = std::move(hook); }
    [[nodiscard]] bool HasVBlankHook() const noexcept { return static_cast<bool>(VBlankComposeHook); }

private:
    int NvidiaReflexMode = 1;
    bool AmdAntiLag2Enabled = true;
    VBlankHook VBlankComposeHook;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
