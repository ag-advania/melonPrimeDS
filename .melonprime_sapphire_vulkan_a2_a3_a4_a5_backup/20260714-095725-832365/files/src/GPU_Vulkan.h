#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h requires the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1
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
    bool CpuReadbackCompatibilityPath = false;
    u32 ContractVersion = 32;
};

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept;
std::unique_ptr<Renderer3D> CreateSapphireVulkanRenderer3D(
    GPU3D& gpu3D,
    bool computeSelected) noexcept;
} // namespace melonDS
