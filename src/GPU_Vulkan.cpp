#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU_Vulkan.h"

#include "GPU3D_Vulkan.h"
#include "NDS.h"
#include "Platform.h"

// Desktop lifecycle adapter for the pinned Android Vulkan 3D implementation.
// Source pin: SapphireRhodonite/melonDS-android-lib
// d77944275fa61f9b79cfcead2c3e98993429a023.
namespace melonDS
{

VulkanRenderer::VulkanRenderer(melonDS::NDS& nds)
    : SoftRenderer(nds)
{
    Rend3D = VulkanRenderer3D::New(GPU.GPU3D);
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Init()
{
    if (!Rend3D || !Rend3D->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "Vulkan renderer init failed stage=3D-context actual=Software");
        return false;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "Vulkan renderer init succeeded requested=Vulkan actual=Vulkan presentation=native-structured-compositor");
    return true;
}

void VulkanRenderer::Stop()
{
    if (auto* vulkan3D = dynamic_cast<VulkanRenderer3D*>(Rend3D.get()))
        vulkan3D->Stop(GPU);
    SoftRenderer::Stop();
}

void VulkanRenderer::PreSavestate()
{
    // The Vulkan renderer owns its synchronization internally. A savestate
    // does not mutate Vulkan-owned resources, so there is no software render
    // thread to suspend here.
}

void VulkanRenderer::PostSavestate()
{
}

void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    auto* vulkan3D = dynamic_cast<VulkanRenderer3D*>(Rend3D.get());
    if (!vulkan3D)
        return;

    vulkan3D->SetRenderSettings(
        settings.Threaded,
        settings.BetterPolygons,
        settings.ScaleFactor,
        true,
        false,
        0.0f,
        0.0f,
        true,
        false,
        false,
        GPU);
}

void VulkanRenderer::VBlank()
{
    if (auto* vulkan3D = dynamic_cast<VulkanRenderer3D*>(Rend3D.get()))
        vulkan3D->Blit(GPU);
}

VulkanRenderer3D* VulkanRenderer::GetVulkanRenderer3D() noexcept
{
    return dynamic_cast<VulkanRenderer3D*>(Rend3D.get());
}

const VulkanRenderer3D* VulkanRenderer::GetVulkanRenderer3D() const noexcept
{
    return dynamic_cast<const VulkanRenderer3D*>(Rend3D.get());
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
