#ifdef MELONPRIME_DS

#include "MelonPrimeRendererFactory.h"

#include "EmuInstance.h"
#include "MelonPrimeVideoBackend.h"

#include "GPU_OpenGL.h"
#include "GPU_Soft.h"
#if defined(MELONPRIME_ENABLE_METAL)
#include "GPU_Metal.h"
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
#include "GPU_Vulkan.h"
#include "MelonPrimeVulkanFrontendSession.h"
#include "VulkanContext.h"
#endif

namespace MelonPrime::VideoBackend
{

std::unique_ptr<melonDS::Renderer> CreateRendererForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report)
{
    report = {};
    report.requested = ResolveRequestedRenderer(configuredRenderer);
    report.normalized = NormalizeRendererForPlatform(report.requested);
    report.actual = report.normalized;
    if (report.normalized != report.requested)
    {
        report.failedStage = "renderer normalization";
        report.fallbackReason = "requested renderer is unavailable or incompatible with the active backend";
    }

    switch (report.normalized)
    {
    case renderer3D_Software:
        return std::make_unique<melonDS::SoftRenderer>(nds);
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
        return std::make_unique<melonDS::GLRenderer>(nds, false);
    case renderer3D_OpenGLCompute:
        return std::make_unique<melonDS::GLRenderer>(nds, true);
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    case renderer3D_Metal:
        return std::make_unique<melonDS::MetalRenderer>(nds, false);
    case renderer3D_MetalCompute:
        return std::make_unique<melonDS::MetalRenderer>(nds, true);
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case renderer3D_Vulkan:
    case renderer3D_VulkanCompute:
        // The outer 2D/timing renderer stays SoftRenderer here: melonDS's own
        // Vulkan architecture (see the reconstruction plan, "重大計画修正版")
        // has GPU3D alone own the swappable Renderer3D backend -- there is no
        // separate outer "VulkanRenderer" class to build. GPU3D may already
        // own a real VulkanRenderer3D for internal validation, but `actual`
        // must not report Vulkan
        // until the Qt frontend session actually consumes it end to end:
        // structured 2D snapshot -> VulkanOutput final composition ->
        // FrameQueue -> VulkanSurfacePresenter (plan phase R3). Until that
        // pipeline is connected, visible output is Software end to end.
        report.actual = renderer3D_Software;
        report.failedStage = "Vulkan frontend session";
        report.fallbackReason =
            "Vulkan frontend composition/presentation path incomplete: GPU3D may own a "
            "VulkanRenderer3D backend for internal validation, but the Qt frontend session "
            "(structured 2D snapshot + VulkanOutput + FrameQueue + surface + "
            "VulkanSurfacePresenter, plan phase R3) is not yet connected, so visible output "
            "is Software end to end";
        return std::make_unique<melonDS::SoftRenderer>(nds);
#endif
    default:
        report.normalized = renderer3D_Software;
        report.actual = renderer3D_Software;
        report.failedStage = "renderer normalization";
        report.fallbackReason = "renderer ID is unavailable in this build";
        return std::make_unique<melonDS::SoftRenderer>(nds);
    }
}

std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report)
{
    // GPU3D owns the active Renderer3D backend; this factory builds the
    // Vulkan/VulkanCompute selection for that existing ownership path. R2
    // canonicalizes the temporary Override name without moving ownership.
    const int normalized = NormalizeRendererForPlatform(
        ResolveRequestedRenderer(configuredRenderer));
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (normalized == renderer3D_Vulkan || normalized == renderer3D_VulkanCompute)
    {
        const bool computeRequested = normalized == renderer3D_VulkanCompute;
        auto renderer3D = melonDS::CreateSapphireVulkanRenderer3D(
            nds.GPU.GPU3D, computeRequested);
        if (!renderer3D)
        {
            report.actual = renderer3D_Software;
            report.failedStage = computeRequested
                ? "Sapphire Vulkan Compute Renderer3D initialization"
                : "Sapphire Vulkan Renderer3D initialization";
            report.fallbackReason = "Vulkan Renderer3D initialization failed";
        }
        return renderer3D;
    }
#else
    (void)nds;
#endif
    return nullptr;
}

#if defined(MELONPRIME_ENABLE_VULKAN)
VulkanRuntimeCapabilities QueryCurrentVulkanCapabilities(melonDS::NDS& nds)
{
    VulkanRuntimeCapabilities caps;

    // Every flag is read from a successfully-created runtime owner/resource.
    caps.ContextReady = melonDS::VulkanContext::Get().IsReady();
    if (nds.GPU.GPU3D.HasCurrentRenderer())
    {
        caps.Renderer3DReady =
            dynamic_cast<melonDS::VulkanRenderer3D*>(
                &nds.GPU.GPU3D.GetCurrentRenderer()) != nullptr;
    }

    auto* emuInstance = static_cast<EmuInstance*>(nds.UserData);
    if (emuInstance != nullptr)
    {
        auto& session = emuInstance->vulkanFrontendSession();
        caps.Structured2DReady = session.hasCompleteStructuredSnapshot();
        caps.FinalCompositorReady = session.hasCompositedFrame();
        caps.FrameQueueReady = session.isInitialized();
        caps.SurfaceReady = session.hasRegisteredPresenter();
        caps.PresenterReady = session.hasRegisteredPresenter();
    }
    caps.TimelineSemaphoreReady = caps.ContextReady
        && melonDS::VulkanContext::Get().SupportsTimelineSemaphores();
    caps.DescriptorIndexingReady = caps.ContextReady
        && melonDS::VulkanContext::Get().SupportsDynamicTextureIndexing();
    return caps;
}
#endif

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
