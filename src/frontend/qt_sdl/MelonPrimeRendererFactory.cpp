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
        // The outer renderer object is still SoftRenderer: there is no complete
        // VulkanRenderer yet that owns 2D, 3D, final composition, and
        // presentation (see the reconstruction plan's R2/R3). A separate
        // GPU3D-level Renderer3D override may run the real Vulkan 3D raster
        // path for internal validation (CreateRenderer3DOverrideForSelection),
        // but until R3 lands a complete VulkanRenderer, `actual` must not
        // report Vulkan: the visible output is Software end to end.
        report.actual = renderer3D_Software;
        report.failedStage = "Vulkan outer renderer";
        report.fallbackReason =
            "no complete VulkanRenderer exists yet (owning 2D/3D/output/presentation); "
            "only a GPU3D-level Renderer3D override may be active for internal validation";
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

std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DOverrideForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report)
{
    // GPU3D-level override used only for internal validation of the Vulkan 3D
    // raster path (see plan phase R2 for why this override exists and R3 for
    // the follow-up that removes it once VulkanRenderer owns Rend3D directly).
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

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
