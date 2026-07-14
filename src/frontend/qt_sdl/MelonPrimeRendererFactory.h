#pragma once

#ifdef MELONPRIME_DS

#include <memory>
#include <string>

namespace melonDS
{
class NDS;
class Renderer;
class Renderer3D;
}

namespace MelonPrime::VideoBackend
{

struct BackendCreationReport
{
    int requested = 0;
    int normalized = 0;
    int actual = 0;
    std::string failedStage;
    std::string fallbackReason;
};

std::unique_ptr<melonDS::Renderer> CreateRendererForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report);

// Canonical GPU3D backend factory. GPU3D::SetCurrentRenderer is the only
// active Vulkan Renderer3D ownership path.
std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report);

// Migration alias only; remove after call-site audit in R26.
inline std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DOverrideForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    BackendCreationReport& report)
{
    return CreateRenderer3DForSelection(nds, configuredRenderer, report);
}

#if defined(MELONPRIME_ENABLE_VULKAN)
// Runtime capability flags for the Vulkan frontend pipeline (plan phase R0).
// Every field must be derived from real, successfully-created resource
// state -- never set true at compile time or based on class identity alone.
// Structured2DReady/FinalCompositorReady/FrameQueueReady/SurfaceReady/
// PresenterReady/TimelineSemaphoreReady/DescriptorIndexingReady stay false
// until the Qt frontend session (plan phase R3b) exists to populate them;
// only ContextReady and Renderer3DReady are wired today.
struct VulkanRuntimeCapabilities
{
    bool ContextReady = false;
    bool Renderer3DReady = false;
    bool Structured2DReady = false;
    bool FinalCompositorReady = false;
    bool FrameQueueReady = false;
    bool SurfaceReady = false;
    bool PresenterReady = false;
    bool TimelineSemaphoreReady = false;
    bool DescriptorIndexingReady = false;
};

// Queries capability flags from actual resource state. Safe to call at any
// time; never blocks and never acquires the Vulkan context as a side effect.
VulkanRuntimeCapabilities QueryCurrentVulkanCapabilities(melonDS::NDS& nds);
#endif

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
