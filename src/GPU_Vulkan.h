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
    void AllocCapture(u32 bank, u32 start, u32 len) override;
    CaptureSyncResult SyncVRAMCapture(
        u32 bank, u32 start, u32 len, bool complete) override;
    void InvalidateVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        CaptureAuthorityTransitionReason reason) override;
    [[nodiscard]] NativeCaptureStateIdentity
    GetNativeCaptureStateIdentity() const noexcept override;
    [[nodiscard]] const char* GetCaptureBackendName() const noexcept override
    {
        return "Vulkan";
    }

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

private:
    [[nodiscard]] bool CanUseNativeGPU2DForFrame() const noexcept override;

    // Developer-only 3D oracle comparison. Called from both publication paths
    // because it compares the 3D renderers, not the composed 2D frame.
    void CompareRasterDifferentialFrame();

    std::unique_ptr<Renderer3D> DifferentialReference;
    RasterDifferential::State DifferentialState;
    int NvidiaReflexMode = 0;
    bool AmdAntiLag2Enabled = false;
    bool NativeGPU2DAnnounced = false;
    bool NativeGPU2DFallbackAnnounced = false;
    bool NativeGPU2DStartupFallbackAnnounced = false;

};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // GPU_VULKAN_H
