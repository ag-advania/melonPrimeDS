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
#if defined(MELONPRIME_ENABLE_METAL)
        Metal,
#endif
    };

    // `useGLConfig` / `requestedRenderer` are the raw `Screen.UseGL` /
    // `3D.Renderer` config values, not yet normalized -- this normalizes
    // `requestedRenderer` internally via NormalizeRendererForPlatform().
    PresentationBackend ResolvePresentationBackend(bool useGLConfig, int requestedRenderer);

    bool IsOpenGLPresentation(PresentationBackend backend);

} // namespace MelonPrime::VideoBackend

#endif // MELONPRIME_DS
#endif // MELON_PRIME_VIDEO_BACKEND_H
