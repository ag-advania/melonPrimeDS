// MelonPrimeDS - Vulkan screen presenter implementation. See MelonPrimeScreenVulkan.h for the
// design overview and known gaps.

#include "MelonPrimeScreenVulkan.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cmath>

#include "EmuInstance.h"
#include "Window.h"

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent) : ScreenPanel(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize(1));

    // MELONPRIME-PC-ADAPT: reference presentation is driven by the Android app's Choreographer
    // callback (see MelonPrimeScreenVulkan.h / presentTick() below); desktop has no equivalent, so
    // a fixed ~60Hz QTimer is the pacing mechanism instead (task-specified approach). This is a
    // CPU-side pacing cap only -- the swapchain's own present mode (FIFO, effectively always
    // vsync'd; see VulkanSurfacePresenter.h PresentMode default) is what actually governs
    // tearing/vsync behavior and is not reconfigurable from here (see applyCurrentLayout()'s
    // Screen.VSync note).
    presentTimer.setTimerType(Qt::PreciseTimer);
    connect(&presentTimer, &QTimer::timeout, this, &ScreenPanelVulkan::presentTick);
    presentTimer.start(16);
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    presentTimer.stop();
    detachIfAttached();
}

void ScreenPanelVulkan::attachIfNeeded()
{
    if (surfaceId > 0)
        return;
    if (!emuInstance || !emuInstance->melonVulkanInstance)
        return;
    if (winId() == 0)
        return;

    const qreal dpr = devicePixelRatioF();
    const melonDS::u32 w = static_cast<melonDS::u32>(
        std::max(1, static_cast<int>(std::lround(static_cast<qreal>(width()) * dpr))));
    const melonDS::u32 h = static_cast<melonDS::u32>(
        std::max(1, static_cast<int>(std::lround(static_cast<qreal>(height()) * dpr))));

    // MELONPRIME-PC-ADAPT: ANativeWindow* -> void* (HWND on Windows), matching the same
    // adaptation already made in attachVulkanSurface()'s own signature (MelonInstanceVulkan.h) and
    // in the verbatim-ported VulkanSurfacePresenter::attachSurface().
    surfaceId = emuInstance->melonVulkanInstance->attachVulkanSurface(
        reinterpret_cast<void*>(static_cast<uintptr_t>(winId())), w, h);

    if (surfaceId > 0)
        applyCurrentLayout();
}

void ScreenPanelVulkan::detachIfAttached()
{
    if (surfaceId > 0 && emuInstance && emuInstance->melonVulkanInstance)
        emuInstance->melonVulkanInstance->detachVulkanSurface(surfaceId);
    surfaceId = -1;
}

void ScreenPanelVulkan::applyCurrentLayout()
{
    if (surfaceId <= 0 || !emuInstance || !emuInstance->melonVulkanInstance)
        return;

    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<qreal>(width()) * dpr)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<qreal>(height()) * dpr)));

    // MELONPRIME-KNOWN-GAP: fixed vertical top/bottom split, not a real read of this fork's
    // ScreenLayout state (rotation, custom layouts, hybrid/swap, aspect ratio -- see
    // ScreenPanel::setupScreenLayout()/ScreenLayout.h, and ScreenPanelGL::transferLayout() for
    // what the full mapping would need to mirror). This gives a functional, if visually basic,
    // default so the surface renders correctly-positioned content immediately after attach/resize;
    // wiring the real ScreenLayout engine into VulkanSurfaceConfig's topScreen/bottomScreen
    // VulkanPresenterRects (and hybridTopScreen/hybridBottomScreen for hybrid layouts) is
    // follow-up work.
    MelonDSAndroid::VulkanSurfaceConfig config;
    // MELONPRIME-PC-ADAPT: preserve Sapphire's physical screen identities at the Win32 boundary.
    // Producer ownership and capture routing already expose topScreen/bottomScreen correctly;
    // swapping these destinations would invert Nintendo/ACTIMAGINE and the title/touch menu.
    config.topScreen.enabled = true;
    config.topScreen.x = 0;
    config.topScreen.y = 0;
    config.topScreen.width = w;
    config.topScreen.height = h / 2;
    config.bottomScreen.enabled = true;
    config.bottomScreen.x = 0;
    config.bottomScreen.y = h / 2;
    config.bottomScreen.width = w;
    config.bottomScreen.height = h - (h / 2);

    // MELONPRIME-KNOWN-GAP: "vsync from Screen.VSync" (W5 task brief) is not actually wirable here
    // -- VulkanSurfaceConfig/VulkanSurfacePresenter (verbatim-copied Sapphire files, never edited
    // per this port's fidelity rules) have no present-mode-selection field; the swapchain always
    // requests VK_PRESENT_MODE_FIFO_KHR (VulkanSurfacePresenter.h PresentMode default), which is
    // effectively always vsync'd regardless of the Screen.VSync config value. Reading
    // Screen.VSync to gate this CPU-side present timer's cadence would only mask, not implement,
    // real vsync toggling, so it is deliberately left unread here rather than given a misleading
    // effect.
    emuInstance->melonVulkanInstance->configureVulkanSurface(
        surfaceId, config, MelonDSAndroid::VulkanBackgroundImage{});
}

