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
    {
        const auto contract = melonDS::DescribeVulkanRendererShell(false);
        // MELONPRIME_VULKAN_ROM_SCALE_FACTORY_V1
        if (!contract.NativeVulkanRomScaleCompatibilityBridge)
        {
            report.actual = renderer3D_Software;
            report.failedStage = "Vulkan ROM-visible compatibility bridge";
            report.fallbackReason =
                "Vulkan raster was requested, but the ROM-visible compatibility bridge is unavailable";
        }
        return std::make_unique<melonDS::VulkanRenderer>(nds, false);
    }
    case renderer3D_VulkanCompute:
    {
        const auto contract = melonDS::DescribeVulkanRendererShell(true);
        if (!contract.NativeVulkanRomScaleCompatibilityBridge)
        {
            report.actual = renderer3D_Software;
            report.failedStage = "Vulkan Compute ROM-visible compatibility bridge";
            report.fallbackReason =
                "Vulkan Compute was requested, but the ROM-visible compatibility bridge is unavailable";
        }
        return std::make_unique<melonDS::VulkanRenderer>(nds, true);
    }
#endif
    default:
        report.normalized = renderer3D_Software;
        report.actual = renderer3D_Software;
        report.failedStage = "renderer normalization";
        report.fallbackReason = "renderer ID is unavailable in this build";
        return std::make_unique<melonDS::SoftRenderer>(nds);
    }
}

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
