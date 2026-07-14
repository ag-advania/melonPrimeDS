#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h requires the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1
// MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
#include <memory>
#include "GPU3D_Vulkan.h"

namespace melonDS
{
struct VulkanRendererShellContract
{
    const char* ModeName = nullptr;
    bool ComputeSelected = false;
    bool UsesSoftwareCorrectnessBaseline = false;
    bool NativeVulkanRomScaleCompatibilityBridge = true;
    bool NativeVulkan3DImplemented = true;
    bool SapphireRenderer3DOwnership = true;
    bool SapphireFrameLifecycle = true;
    bool SapphireStructured2DMetadata = true;
    bool SapphireStructured2DLineMetadata = true;
    bool SapphirePacked2DGpuUpload = true;
    bool SapphireGpuCompositionInput = true;
    bool SapphireGpuCompositionResources = true;
    bool SapphireGpuCompositionDescriptors = true;
    bool SapphireGpuCompositionPushConstants = true;
    bool SapphireGpuCompositionCommandContext = true;
    bool SapphireGpuFinalComposition = false;
    bool SapphireZeroCopyPresenter = false;
    bool CpuReadbackCompatibilityPath = false;
    u32 ContractVersion = 44;
};

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept;
std::unique_ptr<Renderer3D> CreateSapphireVulkanRenderer3D(
    GPU3D& gpu3D,
    bool computeSelected) noexcept;
} // namespace melonDS
