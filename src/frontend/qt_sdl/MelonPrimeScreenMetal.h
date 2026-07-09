// MelonPrimeDS - Metal screen presenter (experimental, Metal-plan Phase 4)
//
// Compiled only when MELONPRIME_METAL_ACTIVE selected MELONPRIME_ENABLE_METAL
// (see .claude/rules/melonprime-metal-backend-plan.md). Presentation only:
// uploads the same CPU BGRA framebuffers ScreenPanelNative composites via
// QPainter, but through a CAMetalLayer attached directly to this widget's
// native NSView (same "own the widget's native surface" model
// ScreenPanelGL already uses for its NSOpenGLContext). No 3D renderer
// integration, no OSD/HUD/splash yet -- those are later phases.
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

    struct Impl;
    std::unique_ptr<Impl> m;
};

#endif // Metal-enabled Apple build
#endif // MELONPRIME_SCREEN_METAL_H
