#include "GPU_Vulkan.h"
#include "Platform.h"

namespace melonDS
{
VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept
{
    VulkanRendererShellContract contract{};
    contract.ModeName = computeSelected
        ? "Vulkan Compute Shader (Sapphire graphics backend)"
        : "Vulkan (Sapphire graphics backend)";
    contract.ComputeSelected = computeSelected;
    return contract;
}

std::unique_ptr<Renderer3D> CreateSapphireVulkanRenderer3D(
    GPU3D& gpu3D,
    bool computeSelected) noexcept
{
    auto renderer = VulkanRenderer3D::New(gpu3D);
    if (!renderer || !renderer->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[MelonPrime] Sapphire Vulkan Renderer3D initialization failed; requested=%s\n",
            computeSelected ? "compute" : "graphics");
        return nullptr;
    }
    renderer->SetBackendMode(VulkanRenderer3D::BackendMode::GraphicsHardware);
    // MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] Sapphire Vulkan Renderer3D active; requested=%s actual=graphics-hardware ownership=GPU3D structured2d=gpu-upload-ready composition_resources=descriptor-ready composition_command=ready cpu_readback_compat=disabled\n",
        computeSelected ? "compute" : "graphics");
    return renderer;
}
} // namespace melonDS
