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
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] Sapphire Vulkan Renderer3D active; requested=%s actual=graphics-hardware ownership=GPU3D\n",
        computeSelected ? "compute" : "graphics");
    return renderer;
}
} // namespace melonDS