void ScreenPanelVulkan::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();

    attachIfNeeded();
    if (surfaceId <= 0 || !emuInstance || !emuInstance->melonVulkanInstance)
        return;

    const qreal dpr = devicePixelRatioF();
    const melonDS::u32 w = static_cast<melonDS::u32>(
        std::max(1, static_cast<int>(std::lround(static_cast<qreal>(width()) * dpr))));
    const melonDS::u32 h = static_cast<melonDS::u32>(
        std::max(1, static_cast<int>(std::lround(static_cast<qreal>(height()) * dpr))));
    emuInstance->melonVulkanInstance->resizeVulkanSurface(surfaceId, w, h);
    applyCurrentLayout();
}

bool ScreenPanelVulkan::event(QEvent* event)
{
    const bool handled = ScreenPanel::event(event);
    switch (event->type())
    {
    case QEvent::WinIdChange:
    case QEvent::Show:
    case QEvent::WindowStateChange:
        attachIfNeeded();
        break;
    case QEvent::Hide:
        detachIfAttached();
        break;
    default:
        break;
    }
    return handled;
}

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // Intentionally empty -- the Vulkan swapchain presents directly to this widget's native
    // surface (see class-level comment in the header); Qt must not paint over it.
}

void ScreenPanelVulkan::presentTick()
{
    // MELONPRIME-PC-ADAPT: on Android the MelonInstance exists before any surface callback can
    // fire, so attach never races the instance. Desktop inverts that ordering: this panel's
    // Show/WinIdChange events (the attach triggers) all fire during window construction, while
    // emuInstance->melonVulkanInstance is only created later by EmuThread::updateRenderer() once
    // emulation starts. Retry the attach from the present timer so the surface connects as soon
    // as the instance appears; without this the surface never attaches and nothing is presented.
    if (surfaceId <= 0)
        attachIfNeeded();
    if (surfaceId <= 0 || !emuInstance || !emuInstance->melonVulkanInstance)
        return;

    using clock = std::chrono::steady_clock;
    const auto now = clock::now();

    // MELONPRIME-PC-ADAPT: reference drives presentVulkanFrame() from the Android app's
    // Choreographer-computed absolute vsync deadline (JNI wrapper:
    // Java_me_magnum_melonds_MelonEmulator_presentVulkanFrame in MelonDSAndroidJNI.cpp, which
    // converts Choreographer's frameTimeNanos-derived deadline/budget into the same
    // optional<steady_clock::time_point> pair presentVulkanFrame() takes here; see
    // MelonDS.cpp:681 for the pass-through). Desktop has no Choreographer; this QTimer tick IS the
    // pacing mechanism (see ctor). `deadline` is approximated as "the next tick's target time"
    // (now + one timer interval) and `budgetDeadline` as half that -- giving presentVulkanFrame()
    // a bounded window to wait for a fresh frame before falling back to reusing the previous one,
    // matching the SHAPE of the reference's late/budget-deadline logic
    // (MelonInstance::presentVulkanFrame's isPresentationDeadlineExpired()/effectiveBudgetDeadline)
    // without claiming to reproduce Android's exact vsync-pacing values -- unverified against a
    // live build; see W5 report notes.
    const auto interval = std::chrono::milliseconds(std::max(1, presentTimer.interval()));
    const auto deadline = std::make_optional(now + interval);
    const auto budgetDeadline = std::make_optional(now + interval / 2);

    emuInstance->melonVulkanInstance->presentVulkanFrame(deadline, budgetDeadline);
}

#endif // defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
