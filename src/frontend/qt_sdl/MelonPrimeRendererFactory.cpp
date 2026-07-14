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
        // MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1: the combined Vulkan : SoftRenderer wrapper is retired.
        return std::make_unique<melonDS::SoftRenderer>(nds);
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
        // MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1: Renderer3D is created only after the previous renderer is stopped.
        return std::make_unique<melonDS::SoftRenderer>(nds);
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

std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DOverrideForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report)
{
    // MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1
    // MELONPRIME_SAPPHIRE_VULKAN_FACTORY_NAMESPACE_FIX_V1
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
