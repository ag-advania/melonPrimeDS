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

RendererCreationResult::RendererCreationResult() = default;
RendererCreationResult::~RendererCreationResult() = default;
RendererCreationResult::RendererCreationResult(RendererCreationResult&&) noexcept = default;
RendererCreationResult& RendererCreationResult::operator=(RendererCreationResult&&) noexcept = default;

void RegenerateSoftwareFallback(
    melonDS::NDS& nds,
    RendererCreationResult& result,
    std::string failedStage,
    std::string fallbackReason)
{
    result.OuterRenderer = std::make_unique<melonDS::SoftRenderer>(nds);
    result.Renderer3D.reset();
    result.OuterAction = OuterRendererAction::Replace;
    result.Presentation = PresentationBackend::NativeQt;
    result.ActualRenderer = renderer3D_Software;
    result.FailedStage = std::move(failedStage);
    result.FallbackReason = std::move(fallbackReason);
}

RendererCreationResult CreateRendererForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    bool useGLPresentation)
{
    RendererCreationResult result;
    result.RequestedRenderer = ResolveRequestedRenderer(configuredRenderer);
    result.NormalizedRenderer = NormalizeRendererForPlatform(result.RequestedRenderer);
    result.ActualRenderer = result.NormalizedRenderer;
    result.Presentation = ResolvePresentationBackend(
        useGLPresentation, result.NormalizedRenderer);
    if (result.NormalizedRenderer != result.RequestedRenderer)
    {
        result.FailedStage = "renderer normalization";
        result.FallbackReason =
            "requested renderer is unavailable or incompatible with the active backend";
    }

    switch (result.NormalizedRenderer)
    {
    case renderer3D_Software:
        result.OuterRenderer = std::make_unique<melonDS::SoftRenderer>(nds);
        break;
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
        result.OuterRenderer = std::make_unique<melonDS::GLRenderer>(nds, false);
        break;
    case renderer3D_OpenGLCompute:
        result.OuterRenderer = std::make_unique<melonDS::GLRenderer>(nds, true);
        break;
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    case renderer3D_Metal:
        result.OuterRenderer = std::make_unique<melonDS::MetalRenderer>(nds, false);
        break;
    case renderer3D_MetalCompute:
        result.OuterRenderer = std::make_unique<melonDS::MetalRenderer>(nds, true);
        break;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case renderer3D_Vulkan:
    {
        if (dynamic_cast<melonDS::SoftRenderer*>(&nds.GPU.GetRenderer()) != nullptr)
        {
            result.OuterAction = OuterRendererAction::KeepCurrent;
        }
        else
        {
            result.OuterRenderer = std::make_unique<melonDS::SoftRenderer>(nds);
            result.OuterAction = OuterRendererAction::Replace;
        }
        result.Renderer3D = CreateRenderer3DForSelection(
            nds, result.NormalizedRenderer, result);
        result.ActualRenderer = renderer3D_Software;
        if (!result.Renderer3D)
        {
            const std::string failedStage = result.FailedStage.empty()
                ? "Sapphire Vulkan Renderer3D initialization"
                : result.FailedStage;
            const std::string fallbackReason = result.FallbackReason.empty()
                ? "Vulkan Renderer3D initialization failed"
                : result.FallbackReason;
            ActivateVulkanRuntimeFallback(
                failedStage.c_str(), -1);
            RegenerateSoftwareFallback(
                nds, result, failedStage, fallbackReason);
        }
        break;
    }
#endif
    default:
        result.NormalizedRenderer = renderer3D_Software;
        RegenerateSoftwareFallback(
            nds,
            result,
            "renderer normalization",
            "renderer ID is unavailable in this build");
        break;
    }

    return result;
}

std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    RendererCreationResult& result)
{
    const int normalized = NormalizeRendererForPlatform(configuredRenderer);
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (normalized == renderer3D_Vulkan)
    {
        auto renderer3D = melonDS::CreateSapphireVulkanRenderer3D(
            nds.GPU.GPU3D, false);
        if (!renderer3D)
        {
            result.ActualRenderer = renderer3D_Software;
            result.FailedStage = "Sapphire Vulkan Renderer3D initialization";
            result.FallbackReason = "Vulkan Renderer3D initialization failed";
        }
        return renderer3D;
    }
#else
    (void)nds;
#endif
    return nullptr;
}

int EvaluateActualRenderer(
    melonDS::NDS& nds,
    int normalizedRenderer,
    PresentationBackend presentation)
{
    normalizedRenderer = NormalizeRendererForPlatform(normalizedRenderer);
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (normalizedRenderer == renderer3D_Vulkan)
    {
        if (presentation != PresentationBackend::Vulkan)
            return renderer3D_Software;
        const VulkanRuntimeCapabilities caps = QueryCurrentVulkanCapabilities(nds);
        return caps.Renderer3DReady
            && caps.Structured2DReady
            && caps.FinalCompositorReady
            && caps.FrameQueueReady
            && caps.SurfaceReady
            && caps.PresenterReady
            ? renderer3D_Vulkan
            : renderer3D_Software;
    }
#else
    (void)nds;
#endif

    if (RendererRequiresOpenGLContext(normalizedRenderer)
        && presentation != PresentationBackend::OpenGL)
        return renderer3D_Software;
#if defined(MELONPRIME_ENABLE_METAL)
    if ((normalizedRenderer == renderer3D_Metal
         || normalizedRenderer == renderer3D_MetalCompute)
        && presentation != PresentationBackend::Metal)
        return renderer3D_Software;
#endif
    return normalizedRenderer;
}

#if defined(MELONPRIME_ENABLE_VULKAN)
VulkanRuntimeCapabilities QueryCurrentVulkanCapabilities(melonDS::NDS& nds)
{
    VulkanRuntimeCapabilities caps;

    // Every flag is read from a successfully-created runtime owner/resource.
    // A lost device (VK_ERROR_DEVICE_LOST observed by any Vulkan subsystem)
    // is treated the same as "context not ready": it must never be masked by
    // the renderer object still existing, since destroying/recreating it is
    // a separate, explicit teardown step (R24), not something this query
    // performs itself.
    const bool deviceLost = melonDS::VulkanContext::Get().IsDeviceLost();
    caps.ContextReady = melonDS::VulkanContext::Get().IsReady() && !deviceLost;
    if (nds.GPU.GPU3D.HasCurrentRenderer())
    {
        caps.Renderer3DReady =
            dynamic_cast<melonDS::VulkanRenderer3D*>(
                &nds.GPU.GPU3D.GetCurrentRenderer()) != nullptr
            && !deviceLost;
    }

    auto* emuInstance = static_cast<EmuInstance*>(nds.UserData);
    if (emuInstance != nullptr)
    {
        auto& session = emuInstance->vulkanFrontendSession();
        caps.Structured2DReady = session.hasCompositedFrame();
        caps.FinalCompositorReady = session.hasPresentedFrame();
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
