#ifndef MELON_PRIME_VIDEO_BACKEND_H
#define MELON_PRIME_VIDEO_BACKEND_H

#ifdef MELONPRIME_DS

namespace MelonPrime::VideoBackend {

    // Clamps a persisted/runtime `3D.Renderer` value to one that is safe to
    // construct on the current platform. macOS's OpenGL implementation does
    // not reliably support the OpenGL compute-shader renderer path (see
    // melonprime_macos_compute_renderer_restriction.md); a stale config value
    // copied from Windows/Linux, or a hand-edited TOML, must never reach the
    // renderer factory unmodified there. Unknown/out-of-range values clamp to
    // Software. On other platforms the input passes through unchanged (aside
    // from that same out-of-range clamp).
    int NormalizeRendererForPlatform(int requested);

    // Whether the given (already-normalized) `3D.Renderer` value needs an
    // OpenGL context/surface to run. Kept distinct from "is not Software" so a
    // future non-OpenGL backend cannot be mistaken for requiring a GL context.
    bool RendererRequiresOpenGLContext(int renderer);

    // Which Qt-visible presentation backend a window should use. Metal-plan
    // Phase 1 (melonprime-metal-backend-plan.md): a seam that both
    // MainWindow::createScreenPanel() and future Metal-aware call sites can
    // share, instead of duplicating the "UseGL || renderer != Software"
    // expression. Only NativeQt/OpenGL are actually reachable until Phase 4
    // adds ScreenPanelMetal and a presenter-selection config key.
    enum class PresentationBackend
    {
        NativeQt,
        OpenGL,
#if defined(MELONPRIME_ENABLE_VULKAN)
        Vulkan,
#endif
#if defined(MELONPRIME_ENABLE_METAL)
        Metal,
#endif
    };

    // `useGLConfig` / `requestedRenderer` are the raw `Screen.UseGL` /
    // `3D.Renderer` config values, not yet normalized -- this normalizes
    // `requestedRenderer` internally via NormalizeRendererForPlatform().
    PresentationBackend ResolvePresentationBackend(bool useGLConfig, int requestedRenderer);

    bool IsOpenGLPresentation(PresentationBackend backend);

    // Metal-plan Phase 3: bridges EmuThread's legacy 2-state `useOpenGL` bool
    // into the enum at the sites that still only compute a bool. Can only
    // ever return NativeQt or OpenGL (a bool cannot represent Metal) --
    // callers that need to actually select Metal must resolve
    // PresentationBackend directly instead of going through this bridge.
    PresentationBackend FromLegacyOpenGLFlag(bool useOpenGL);

#if defined(MELONPRIME_ENABLE_METAL)
    // Metal-plan Phase 4 bootstrap only: there is no persisted config key or
    // UI for selecting the Metal presenter yet (that is Phase 9, once
    // Phase 7 gives 3D.Renderer a real renderer3D_Metal value to route
    // through like every other backend). Until then, the presenter can only
    // be selected for local testing via the MELONPRIME_FORCE_METAL_PRESENTER
    // environment variable ("1" to force it). Never read outside a
    // MELONPRIME_ENABLE_METAL build; never persisted; never exposed in any
    // UI. Delete this function (and its call site in
    // ResolvePresentationBackend) once Phase 9 lands.
    bool ShouldForceMetalPresenterFromEnv();
    bool ShouldForceMetalRendererFromEnv();
#endif

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
#endif // MELON_PRIME_VIDEO_BACKEND_H
