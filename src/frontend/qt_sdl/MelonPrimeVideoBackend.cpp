#ifdef MELONPRIME_DS

#include "MelonPrimeVideoBackend.h"
#include "EmuInstance.h" // renderer3D_* enum

#if defined(MELONPRIME_ENABLE_METAL)
#include <cstdlib>
#endif

namespace MelonPrime::VideoBackend {

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

int NormalizeRendererForPlatform(int requested)
{
#if defined(MELONPRIME_ENABLE_METAL)
    if (ShouldForceMetalRendererFromEnv())
        return renderer3D_Metal;

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
    default:
        return renderer3D_Software;
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

PresentationBackend ResolvePresentationBackend(bool useGLConfig, int requestedRenderer)
{
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

    const int normalized = NormalizeRendererForPlatform(requestedRenderer);
#if defined(MELONPRIME_ENABLE_METAL)
    if (normalized == renderer3D_Metal || normalized == renderer3D_MetalCompute)
        return PresentationBackend::Metal;
#endif
    if (useGLConfig || RendererRequiresOpenGLContext(normalized))
        return PresentationBackend::OpenGL;
    return PresentationBackend::NativeQt;
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
