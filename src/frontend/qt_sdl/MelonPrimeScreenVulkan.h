// MelonPrimeDS - Vulkan screen presenter (W5: Qt wiring for the ported Sapphire Vulkan pipeline)
//
// Compiled only when MELONPRIME_ENABLE_VULKAN is selected. Presentation-only, mirroring the
// "own the widget's native surface" model ScreenPanelGL (native GL surface) and ScreenPanelMetal
// (CAMetalLayer) already use: this panel owns a native window handle that
// MelonDSAndroid::MelonInstance's ported VulkanSurfacePresenter (src/frontend/qt_sdl/sapphire/
// renderer/VulkanSurfacePresenter.h/.cpp, verbatim-copied Sapphire code, never edited) attaches a
// swapchain to directly -- Qt never paints into this widget (paintEvent() is a no-op,
// paintEngine() returns null, matching ScreenPanelGL/ScreenPanelMetal's own null paintEngine()).
//
// Threading: attach/resize/detach and the present-timer tick all run on the GUI thread (matching
// ScreenPanelGL's createContext()/ScreenPanelMetal's attachLayerToCurrentViewGuiThread()); the emu
// thread never touches this panel directly. Emu-thread-side Vulkan work (the runFrameVulkanPreStep/
// PostStep split, the soft-packed latch) is driven from EmuThread.cpp instead (see
// EmuThread.cpp:444-465), not from here -- this panel is presentation-only.
//
// MELONPRIME-KNOWN-GAP: no MainWindow::createScreenPanel() wiring for renderer3D_Vulkan exists yet
// in the reference the way ScreenPanelGL/ScreenPanelMetal are wired -- see the W5 port report for
// the panel-selection call site this needs (MelonPrimeVideoBackend.h/.cpp
// PresentationBackend::Vulkan + Window.cpp createScreenPanel()). This class is self-contained and
// syntactically complete but has not been exercised against a live Qt/moc build (no Qt toolchain
// available in this port session's probe environment) -- see the W5 report notes.

#ifndef MELONPRIME_SCREEN_VULKAN_H
#define MELONPRIME_SCREEN_VULKAN_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <chrono>

#include <QTimer>

#include "Screen.h"

class ScreenPanelVulkan final : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;

    // Presentation is driven entirely by the present timer (presentTick()), not by Qt's normal
    // paint/update cycle -- matches ScreenPanelMetal::drawScreen() being effectively a no-op
    // trigger point (MainWindow::drawScreen() still calls panel->drawScreen() generically for
    // every panel type; nothing else in this port depends on it doing real work here).
    void drawScreen() override {}

protected:
    // Native surface owns its own pixels; Qt must never paint into or erase this widget (same
    // reasoning as ScreenPanelGL::paintEngine()/ScreenPanelMetal::paintEngine()).
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent* event) override;

    // Catches WinIdChange/Show/WindowStateChange (attach) and Hide (detach), mirroring
    // ScreenPanelMetal::event()'s pattern for the same native-surface-lifecycle problem.
    bool event(QEvent* event) override;

private:
    void setupScreenLayout() override;

    void attachIfNeeded();
    void detachIfAttached();
    void applyCurrentLayout();

private slots:
    void presentTick();

private:
    int surfaceId = -1;
    QTimer presentTimer;
};

#endif // defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#endif // MELONPRIME_SCREEN_VULKAN_H
