#pragma once

#ifdef MELONPRIME_DS

#include <memory>
#include <string>

#include "MelonPrimeVideoBackend.h"

namespace melonDS
{
class NDS;
class Renderer;
class Renderer3D;
}

namespace MelonPrime::VideoBackend
{

enum class OuterRendererAction
{
    KeepCurrent,
    Replace,
};

struct RendererCreationResult
{
    RendererCreationResult();
    ~RendererCreationResult();
    RendererCreationResult(RendererCreationResult&&) noexcept;
    RendererCreationResult& operator=(RendererCreationResult&&) noexcept;
    RendererCreationResult(const RendererCreationResult&) = delete;
    RendererCreationResult& operator=(const RendererCreationResult&) = delete;

    std::unique_ptr<melonDS::Renderer> OuterRenderer;
    std::unique_ptr<melonDS::Renderer3D> Renderer3D;
    OuterRendererAction OuterAction = OuterRendererAction::Replace;
    PresentationBackend Presentation = PresentationBackend::NativeQt;
    int RequestedRenderer = 0;
    int NormalizedRenderer = 0;
    int ActualRenderer = 0;
    std::string FailedStage;
    std::string FallbackReason;
};

RendererCreationResult CreateRendererForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    bool useGLPresentation);

// Canonical GPU3D backend factory. GPU3D::SetCurrentRenderer is the only
// active Vulkan Renderer3D ownership path.
std::unique_ptr<melonDS::Renderer3D> CreateRenderer3DForSelection(
    melonDS::NDS& nds,
    int configuredRenderer,
    RendererCreationResult& result);

void RegenerateSoftwareFallback(
    melonDS::NDS& nds,
    RendererCreationResult& result,
    std::string failedStage,
    std::string fallbackReason);

int EvaluateActualRenderer(
    melonDS::NDS& nds,
    int normalizedRenderer,
    PresentationBackend presentation);

#if defined(MELONPRIME_ENABLE_VULKAN)
// Runtime capability flags for the Vulkan frontend pipeline (plan phase R0).
// Every field must be derived from real, successfully-created resource
// state -- never set true at compile time or based on class identity alone.
// The Qt frontend session populates structured/compositor/queue/surface and
// presenter state after the first complete submitted frame. Context feature
// bits remain direct queries of the shared VulkanContext.
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
