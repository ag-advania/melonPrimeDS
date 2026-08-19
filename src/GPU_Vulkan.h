/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef GPU_VULKAN_H
#define GPU_VULKAN_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>

#include "GPU3D_RasterDifferential.h"
#include "GPU_Soft.h"

namespace melonDS
{

class VulkanRenderer3D;

// Software 2D + Vulkan 3D. The software engines also record structured 2D
// planes; phases 8-9 add the Vulkan compositor that combines them with the
// internal-resolution 3D image, and ScreenPanelVulkan (phases 10-12) presents
// the result through a native VkSwapchainKHR.
class VulkanRenderer final : public SoftRenderer
{
public:
    explicit VulkanRenderer(melonDS::NDS& nds);
    ~VulkanRenderer() override;

    bool Init() override;
    void Stop() override;

    // SoftRenderer's implementations dynamic_cast Rend3D to SoftRenderer3D and
    // dereference the result unchecked, so every one of these must be
    // overridden here -- Rend3D is a VulkanRenderer3D, and the cast would
    // return null.
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void Start3DRendering() override;
    void VBlank() override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

    bool NeedsShaderCompile() override;
    void ShaderCompileStep(int& current, int& count) override;

    [[nodiscard]] bool HasRuntimeFailure() const noexcept;
    [[nodiscard]] const std::string& GetRuntimeFailureReason() const noexcept;

    // Configured low-latency settings, published for the emulation thread's
    // beginVulkanLowLatencyFrame() call (EmuThread.cpp). These are the values
    // SetRenderSettings() was given; the presenter separately capability-gates
    // the VK_NV_low_latency2 / VK_AMD_anti_lag paths and owns effective
    // present-slot authority.
    [[nodiscard]] int GetNvidiaReflexMode() const noexcept { return NvidiaReflexMode; }
    [[nodiscard]] bool GetAmdAntiLag2Enabled() const noexcept { return AmdAntiLag2Enabled; }

    [[nodiscard]] VulkanRenderer3D* GetVulkanRenderer3D() noexcept;
    [[nodiscard]] const VulkanRenderer3D* GetVulkanRenderer3D() const noexcept;

    // MELONPRIME_VULKAN_PRESENT_HOOK_V1
    //
    // Called at the end of VBlank(), on the emulation thread, immediately after
    // the compositor has published this frame's composed screens. The
    // presentation panel (ScreenPanelVulkan) uses it to capture the frame at
    // the one instant it is guaranteed to be the current one: GetOutput()
    // returns pointers into a double-buffered surface that the *next* VBlank
    // overwrites, so the panel snapshots them here rather than holding a
    // borrowed pointer across a renderer transition.
    //
    // A plain function pointer rather than std::function on purpose: this is
    // called once per DS frame from the emulation thread and must not allocate
    // or take a lock. Installing and clearing happen on the same thread as the
    // call, from the panel's renderer-transition path.
    using VBlankObserver = void (*)(void* userData);
    void SetVBlankObserver(VBlankObserver observer, void* userData) noexcept
    {
        VBlankObserverFn = observer;
        VBlankObserverData = userData;
    }

private:
    std::unique_ptr<Renderer3D> DifferentialReference;
    RasterDifferential::State DifferentialState;
    int NvidiaReflexMode = 0;
    bool AmdAntiLag2Enabled = false;
    bool NativeGPU2DAnnounced = false;
    bool NativeGPU2DFallbackAnnounced = false;
    bool NativeGPU2DStartupFallbackAnnounced = false;

    VBlankObserver VBlankObserverFn = nullptr;
    void* VBlankObserverData = nullptr;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // GPU_VULKAN_H
