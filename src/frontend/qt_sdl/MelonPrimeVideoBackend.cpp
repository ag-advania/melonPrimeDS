#ifdef MELONPRIME_DS

#include "MelonPrimeVideoBackend.h"
#include "EmuInstance.h" // renderer3D_* enum

#if defined(MELONPRIME_ENABLE_METAL) || defined(MELONPRIME_ENABLE_VULKAN)
#include <cstdlib>
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#endif

namespace MelonPrime::VideoBackend {

#if defined(MELONPRIME_ENABLE_VULKAN)
namespace
{
std::atomic<bool> gVulkanRuntimeFallback{false};
std::mutex gVulkanRuntimeFallbackMutex;
std::string gVulkanRuntimeFallbackReason;
}
#endif

static_assert(renderer3D_Software == 0);
#ifdef OGLRENDERER_ENABLED
static_assert(renderer3D_OpenGL == 1);
static_assert(renderer3D_OpenGLCompute == 2);
#endif
#if defined(MELONPRIME_ENABLE_METAL)
static_assert(renderer3D_Metal == 3);
static_assert(renderer3D_MetalCompute == 4);
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
static_assert(renderer3D_Vulkan == 5);
static_assert(renderer3D_VulkanCompute == 6);
#endif
static_assert(renderer3D_Max == 7);

int MigrateLegacyRendererId(int renderer)
{
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (renderer == renderer3D_VulkanCompute)
        return renderer3D_Vulkan;
#endif
    return renderer;
}

#if defined(MELONPRIME_ENABLE_METAL)
bool ShouldForceMetalPresenterFromEnv()
{
    const char* env = std::getenv("MELONPRIME_FORCE_METAL_PRESENTER");
    return env != nullptr && env[0] == '1';
}

bool ShouldForceMetalRendererFromEnv()
{
    const char* env = std::getenv("MELONPRIME_FORCE_METAL_RENDERER");
    return env != nullptr && env[0] == '1';
}
#endif

#if defined(MELONPRIME_ENABLE_VULKAN)
bool ShouldForceVulkanPresenterFromEnv()
{
    const char* env = std::getenv("MELONPRIME_FORCE_VULKAN_PRESENTER");
    return env != nullptr && env[0] == '1';
}

bool ShouldForceVulkanRendererFromEnv()
{
    const char* env = std::getenv("MELONPRIME_FORCE_VULKAN_RENDERER");
    return env != nullptr && env[0] == '1';
}

bool ActivateVulkanRuntimeFallback(const char* stage, int result)
{
    bool expected = false;
    if (!gVulkanRuntimeFallback.compare_exchange_strong(expected, true))
        return false;
    std::lock_guard<std::mutex> lock(gVulkanRuntimeFallbackMutex);
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "stage=%s VkResult=%d",
        stage ? stage : "unknown", result);
    gVulkanRuntimeFallbackReason = buffer;
    return true;
}

bool VulkanRuntimeFallbackActive()
{
    return gVulkanRuntimeFallback.load();
}

const char* VulkanRuntimeFallbackReason()
{
    std::lock_guard<std::mutex> lock(gVulkanRuntimeFallbackMutex);
    return gVulkanRuntimeFallbackReason.c_str();
}
#endif

int ResolveRequestedRenderer(int configuredRenderer)
{
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (ShouldForceVulkanRendererFromEnv())
        return renderer3D_Vulkan;
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    if (ShouldForceMetalRendererFromEnv())
        return renderer3D_Metal;
#endif
    return MigrateLegacyRendererId(configuredRenderer);
}

int NormalizeRendererForPlatform(int requested)
{
    requested = MigrateLegacyRendererId(ResolveRequestedRenderer(requested));

#if defined(MELONPRIME_ENABLE_VULKAN)
    if (VulkanRuntimeFallbackActive() &&
        requested == renderer3D_Vulkan)
        return renderer3D_Software;
#endif

#if defined(MELONPRIME_ENABLE_METAL)
    // Phase 4 bootstrap: while the Metal presenter is force-selected there is
    // no GL context for a hardware 3D renderer to render into (no
    // working Metal 3D renderer exists yet -- Phase 7 only adds a shell).
    // Force non-Metal hardware renderers back to Software rather than let
    // EmuThread::updateRenderer() try to construct a GLRenderer against a
    // window that never created a GL surface.
    if (ShouldForceMetalPresenterFromEnv() &&
        requested != renderer3D_Software &&
        requested != renderer3D_Metal &&
        requested != renderer3D_MetalCompute)
    {
        return renderer3D_Software;
    }
#endif

#if defined(MELONPRIME_ENABLE_VULKAN)
    if (ShouldForceVulkanPresenterFromEnv() &&
        requested != renderer3D_Software &&
        requested != renderer3D_Vulkan)
    {
        return renderer3D_Software;
    }
#endif

#if defined(__APPLE__) && defined(OGLRENDERER_ENABLED) // scatter-budget-exempt: renderer-selection normalization, not input dispatch
    // macOS OpenGL cannot run the compute-shader renderer path (see
    // melonprime_macos_compute_renderer_restriction.md and
    // compute-renderer-mosaic-bug.md). The settings-dialog High2 button is
    // already disabled on macOS, but that only stops *new* selections from
    // the UI; a config value saved on another platform, imported, or
    // hand-edited must be normalized here before it ever reaches the
    // renderer factory in EmuThread::updateRenderer().
    if (requested == renderer3D_OpenGLCompute)
        return renderer3D_OpenGL;
#endif

    switch (requested)
    {
    case renderer3D_Software:
        return requested;
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
    case renderer3D_OpenGLCompute:
        return requested;
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    case renderer3D_Metal:
    case renderer3D_MetalCompute:
        return requested;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case renderer3D_Vulkan:
        return requested;
#endif
    default:
        return renderer3D_Software;
    }
}

