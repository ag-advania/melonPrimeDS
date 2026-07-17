// MelonPrimeDS - Metal screen presenter (experimental, Metal-plan Phase 4)
//
// Compiled only when MELONPRIME_METAL_ACTIVE selected MELONPRIME_ENABLE_METAL
// (see docs/plans/rendering/metal/backend-plan.md). Presents through a
// CAMetalLayer attached directly to this widget's native NSView (same "own
// the widget's native surface" model ScreenPanelGL already uses for its
// NSOpenGLContext).
//
// MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1 (PR-9): the DS top/bottom
// screens are sourced exclusively from the renderer-owned MetalTexture
// obtained through GPU::AcquireRendererOutputLease() -- there is no CPU
// framebuffer upload for DS screen content in this presenter at all. Custom
// HUD/OSD/splash still composite through a CPU QImage overlay (`uiOverlay`)
// blended over that texture; that is unrelated UI-layer content, not a DS
// screen fallback, and is expected to move onto Metal in PR-10..PR-12.
//
// MELONPRIME_METAL_RADAR_NATIVE_V1 (PR-10): the custom-HUD radar overlay
// (bottom-screen crop-circle drawn onto the top screen) is sampled natively
// in Metal from that same renderer-owned MetalTexture's bottom array layer,
// with a fragment-shader circle mask + palette filter mirroring the GL-native
// btmOverlay shader. There is no CPU bottom-screen buffer or memcpy anywhere
// on this path.
//
// MELONPRIME_METAL_OSD_SPLASH_NATIVE_V1 (PR-12): the splash logo/text and OSD
// toast items no longer composite into `uiOverlay` at all. Each item gets
// its own Metal texture (osdRenderItem() override below, mirroring
// ScreenPanelGL's per-item GL texture cache), drawn as an individual
// textured quad through the PR-11 HUD command list. Only the custom HUD
// (gauges/crosshair/etc. from MelonPrimeHudRender*) still rasterizes through
// QPainter into `uiOverlay`.
//
// Threading: initMetal()/setupScreenLayout() (layer creation, drawable size)
// run on the GUI thread. drawScreen() runs on the emu thread, matching
// ScreenPanelGL::drawScreen() (also called from the emu thread's
// RunFrameHook path, not a Qt slot) -- so the Objective-C rendering here
// follows the same thread as GL's MakeCurrent()/glDraw* calls today.

#ifndef MELONPRIME_SCREEN_METAL_H
#define MELONPRIME_SCREEN_METAL_H

#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#include <memory>

#include "Screen.h"

class ScreenPanelMetal final : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelMetal(QWidget* parent);
    ~ScreenPanelMetal() override;

    // GUI thread. Creates the MTLDevice/CAMetalLayer/pipeline/textures.
    // Returns false (nothing partially left listening) if
    // MelonPrime::Metal::SupportsRequiredBaseline() is false or any Metal
    // object fails to create; callers must fall back to
    // ScreenPanelGL/ScreenPanelNative in that case.
    bool initMetal();

    void drawScreen() override;

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    bool event(QEvent* event) override;

private:
    void setupScreenLayout() override;
    bool attachLayerToCurrentViewGuiThread();
    void updateDrawableSizeGuiThread();
    qreal devicePixelRatioFromScreenLocal() const;

    // MELONPRIME_METAL_OSD_SPLASH_NATIVE_V1 (PR-12): per-item Metal texture
    // cache for OSD/splash-text content, mirroring ScreenPanelGL's overrides
    // of the same two ScreenPanel virtuals.
    void osdRenderItem(OSDItem* item) override;
    void osdDeleteItem(OSDItem* item) override;

    struct Impl;
    std::unique_ptr<Impl> m;
};

#endif // Metal-enabled Apple build
#endif // MELONPRIME_SCREEN_METAL_H
