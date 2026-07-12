#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_RENDERER_SHELL_V1

#include "GPU_Soft.h"

namespace melonDS
{

struct VulkanRendererShellContract
{
    const char* ModeName = nullptr;
    bool ComputeSelected = false;
    bool UsesSoftwareCorrectnessBaseline = true;
    bool NativeVulkanRasterBootstrapAvailable = true;
    bool NativeVulkanClearPlaneBootstrapAvailable = true;
    bool NativeVulkanClearBitmapBootstrapAvailable = true;
    bool NativeVulkanVertexUploadBootstrapAvailable = true;
    bool NativeVulkanPolygonBatchBootstrapAvailable = true;
    bool NativeVulkanOpaquePipelineBootstrapAvailable = true;
    bool NativeVulkanTranslucentPipelineBootstrapAvailable = true;
    bool NativeVulkanShadowPipelineBootstrapAvailable = true;
    bool NativeVulkanToonHighlightContractAvailable = true;
    bool NativeVulkan3DImplemented = false;
    u32 ContractVersion = 10;
};

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept;

// Phase 6 establishes the Vulkan renderer identity and lifecycle while keeping
// Software 2D/3D/capture/CPU-BGRA output as the correctness source. Phase 7.1
// proves a native offscreen graphics pipeline; Phase 7.2 adds typed plain
// clear-plane targets; Phase 7.3 adds the repeated VRAM clear-bitmap pass;
// Phase 7.4 adds packed vertex/index/polygon upload; Phase 7.5 adds ordered,
// adjacent-only pipeline batches; Phase 7.6 adds an untextured opaque Vulkan
// draw bootstrap; Phase 7.7 adds untextured translucent blend/depth/stencil;
// Phase 7.8 adds two-stage shadow mask/reject/blend. ROM Renderer3D integration
// is still intentionally absent.
class VulkanRenderer final : public SoftRenderer
{
public:
    explicit VulkanRenderer(melonDS::NDS& nds, bool useComputeRenderer = false) noexcept;
    ~VulkanRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void SwapBuffers() override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

    [[nodiscard]] bool IsComputeRendererSelected() const noexcept
    {
        return ComputeRendererSelected;
    }

    [[nodiscard]] int GetRecordedScaleFactor() const noexcept
    {
        return ScaleFactor;
    }

    [[nodiscard]] u64 GetFrameSerialForDiagnostics() const noexcept
    {
        return FrameSerial;
    }

    [[nodiscard]] u64 GetOutputGenerationForDiagnostics() const noexcept
    {
        return OutputGeneration;
    }

private:
    void AdvanceOutputGeneration() noexcept;

    bool ComputeRendererSelected = false;
    bool Initialized = false;
    int ScaleFactor = 1;
    u64 FrameSerial = 0;
    u64 OutputGeneration = 1;
};

} // namespace melonDS