bool RendererIsAvailableInBuild(int renderer)
{
    renderer = MigrateLegacyRendererId(renderer);
    switch (renderer)
    {
    case renderer3D_Software:
        return true;
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
    case renderer3D_OpenGLCompute:
        return true;
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    case renderer3D_Metal:
    case renderer3D_MetalCompute:
        return true;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case renderer3D_Vulkan:
        return true;
#endif
    default:
        return false;
    }
}

bool RendererRequiresOpenGLContext(int renderer)
{
#ifdef OGLRENDERER_ENABLED
    return renderer == renderer3D_OpenGL || renderer == renderer3D_OpenGLCompute;
#else
    (void)renderer;
    return false;
#endif
}

#if defined(MELONPRIME_ENABLE_VULKAN)
bool RendererRequiresVulkanContext(int renderer)
{
    return MigrateLegacyRendererId(renderer) == renderer3D_Vulkan;
}
#endif

PresentationBackend ResolvePresentationBackend(bool useGLConfig, int requestedRenderer)
{
#if defined(MELONPRIME_ENABLE_VULKAN)
    const int effectiveRequested = ResolveRequestedRenderer(requestedRenderer);
    if (VulkanRuntimeFallbackActive() &&
        effectiveRequested == renderer3D_Vulkan)
        return PresentationBackend::NativeQt;
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    // Phase 4/7 bootstrap (see ShouldForceMetalPresenterFromEnv() and
    // ShouldForceMetalRendererFromEnv()). Checked
    // before the GL branch so both MainWindow::createScreenPanel() and
    // EmuInstance::usesOpenGL() agree Metal owns presentation -- the latter
    // then correctly reports false (IsOpenGLPresentation(Metal) == false),
    // so EmuThread never requests a GL context for a Metal-presented window.
    if (ShouldForceMetalPresenterFromEnv())
        return PresentationBackend::Metal;
#endif

#if defined(MELONPRIME_ENABLE_VULKAN)
    if (ShouldForceVulkanPresenterFromEnv())
        return PresentationBackend::Vulkan;
#endif

    const int normalized = NormalizeRendererForPlatform(requestedRenderer);
#if defined(MELONPRIME_ENABLE_METAL)
    if (normalized == renderer3D_Metal || normalized == renderer3D_MetalCompute)
        return PresentationBackend::Metal;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (normalized == renderer3D_Vulkan)
        return PresentationBackend::Vulkan;
#endif
    if (useGLConfig || RendererRequiresOpenGLContext(normalized))
        return PresentationBackend::OpenGL;
    return PresentationBackend::NativeQt;
}

#if defined(MELONPRIME_ENABLE_VULKAN)
bool IsVulkanPresentation(PresentationBackend backend)
{
    return backend == PresentationBackend::Vulkan;
}
#endif

const char* PresentationBackendName(PresentationBackend backend)
{
    switch (backend)
    {
    case PresentationBackend::NativeQt:
        return "NativeQt";
    case PresentationBackend::OpenGL:
        return "OpenGL";
#if defined(MELONPRIME_ENABLE_METAL)
    case PresentationBackend::Metal:
        return "Metal";
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case PresentationBackend::Vulkan:
        return "Vulkan";
#endif
    default:
        return "Unknown";
    }
}

const char* RendererName(int renderer)
{
    renderer = MigrateLegacyRendererId(renderer);
    switch (renderer)
    {
    case renderer3D_Software:
        return "Software";
#ifdef OGLRENDERER_ENABLED
    case renderer3D_OpenGL:
        return "OpenGL";
    case renderer3D_OpenGLCompute:
        return "OpenGLCompute";
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    case renderer3D_Metal:
        return "Metal";
    case renderer3D_MetalCompute:
        return "MetalCompute";
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    case renderer3D_Vulkan:
        return "Vulkan";
#endif
    default:
        return "Unavailable";
    }
}

bool IsOpenGLPresentation(PresentationBackend backend)
{
    return backend == PresentationBackend::OpenGL;
}

PresentationBackend FromLegacyOpenGLFlag(bool useOpenGL)
{
    return useOpenGL ? PresentationBackend::OpenGL : PresentationBackend::NativeQt;
}

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
