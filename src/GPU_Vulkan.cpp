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
    NvidiaReflexMode = settings.NvidiaReflexMode;
    AmdAntiLag2Enabled = settings.AmdAntiLag2Enabled;
    auto* vulkan3D = dynamic_cast<VulkanRenderer3D*>(Rend3D.get());
    if (!vulkan3D)
        return;

    // RendererSettings::Threaded is 3D.Soft.Threaded, the software renderer's
    // own "use separate thread" option, and no other hardware backend reads it:
    // GLRenderer::SetRenderSettings and DX12Renderer::SetRenderSettings both
    // ignore it entirely. Forwarding it here let that unrelated checkbox (which
    // defaults to on) switch the Vulkan renderer onto the pinned Android
    // early-submit path, where VCount144 submits the 3D frame and
    // SkipRenderAtVCount215 then skips the VCount215 submission. That puts the
    // 3D render on a different scanline than Software, OpenGL Compute and DX12
    // use, so the structured 2D metadata no longer pairs with the 3D image the
    // compositor samples. Desktop always submits at VCount215, like they do.
    vulkan3D->SetRenderSettings(
        false,
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

    // See SetVBlankHook: the frontend composes here so that the structured 2D
    // planes finished at scanline 191 are paired with the 3D image that scanline
    // 215 is about to overwrite.
    if (VBlankComposeHook)
        VBlankComposeHook();
}

RendererOutput VulkanRenderer::GetOutput()
{
    // SoftRenderer exposes its CPU framebuffers by default. Those buffers
    // deliberately omit Vulkan's 3D target, so advertising them would make
    // the Qt screen select its software paint path and bypass the structured
    // Vulkan compositor entirely.
    return {};
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
