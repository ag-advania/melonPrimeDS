#include "GPU_Vulkan.h"
#include "Platform.h"

namespace melonDS
{
std::unique_ptr<Renderer3D> CreateSapphireVulkanRenderer3D(
    GPU3D& gpu3D,
    bool computeSelected) noexcept
{
    auto renderer = VulkanRenderer3D::New(gpu3D);
    if (!renderer || !renderer->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[MelonPrime] Vulkan Renderer3D initialization failed; requested=%s\n",
            computeSelected ? "compute" : "graphics");
        return nullptr;
    }
    // 0.7.0.rc4 reference only ships a graphics-hardware backend; a compute
    // backend is a separate future object (see plan phase R11), not a mode
    // switch on this class.
    renderer->SetBackendMode(VulkanRenderer3D::BackendMode::GraphicsHardware);
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] Vulkan Renderer3D initialized (GPU3D-level override only; "
        "not yet the active renderer's owned Rend3D, and final 2D/3D composition "
        "and presentation are still Software); requested=%s\n",
        computeSelected ? "compute" : "graphics");
    return renderer;
}
} // namespace melonDS
