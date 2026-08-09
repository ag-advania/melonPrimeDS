/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// ScreenPanelVulkan -- the Qt seam for the native Vulkan presentation path.
//
// The class is declared in Screen.h next to its siblings, but its body lives
// here rather than in Screen.cpp: Screen.cpp is upstream-owned, and this panel
// is entirely MelonPrime-owned (Vulkan presenter, Custom HUD compositing,
// Wayland pointer lock, renderer-transition quiescing). Nothing in Screen.cpp
// had to change for it.
//
// Threading, stated once because everything below depends on it:
//
//   GUI thread          constructs and destroys the panel, owns every QWidget
//                       operation, creates the native child surface, and calls
//                       initVulkan() before the panel is published to
//                       MainWindow::panel.
//   Emulation thread    calls drawScreen() (once per emulated frame, and every
//                       ~75 ms while paused) and the VBlank observer. It is the
//                       only thread that records and submits Vulkan work.
//
// The two never touch a Vulkan object at the same time: the panel is not
// reachable from the emulation thread until initVulkan() has returned, and it
// is unpublished under MainWindow::screenPanelLock before the destructor runs.
// GUI-thread resize / DPI / fullscreen handling therefore only sets flags, and
// the swapchain is rebuilt on the emulation thread at the next frame boundary.

#include "Screen.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include <QApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPaintEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)  // scatter-budget-exempt: Wayland pointer lock, the same override ScreenPanelGL/ScreenPanelNative already carry in Screen.cpp; not new input dispatch, only the same gate in the panel's own translation unit
#include <optional>
#include <utility>
#include <qpa/qplatformnativeinterface.h>
#include "MelonPrimeWaylandPointerLock.h"
#endif

#include "Config.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "GPU.h"
#include "GPU_Vulkan.h"
#include "MelonPrime.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePerfProbe.h"
#include "VulkanPerf.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanPresenter.h"
#include "MelonPrimeVulkanSurface.h"
#include "NDS.h"
#include "Platform.h"
#include "main.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
// Canonical Custom HUD config keys. Included rather than mirrored as string
// literals, per the config-key ownership rule; this translation unit is
// registered in tools/ci/audits/check-inc-ownership.ps1's multi-parent map
// alongside the other consumers.
#include "MelonPrimeHudPropSchema.inc"
#endif

using namespace melonDS;

namespace
{

// Mirrors the values Screen.cpp uses for the same purpose. They are layout
// constants of the shared OSD/splash drawing, not configuration, and the two
// panels must place the OSD identically.
constexpr int kOSDMarginPx = 6;
constexpr int kSplashLogoWidth = 192;

// Presentation stall watchdog threshold, in consecutive emulated frames the
// panel was supposed to present and could not. Roughly five seconds: long
// enough that a swapchain rebuild or a renderer transition never trips it,
// short enough to appear in a log captured while wondering why the window
// stopped updating.
constexpr u32 kPresentationStallFrames = 300;

// Live ScreenPanelVulkan instances.
//
// PrepareForInstanceRendererTransition() and the VBlank observer are both
// entered from outside the panel (EmuThread, and the renderer on the emulation
// thread) with only an EmuInstance to go on, so the mapping has to exist
// somewhere. A flat list is right: there is one panel per window and at most a
// handful of windows.
QMutex g_panelRegistryLock;
std::vector<ScreenPanelVulkan*> g_panelRegistry;

// The native child window the swapchain presents into.
//
// A dedicated child rather than the panel itself: the panel keeps Qt painting
// (splash screen, and the fallback when Vulkan is unavailable), while this
// widget is pure presentation surface -- no Qt paint engine, no system
// background, and transparent to the mouse so the panel keeps receiving every
// input event exactly as before.
class VulkanSurfaceHost final : public QWidget
{
public:
    explicit VulkanSurfaceHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAutoFillBackground(false);
    }

    // Returning nullptr tells Qt this widget is painted by something other than
    // the Qt paint system, which is what stops Qt from clearing the surface
    // underneath the swapchain.
    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void paintEvent(QPaintEvent*) override {}
};

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)  // scatter-budget-exempt: Wayland pointer lock, the same override ScreenPanelGL/ScreenPanelNative already carry in Screen.cpp; not new input dispatch, only the same gate in the panel's own translation unit
// Local copy of Screen.cpp's helper. It cannot be shared: it lives in that
// file's internal linkage, and the unity fragments under src/frontend/qt_sdl
// are each owned by exactly one translation unit.
std::optional<std::pair<void*, void*>> ResolveWaylandHandles(QWindow* handle)
{
    if (!handle || QGuiApplication::platformName() != QStringLiteral("wayland"))
        return std::nullopt;

    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (!pni)
        return std::nullopt;

    void* display = pni->nativeResourceForWindow("display", handle);
    void* const surface = pni->nativeResourceForWindow("surface", handle);
    if (!display || !surface)
        return std::nullopt;
    return std::make_pair(display, surface);
}
#endif

} // namespace


// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct ScreenPanelVulkan::VulkanState
{
    MelonPrime::VulkanPresenter presenter;
    QPointer<VulkanSurfaceHost> surface;

    // Screen layout, published by the GUI thread's setupScreenLayout() and read
    // by the emulation thread's draw pass.
    QMutex layoutLock;
    float screenMatrix[kMaxScreenTransforms][6]{};
    int screenKind[kMaxScreenTransforms]{};
    int numScreens = 0;

    // The composed frame, captured at the renderer's VBlank.
    //
    // GetOutput() hands out pointers into a surface the *next* VBlank
    // overwrites, and the renderer can be destroyed under us on a renderer
    // switch, so the panel latches them here and drops them in
    // prepareForRendererTransition() instead of dereferencing a borrowed
    // pointer at an arbitrary later moment.
    QMutex frameLock;
    const u32* frameTop = nullptr;
    const u32* frameBottom = nullptr;
    u32 frameWidth = 0;
    u32 frameHeight = 0;
    bool frameValid = false;

    // One renderer-output lease per presenter frame slot. BeginFrame() waits
    // that slot's fence before it is replaced, so a compositor buffer remains
    // immutable until the GPU has finished copying both screens from it.
    std::array<RendererOutputLease, Vk::FramesInFlight> frameLeases;

    // Renderer the VBlank observer is currently installed on, so the hook is
    // (re)installed exactly once per renderer instance.
    melonDS::VulkanRenderer* hookedRenderer = nullptr;

    QImage osdStrip;

    std::atomic_bool surfaceVisibleRequested{false};
    std::atomic_bool hudEditLivePresentation{false};
    std::atomic_bool modalPauseActive{false};
    std::atomic_bool surfaceHandleDirty{true};

    // The values the emulation thread most recently asked for. The presenter
    // is the authority once it exists; these are the panel-side record, read by
    // initVulkanPresenter() so a presenter created mid-session (renderer switch,
    // native handle change) starts at the current setting instead of off.
    std::atomic_int reflexMode{0};
    std::atomic_bool antiLag2Enabled{false};

    bool initialized = false;
    bool runtimeFailureReported = false;
    bool vsyncApplied = true;
    unsigned long long lastNativeHandle = 0;

    // Presentation stall watchdog. Emulation thread only, hence plain members.
    //
    // drawScreenFrame() has several legitimate reasons to skip a frame (no ROM,
    // paused, a modal dialog holding the surface), so logging every skip would
    // be noise and logging none leaves a dead presenter indistinguishable from
    // an idle one. This counts only consecutive frames the panel was supposed
    // to present and could not, and names the reason exactly once.
    const char* stallReason = nullptr;
    u32 stallFrames = 0;
    bool stallReported = false;
};


// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent), vulkan(std::make_unique<VulkanState>())
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    // The factor is passed explicitly: ScreenPanel::screenGetMinSize()'s default
    // argument is written on its definition in Screen.cpp, so it is not visible
    // to this translation unit.
    setMinimumSize(screenGetMinSize(1));

    vulkan->surface = new VulkanSurfaceHost(this);
    vulkan->surface->setGeometry(rect());
    vulkan->surface->hide();

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)  // scatter-budget-exempt: Wayland pointer lock, the same override ScreenPanelGL/ScreenPanelNative already carry in Screen.cpp; not new input dispatch, only the same gate in the panel's own translation unit
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>(
        [this](std::int32_t dx, std::int32_t dy) {
            addAimMouseDeltaForMelonPrime(dx, dy);
        });
#endif

    {
        QMutexLocker lock(&g_panelRegistryLock);
        g_panelRegistry.push_back(this);
    }
}


ScreenPanelVulkan::~ScreenPanelVulkan()
{
    {
        QMutexLocker lock(&g_panelRegistryLock);
        g_panelRegistry.erase(
            std::remove(g_panelRegistry.begin(), g_panelRegistry.end(), this),
            g_panelRegistry.end());
    }

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)  // scatter-budget-exempt: Wayland pointer lock, the same override ScreenPanelGL/ScreenPanelNative already carry in Screen.cpp; not new input dispatch, only the same gate in the panel's own translation unit
    if (waylandPointerLock)
        waylandPointerLock->setLocked(nullptr, nullptr, false);
#endif

    // The emulation thread can no longer reach this panel: MainWindow cleared
    // its `panel` pointer under screenPanelLock before deleting it. Tearing the
    // presenter down here is therefore the last Vulkan work on any thread.
    prepareForRendererTransition();
    releaseNativeSurface();
}


// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool ScreenPanelVulkan::initVulkan()
{
    if (!vulkan || !vulkan->surface)
        return false;

    if (!MelonPrime::VulkanFeatureCheck::IsRuntimeAvailable())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] presentation panel refused: %s\n",
            MelonPrime::VulkanFeatureCheck::Probe().Reason.c_str());
        return false;
    }

    // Realizes the child window so the platform adapter has a native handle to
    // build a VkSurfaceKHR from.
    vulkan->surface->setGeometry(rect());
    vulkan->surface->winId();

    if (!initVulkanPresenter())
        return false;

    vulkan->initialized = true;
    refreshNativeSurfaceGuiThread();
    return true;
}


bool ScreenPanelVulkan::initVulkanPresenter()
{
    if (!vulkan || !vulkan->surface)
        return false;
    if (vulkan->presenter.IsInitialized())
        return true;
    if (!nativeSurfaceReady())
        return false;

    // Before Init(), not after: the present mode is baked into the swapchain,
    // so applying the setting afterwards would build one swapchain with the
    // default mode and immediately throw it away -- and the startup log line
    // would claim a VSync state the user did not ask for.
    Config::Table config = emuInstance->getGlobalConfig();
    vulkan->vsyncApplied = config.GetBool("Screen.VSync");
    vulkan->presenter.SetVSync(vulkan->vsyncApplied);
    vulkan->presenter.SetWindowFullscreen(window() && window()->isFullScreen());

    const int reflexMode = config.GetInt(MelonPrime::CfgKey::NvidiaReflexMode);
    const bool antiLag2Enabled = config.GetBool(MelonPrime::CfgKey::AmdAntiLag2Enabled);
    vulkan->reflexMode.store(reflexMode, std::memory_order_relaxed);
    vulkan->antiLag2Enabled.store(antiLag2Enabled, std::memory_order_relaxed);

    if (!vulkan->presenter.Init(vulkan->surface.data()))
    {
        const std::string reason = vulkan->presenter.LastError();
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] presenter initialization failed reason=%s\n",
            reason.empty() ? "unspecified" : reason.c_str());
        MelonPrime::VulkanFeatureCheck::ReportRuntimeFailure(
            reason.empty() ? std::string("the Vulkan presenter could not be initialized") : reason);
        return false;
    }

    // Init creates the extension modules and the first swapchain. Apply the
    // saved preferences before the first effective-state diagnostic so it
    // never reports the presenter's default Off state as the user's request.
    vulkan->presenter.SetLowLatencyPreferences(reflexMode, antiLag2Enabled);

    vulkan->lastNativeHandle = static_cast<unsigned long long>(vulkan->surface->winId());
    vulkan->surfaceHandleDirty.store(false, std::memory_order_release);

    // Requested / Supported / device-extension-enabled / Actual / Reason for both vendor
    // extensions, once, as soon as there is a device to report on. Emitted here
    // rather than inside Init() so it is also printed for a presenter rebuilt
    // mid-session -- a renderer switch or a new native window handle -- where
    // the answer can legitimately differ from the one at startup.
    vulkan->presenter.LogLowLatencyState("presenter ready:");
    return true;
}


void ScreenPanelVulkan::reportVulkanRuntimeFailure(const char* reason)
{
    if (!vulkan || vulkan->runtimeFailureReported)
        return;
    vulkan->runtimeFailureReported = true;

    MelonPrime::VulkanFeatureCheck::ReportRuntimeFailure(
        reason ? reason : "Vulkan presentation failed");

    requestNativeSurfaceVisible(false);

    if (auto* emuThread = emuInstance->getEmuThread())
    {
        QMetaObject::invokeMethod(
            emuThread,
            [emuThread]() { emit emuThread->rendererRuntimeFallback(); },
            Qt::QueuedConnection);
    }
}


// ---------------------------------------------------------------------------
// Native surface plumbing
// ---------------------------------------------------------------------------

bool ScreenPanelVulkan::nativeSurfaceReady() const
{
    return vulkan && vulkan->surface && vulkan->surface->windowHandle() != nullptr;
}


void ScreenPanelVulkan::refreshNativeSurfaceGuiThread()
{
    if (!vulkan || !vulkan->surface)
        return;

    vulkan->surface->setGeometry(rect());
    // macOS is the only platform where the presentation layer has a size of its
    // own; everywhere else this is a no-op and the window system resizes the
    // surface with the native window.
    MelonPrime::VulkanSurface::UpdateGeometry(
        vulkan->presenter.GetPlatformSurface(), vulkan->surface.data());
    vulkan->presenter.SetWindowFullscreen(window() && window()->isFullScreen());
    vulkan->presenter.NotifySurfaceChanged();
}


void ScreenPanelVulkan::setNativeSurfaceVisibleGuiThread(bool visible)
{
    if (!vulkan || !vulkan->surface)
        return;

    if (visible)
    {
        vulkan->surface->setGeometry(rect());
        vulkan->surface->show();
        vulkan->surface->raise();
    }
    else
    {
        vulkan->surface->hide();
        update();
    }
}


void ScreenPanelVulkan::requestNativeSurfaceVisible(bool visible)
{
    if (!vulkan)
        return;
    // Only a real state change is posted: the emulation thread calls this every
    // frame, and queueing a GUI-thread lambda per frame would be a pure waste.
    if (vulkan->surfaceVisibleRequested.exchange(visible) == visible)
        return;

    QMetaObject::invokeMethod(
        this,
        [this, visible]() { setNativeSurfaceVisibleGuiThread(visible); },
        Qt::QueuedConnection);
}


void ScreenPanelVulkan::releaseNativeSurface()
{
    if (!vulkan)
        return;

    for (RendererOutputLease& lease : vulkan->frameLeases)
        lease.ReleaseNow();
    vulkan->presenter.Shutdown();
    vulkan->initialized = false;
    vulkan->surfaceHandleDirty.store(true, std::memory_order_release);

    if (vulkan->surface)
    {
        vulkan->surface->hide();
        vulkan->surface->deleteLater();
        vulkan->surface = nullptr;
    }
}


#if defined(__linux__)  // scatter-budget-exempt: presentation-surface rebinding when a Wayland/XWayland compositor hands out a new native window; not input dispatch
bool ScreenPanelVulkan::prepareLinuxPresentationSurface()
{
    if (!vulkan || !vulkan->surface)
        return false;
    if (!nativeSurfaceReady())
        return false;

    const unsigned long long handle =
        static_cast<unsigned long long>(vulkan->surface->winId());
    if (handle == 0)
        return false;

    // A compositor (or a Qt reparent, which XWayland and Wayland both perform
    // on a fullscreen transition) can hand out a new native window for the same
    // QWidget. The old VkSurfaceKHR then refers to a window that no longer
    // exists, and every acquire returns VK_ERROR_SURFACE_LOST_KHR, so the whole
    // presenter -- surface included -- is rebuilt rather than the swapchain
    // alone.
    if (vulkan->presenter.IsInitialized() && handle != vulkan->lastNativeHandle)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] native window handle changed (%llu -> %llu); rebuilding the presenter\n",
            vulkan->lastNativeHandle,
            handle);
        for (RendererOutputLease& lease : vulkan->frameLeases)
            lease.ReleaseNow();
        vulkan->presenter.Shutdown();
        vulkan->lastNativeHandle = handle;
    }

    return initVulkanPresenter();
}
#endif


// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    if (!vulkan)
        return;

    QMutexLocker lock(&vulkan->layoutLock);
    vulkan->numScreens = numScreens;
    for (int index = 0; index < numScreens && index < kMaxScreenTransforms; ++index)
    {
        std::memcpy(vulkan->screenMatrix[index], screenMatrix[index], sizeof(float) * 6);
        vulkan->screenKind[index] = screenKind[index];
    }
}


void ScreenPanelVulkan::resizeEvent(QResizeEvent* event)
{
    if (vulkan && vulkan->surface)
        vulkan->surface->setGeometry(rect());

    // Base class first: it recomputes the screen layout, which the draw pass
    // reads under vulkan->layoutLock.
    ScreenPanel::resizeEvent(event);

    if (vulkan)
    {
        // Coalesced, not immediate. Rebuilding here would mean one swapchain
        // per resize event during a window drag, and the GUI thread must not
        // destroy a swapchain the emulation thread may be presenting from.
        vulkan->presenter.NotifySurfaceChanged();
    }
}


bool ScreenPanelVulkan::event(QEvent* event)
{
    if (vulkan && event)
    {
        switch (event->type())
        {
        case QEvent::PlatformSurface:
        {
            const auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
            if (surfaceEvent->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            {
                // The VkSurfaceKHR must not outlive the native window it wraps.
                releaseNativeSurface();
            }
            else
            {
                vulkan->surfaceHandleDirty.store(true, std::memory_order_release);
            }
            break;
        }

        case QEvent::ScreenChangeInternal:
        case QEvent::WindowStateChange:
        case QEvent::Show:
            // A move to another monitor, a fullscreen transition or a DPI
            // change all alter the surface's physical pixel size without
            // necessarily producing a resize event first.
            vulkan->presenter.SetWindowFullscreen(window() && window()->isFullScreen());
            vulkan->presenter.NotifySurfaceChanged();
            QMetaObject::invokeMethod(
                this, [this]() { refreshNativeSurfaceGuiThread(); }, Qt::QueuedConnection);
            break;

        default:
            break;
        }
    }

    return ScreenPanel::event(event);
}


// ---------------------------------------------------------------------------
// Modal pause / HUD edit mode
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::beginModalPausePresentation()
{
    if (!vulkan)
        return;

    // A modal dialog pauses emulation, but the emulation thread keeps calling
    // drawScreen() every ~75 ms. Presenting through that would repeatedly
    // rebuild and re-present a frame nobody can see behind the dialog, and --
    // on the paths where the dialog changes the renderer or the video backend
    // -- would race the teardown that follows. The swapchain simply keeps
    // showing the last presented frame instead.
    vulkan->modalPauseActive.store(true, std::memory_order_release);
}


void ScreenPanelVulkan::endModalPausePresentation()
{
    if (!vulkan)
        return;
    vulkan->modalPauseActive.store(false, std::memory_order_release);
    vulkan->presenter.NotifySurfaceChanged();
}


#ifdef MELONPRIME_CUSTOM_HUD
void ScreenPanelVulkan::setHudEditModeActive(bool active)
{
    ScreenPanel::setHudEditModeActive(active);
    if (!vulkan)
        return;

    // The on-screen HUD editor runs while the settings dialog holds emulation
    // paused, so the paused draw pass is the only thing that can put the editor
    // overlay on screen. This flag re-enables presentation that
    // beginModalPausePresentation() suspended, for the duration of the editor
    // session only.
    vulkan->hudEditLivePresentation.store(active, std::memory_order_release);
}
#endif


// ---------------------------------------------------------------------------
// Low-latency hooks
//
// Thin forwarding to the presenter, which owns the swapchain and therefore
// owns both vendor extensions. These run on the emulation thread, the same
// thread that later calls drawScreenFrame(), so the presenter sees one
// consistent sequence per frame and no locking is needed.
//
// The presenter is only touched once it is initialized; a panel that is still
// coming up, or one whose presenter was torn down by a renderer switch, simply
// drops the frame's markers rather than opening a Reflex frame that could never
// be closed.
//
// The RENDERSUBMIT_* and PRESENT_* markers are NOT here on purpose: they sit
// around the real vkQueueSubmit / vkQueuePresentKHR inside
// VulkanPresenter::EndFrame(). Emitting them from this thread would time Qt
// bookkeeping instead of GPU work.
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::beginVulkanLowLatencyFrame(int reflexMode, bool antiLag2Enabled)
{
    if (!vulkan)
        return;

    // Kept for the panel's own diagnostics and so a presenter created later in
    // this session starts from the current setting rather than a stale one.
    vulkan->reflexMode.store(reflexMode, std::memory_order_relaxed);
    vulkan->antiLag2Enabled.store(antiLag2Enabled, std::memory_order_relaxed);

    if (!vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.BeginLowLatencyFrame(reflexMode, antiLag2Enabled);
}

void ScreenPanelVulkan::markVulkanReflexInputSample()
{
    if (!vulkan || !vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.MarkLowLatencyInputSample();
}

void ScreenPanelVulkan::markVulkanReflexSimulationStart()
{
    if (!vulkan || !vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.MarkLowLatencySimulationStart();
}

void ScreenPanelVulkan::markVulkanReflexSimulationEnd()
{
    if (!vulkan || !vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.MarkLowLatencySimulationEnd();
}

void ScreenPanelVulkan::finishVulkanLowLatencyFrame()
{
    if (!vulkan || !vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.FinishLowLatencyFrame();
}


// ---------------------------------------------------------------------------
// Renderer transitions
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::composeFrameAtVBlank()
{
    if (!vulkan)
        return;

    auto* nds = emuInstance ? emuInstance->getNDS() : nullptr;
    if (!nds)
        return;

    const RendererOutput output = nds->GPU.GetRendererOutput();

    QMutexLocker lock(&vulkan->frameLock);
    if (output.Kind != RendererOutputKind::CpuBgra || !output.Top || !output.Bottom
        || output.Width == 0 || output.Height == 0)
    {
        vulkan->frameValid = false;
        return;
    }

    vulkan->frameTop = static_cast<const u32*>(output.Top);
    vulkan->frameBottom = static_cast<const u32*>(output.Bottom);
    vulkan->frameWidth = output.Width;
    vulkan->frameHeight = output.Height;
    vulkan->frameValid = true;
}


void ScreenPanelVulkan::ComposeInstanceFrameAtVBlank(EmuInstance* instance)
{
    QMutexLocker lock(&g_panelRegistryLock);
    for (ScreenPanelVulkan* panel : g_panelRegistry)
    {
        if (panel->emuInstance == instance)
            panel->composeFrameAtVBlank();
    }
}


void ScreenPanelVulkan::installVulkanComposeHook(melonDS::VulkanRenderer* renderer)
{
    if (!vulkan || vulkan->hookedRenderer == renderer)
        return;

    vulkan->hookedRenderer = renderer;
    if (!renderer)
        return;

    // A captureless lambda so this stays a plain function pointer: the observer
    // is called once per DS frame from VBlank() and must not allocate.
    renderer->SetVBlankObserver(
        [](void* userData) {
            ComposeInstanceFrameAtVBlank(static_cast<EmuInstance*>(userData));
        },
        emuInstance);
}


void ScreenPanelVulkan::prepareForRendererTransition()
{
    if (!vulkan)
        return;

    // Drop the borrowed composed-frame pointers *before* the renderer that owns
    // them is destroyed, and take the observer back off it. After this the panel
    // presents black (or the splash) until a new renderer publishes a frame,
    // which is correct: there is no frame to show.
    if (vulkan->hookedRenderer)
    {
        vulkan->hookedRenderer->SetVBlankObserver(nullptr, nullptr);
        vulkan->hookedRenderer = nullptr;
    }

    vulkan->presenter.Quiesce();
    for (RendererOutputLease& lease : vulkan->frameLeases)
        lease.ReleaseNow();

    QMutexLocker lock(&vulkan->frameLock);
    vulkan->frameTop = nullptr;
    vulkan->frameBottom = nullptr;
    vulkan->frameWidth = 0;
    vulkan->frameHeight = 0;
    vulkan->frameValid = false;
}


void ScreenPanelVulkan::PrepareForInstanceRendererTransition(EmuInstance* instance)
{
    QMutexLocker lock(&g_panelRegistryLock);
    for (ScreenPanelVulkan* panel : g_panelRegistry)
    {
        if (panel->emuInstance == instance)
            panel->prepareForRendererTransition();
    }
}


// ---------------------------------------------------------------------------
// Wayland pointer lock
// ---------------------------------------------------------------------------

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)  // scatter-budget-exempt: Wayland pointer lock, the same override ScreenPanelGL/ScreenPanelNative already carry in Screen.cpp; not new input dispatch, only the same gate in the panel's own translation unit
bool ScreenPanelVulkan::setWaylandPointerLockForMelonPrime(bool enabled)
{
    if (!waylandPointerLock)
        return false;

    if (!enabled)
        return waylandPointerLock->setLocked(nullptr, nullptr, false);

    // The top-level window's surface, not this panel's native presentation
    // child: locking a child surface made KWin fire WindowDeactivate on the
    // main window in windowed mode, which the aim path reads as focus loss and
    // immediately unlocks again (issue #526).
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveWaylandHandles(topLevelHandle);
    if (!handles.has_value())
        return false;

    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    return waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
}


bool ScreenPanelVulkan::isWaylandPointerLockActiveForMelonPrime() const
{
    return waylandPointerLock && waylandPointerLock->isLockActive();
}
#endif


// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::drawScreen()
{
    refreshClipForGameStateChange();

    // macOS needs an @autorelease boundary around the whole frame; every other
    // platform calls straight through.
    MelonPrime::VulkanSurface::RunFrameInPlatformScope(
        [](void* context) { static_cast<ScreenPanelVulkan*>(context)->drawScreenFrame(); },
        this);
}


// Presentation stall watchdog.
//
// drawScreenFrame() has a dozen early returns and every one of them used to be
// silent, which left "Vulkan came up and then stopped presenting"
// indistinguishable from "Vulkan is deliberately idle" in a log -- the two
// states differ only in whether anything is wrong. These three helpers give
// every exit from drawScreenFrame() a recorded outcome while costing at most
// one log line per stall rather than one per frame.
//
// Emulation thread only.

void ScreenPanelVulkan::noteFrameIdle()
{
    // A skip the panel is *supposed* to perform: no ROM, paused, or a modal
    // dialog holding the surface. Nothing is wrong, so the watchdog is armed
    // afresh rather than advanced.
    if (!vulkan)
        return;
    vulkan->stallReason = nullptr;
    vulkan->stallFrames = 0;
    vulkan->stallReported = false;
}


void ScreenPanelVulkan::noteFrameStalled(const char* reason)
{
    if (!vulkan)
        return;

    // Reason strings are literals, so pointer identity is the comparison: a
    // different reason restarts the count instead of accumulating across two
    // unrelated conditions.
    if (vulkan->stallReason != reason)
    {
        vulkan->stallReason = reason;
        vulkan->stallFrames = 0;
        vulkan->stallReported = false;
    }

    // Counted even after the warning has been issued, so the recovery line can
    // state how long the stall actually lasted rather than the threshold.
    vulkan->stallFrames++;

    if (vulkan->stallReported)
        return;
    if (vulkan->stallFrames < kPresentationStallFrames)
        return;

    vulkan->stallReported = true;
    Platform::Log(
        Platform::LogLevel::Warn,
        "[Vulkan] no frame presented for %u consecutive frames: %s\n",
        vulkan->stallFrames,
        reason);
}


void ScreenPanelVulkan::noteFramePresented()
{
    if (!vulkan)
        return;

    if (vulkan->stallReported)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] presentation resumed after %u skipped frames\n",
            vulkan->stallFrames);
    }
    vulkan->stallReason = nullptr;
    vulkan->stallFrames = 0;
    vulkan->stallReported = false;
}


void ScreenPanelVulkan::drawScreenFrame()
{
    if (!vulkan)
        return;

#if defined(__linux__)  // scatter-budget-exempt: presentation-surface rebinding on Linux only; every other platform keeps a stable native handle
    // Re-binds the presenter when the compositor handed out a new native
    // window; a no-op once the handle is stable.
    if (vulkan->surfaceHandleDirty.exchange(false, std::memory_order_acq_rel)
        || !vulkan->presenter.IsInitialized())
    {
        if (!prepareLinuxPresentationSurface())
        {
            noteFrameStalled("the presentation surface could not be bound to the native window");
            return;
        }
    }
#else
    if (!vulkan->presenter.IsInitialized())
    {
        noteFrameStalled("the Vulkan presenter is not initialized");
        return;
    }
#endif

    auto* emuThread = emuInstance->getEmuThread();
    if (!emuThread)
    {
        noteFrameStalled("the emulation thread is gone");
        return;
    }

    if (!emuThread->emuIsActive())
    {
        // No ROM: the splash screen is Qt-drawn (it composites a QPixmap, which
        // is GUI-thread-only), so the native surface steps aside and the
        // panel's own paintEvent takes over.
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        noteFrameIdle();
        return;
    }

#ifdef MELONPRIME_CUSTOM_HUD
    const bool hudEditLivePresentation =
        vulkan->hudEditLivePresentation.load(std::memory_order_acquire);
#else
    constexpr bool hudEditLivePresentation = false;
#endif

    if (vulkan->modalPauseActive.load(std::memory_order_acquire) && !hudEditLivePresentation)
    {
        noteFrameIdle();
        return;
    }
    if (!emuThread->emuIsRunning() && !hudEditLivePresentation)
    {
        noteFrameIdle();
        return;
    }

    auto* nds = emuInstance->getNDS();
    if (!nds)
    {
        noteFrameStalled("the emulated console is gone");
        return;
    }

    // Keeps the VBlank observer bound to the live renderer across every
    // renderer switch, including switches away from and back to Vulkan.
    auto* vulkanRenderer = dynamic_cast<melonDS::VulkanRenderer*>(&nds->GPU.GetRenderer());
    installVulkanComposeHook(vulkanRenderer);

    const bool vsync = emuInstance->getGlobalConfig().GetBool("Screen.VSync");
    if (vsync != vulkan->vsyncApplied)
    {
        vulkan->vsyncApplied = vsync;
        vulkan->presenter.SetVSync(vsync);
    }

    RendererOutputLease rendererOutputLease = nds->GPU.AcquireRendererOutputLease();
    const RendererOutput& rendererOutput = rendererOutputLease.Output;
    const VulkanPresentedFrame* gpuFrame = nullptr;
    const u32* topPixels = nullptr;
    const u32* bottomPixels = nullptr;
    u32 sourceWidth = 0;
    u32 sourceHeight = 0;
    if (rendererOutput.Kind == RendererOutputKind::VulkanBuffer && rendererOutput.Top)
    {
        gpuFrame = static_cast<const VulkanPresentedFrame*>(rendererOutput.Top);
        sourceWidth = rendererOutput.Width;
        sourceHeight = rendererOutput.Height;
    }
    else if (rendererOutput.Kind == RendererOutputKind::CpuBgra
        && rendererOutput.Top && rendererOutput.Bottom
        && rendererOutput.Width > 0 && rendererOutput.Height > 0)
    {
        topPixels = static_cast<const u32*>(rendererOutput.Top);
        bottomPixels = static_cast<const u32*>(rendererOutput.Bottom);
        sourceWidth = rendererOutput.Width;
        sourceHeight = rendererOutput.Height;
    }

    const int logicalWidth = std::max(1, width());
    const int logicalHeight = std::max(1, height());
    const qreal dpr = devicePixelRatioF();
    const u32 physicalWidth = static_cast<u32>(std::max(1, qRound(logicalWidth * dpr)));
    const u32 physicalHeight = static_cast<u32>(std::max(1, qRound(logicalHeight * dpr)));

    if (!vulkan->presenter.BeginFrame(physicalWidth, physicalHeight))
    {
        if (vulkan->presenter.HasFailed())
            reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
        else
            noteFrameStalled("the presenter could not begin a frame (swapchain not ready)");
        return;
    }

    const u32 presenterFrameIndex = vulkan->presenter.GetFrameIndex();
    vulkan->frameLeases[presenterFrameIndex].ReleaseNow();

    // The swapchain may be a different size than the widget for one frame
    // after a resize; every quad below is expressed in the swapchain's own
    // pixels so the frame is still correct rather than skipped.
    const float viewportWidth = static_cast<float>(vulkan->presenter.GetWidth());
    const float viewportHeight = static_cast<float>(vulkan->presenter.GetHeight());
    const float scaleX = viewportWidth / static_cast<float>(logicalWidth);
    const float scaleY = viewportHeight / static_cast<float>(logicalHeight);

    // --- uploads (must precede BeginComposition) ---------------------------

    float matrices[kMaxScreenTransforms][6];
    int kinds[kMaxScreenTransforms];
    int screens = 0;
    {
        QMutexLocker lock(&vulkan->layoutLock);
        screens = std::min(vulkan->numScreens, kMaxScreenTransforms);
        for (int index = 0; index < screens; ++index)
        {
            std::memcpy(matrices[index], vulkan->screenMatrix[index], sizeof(float) * 6);
            kinds[index] = vulkan->screenKind[index];
        }
    }

    bool screenUploaded[2] = {false, false};
    if (gpuFrame)
    {
        // Retain before recording the first copy. Even if a later upload or
        // EndFrame fails, this presenter slot keeps the source alive until its
        // fence is retired or the transition path explicitly quiesces it.
        vulkan->frameLeases[presenterFrameIndex] = std::move(rendererOutputLease);
        for (int index = 0; index < screens; ++index)
        {
            const int kind = kinds[index] & 1;
            if (screenUploaded[kind])
                continue;
            screenUploaded[kind] = vulkan->presenter.UploadLayerFromBuffer(
                kind == 0 ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                          : MelonPrime::VulkanPresenter::Layer::ScreenBottom,
                *gpuFrame,
                kind == 0 ? gpuFrame->TopOffset : gpuFrame->BottomOffset);
        }
    }
    else if (topPixels && bottomPixels)
    {
        const std::size_t rowBytes = static_cast<std::size_t>(sourceWidth) * sizeof(u32);
        for (int index = 0; index < screens; ++index)
        {
            const int kind = kinds[index] & 1;
            if (screenUploaded[kind])
                continue;
            screenUploaded[kind] = vulkan->presenter.UploadLayer(
                kind == 0 ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                          : MelonPrime::VulkanPresenter::Layer::ScreenBottom,
                kind == 0 ? static_cast<const void*>(topPixels)
                          : static_cast<const void*>(bottomPixels),
                sourceWidth,
                sourceHeight,
                rowBytes);
        }
    }

#ifdef MELONPRIME_CUSTOM_HUD
    // The bottom screen at internal resolution is the radar's source, exactly
    // as the software and DX12 panels feed it.
    QImage bottomScreenImage;
    if (bottomPixels)
    {
        bottomScreenImage = QImage(
            reinterpret_cast<const uchar*>(bottomPixels),
            static_cast<int>(sourceWidth),
            static_cast<int>(sourceHeight),
            static_cast<int>(sourceWidth * sizeof(u32)),
            QImage::Format_RGB32);
    }

    QRect hudRect;
    const bool hudVisible = renderHudOverlay(emuThread, bottomPixels ? &bottomScreenImage : nullptr, hudRect);
    bool gpuRadarVisible = false;
    MelonPrime::VulkanPresenter::Quad gpuRadarQuad;
    u32 gpuRadarCenterY = 0;
    if (gpuFrame && m_radarEnable)
    {
        auto* mp = emuThread->GetMelonPrimeCore();
        const float* topMatrix = nullptr;
        for (int index = 0; index < screens; ++index)
        {
            if ((kinds[index] & 1) == 0)
            {
                topMatrix = matrices[index];
                break;
            }
        }
        if (mp && topMatrix && MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                emuInstance, mp->GetCurrentRom(), mp->GetPlayerPosition()))
        {
            // The bottom screen is a Custom HUD input even when the active
            // layout only presents the top screen. DX12 always hands its
            // bottom QImage to the colour-key pass independently of the
            // visible screen list; keep that same contract for the native
            // Vulkan path instead of leaving ScreenBottom empty (or stale).
            if (!screenUploaded[1])
            {
                screenUploaded[1] = vulkan->presenter.UploadLayerFromBuffer(
                    MelonPrime::VulkanPresenter::Layer::ScreenBottom,
                    *gpuFrame,
                    gpuFrame->BottomOffset);
            }

            const float anchorX = topMatrix[0] * m_radarAnchorDsX
                + topMatrix[1] * m_radarAnchorDsY + topMatrix[4];
            const float anchorY = topMatrix[2] * m_radarAnchorDsX
                + topMatrix[3] * m_radarAnchorDsY + topMatrix[5];
            const int destinationX = static_cast<int>(m_hudOriginX) + static_cast<int>(
                (anchorX - m_hudOriginX) + m_radarDstX * m_hudScale);
            const int destinationY = static_cast<int>(m_hudOriginY) + static_cast<int>(
                (anchorY - m_hudOriginY) + m_radarDstY * m_hudScale);
            const float destinationSize = static_cast<float>(m_radarDstSize) * m_hudScale;

            gpuRadarQuad.Axis[0] = destinationSize * scaleX;
            gpuRadarQuad.Axis[3] = destinationSize * scaleY;
            gpuRadarQuad.Origin[0] = static_cast<float>(destinationX) * scaleX;
            gpuRadarQuad.Origin[1] = static_cast<float>(destinationY) * scaleY;
            gpuRadarQuad.Origin[2] = viewportWidth;
            gpuRadarQuad.Origin[3] = viewportHeight;
            const u8 hunter = std::min<u8>(mp->GetHunterID(), MelonPrime::kHunterCount - 1);
            gpuRadarCenterY = static_cast<u32>(MelonPrime::kBtmOverlaySrcCenterY[hunter]);
            gpuRadarVisible = screenUploaded[1]
                && m_radarSrcRadius > 0 && m_radarOpacity > 0.0f;
        }
    }
    bool hudUploaded = false;
    if (hudVisible && !Overlay[0].isNull())
    {
        const auto layer = MelonPrime::VulkanPresenter::Layer::Hud;
        const QRect imageRect(0, 0, Overlay[0].width(), Overlay[0].height());
        const QRect uploadRect = hudRect.intersected(imageRect);
        const bool needsInitialUpload = !vulkan->presenter.HasLayerContent(layer);
        if (!needsInitialUpload && uploadRect.isEmpty())
        {
            hudUploaded = true;
        }
        else
        {
            const QRect region = needsInitialUpload ? imageRect : uploadRect;
            VulkanPerf::ScopedCpuTimer hudUploadTimer(VulkanPerf::CpuMetric::HudUpload);
            MelonPrimePerf::ScopedHudPhase uploadPrepareTimer(
                MelonPrimePerf::HudPhase::UploadPrepare);
            hudUploaded = vulkan->presenter.UploadLayerRegion(
                layer,
                Overlay[0].constBits(),
                static_cast<u32>(Overlay[0].width()),
                static_cast<u32>(Overlay[0].height()),
                static_cast<std::size_t>(Overlay[0].bytesPerLine()),
                static_cast<u32>(region.x()),
                static_cast<u32>(region.y()),
                static_cast<u32>(region.width()),
                static_cast<u32>(region.height()));
        }
    }
#endif

    QSize osdSize;
    const bool osdUploaded = buildOsdStrip(osdSize)
        && vulkan->presenter.UploadLayer(
               MelonPrime::VulkanPresenter::Layer::Osd,
               vulkan->osdStrip.constBits(),
               static_cast<u32>(osdSize.width()),
               static_cast<u32>(osdSize.height()),
               static_cast<std::size_t>(vulkan->osdStrip.bytesPerLine()));

    // --- composition -------------------------------------------------------
    //
    // Layer order is the DS display order plus the two MelonPrime overlays:
    // native game 3D and native game 2D are already combined inside the
    // composed screen images by the Vulkan compositor, then the Custom HUD,
    // then the OSD on top of everything.

    vulkan->presenter.BeginComposition();

    for (int index = 0; index < screens; ++index)
    {
        const int kind = kinds[index] & 1;
        if (!screenUploaded[kind])
            continue;

        const float* matrix = matrices[index];
        MelonPrime::VulkanPresenter::Quad quad;
        // ScreenLayout's 2x3 maps the 256x192 DS rect into logical widget
        // coordinates. Folding the source rect and the device pixel ratio into
        // it here is what keeps rotation, gap, swap, integer scaling and
        // letterboxing byte-identical to the other panels: the layout maths is
        // reused, not reimplemented.
        quad.Axis[0] = matrix[0] * 256.0f * scaleX;
        quad.Axis[1] = matrix[1] * 256.0f * scaleY;
        quad.Axis[2] = matrix[2] * 192.0f * scaleX;
        quad.Axis[3] = matrix[3] * 192.0f * scaleY;
        quad.Origin[0] = matrix[4] * scaleX;
        quad.Origin[1] = matrix[5] * scaleY;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;

        vulkan->presenter.DrawLayer(
            kind == 0 ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                      : MelonPrime::VulkanPresenter::Layer::ScreenBottom,
            quad,
            MelonPrime::VulkanPresenter::Blend::Opaque,
            filter);
    }

#ifdef MELONPRIME_CUSTOM_HUD
    if (hudUploaded)
    {
        // The overlay covers the whole widget in logical pixels and is stretched
        // over the whole viewport, which is how the HUD reaches elements the
        // layout placed inside the black bars.
        MelonPrime::VulkanPresenter::Quad quad;
        quad.Axis[0] = viewportWidth;
        quad.Axis[1] = 0.0f;
        quad.Axis[2] = 0.0f;
        quad.Axis[3] = viewportHeight;
        quad.Origin[0] = 0.0f;
        quad.Origin[1] = 0.0f;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;
        quad.UvRect[2] =
            static_cast<float>(Overlay[0].width()) / static_cast<float>(vulkan->presenter.GetWidth());
        quad.UvRect[3] =
            static_cast<float>(Overlay[0].height()) / static_cast<float>(vulkan->presenter.GetHeight());

        vulkan->presenter.DrawLayer(
            MelonPrime::VulkanPresenter::Layer::Hud,
            quad,
            MelonPrime::VulkanPresenter::Blend::Premultiplied,
            filter);
    }

    // Match DrawBottomScreenOverlay(): combined outline and SVG frame are
    // behind the colour-keyed bottom-screen pixels. The CPU HUD image contains
    // those two backing layers, so draw the native GPU radar after it.
    if (gpuRadarVisible)
    {
        vulkan->presenter.DrawRadar(
            gpuRadarQuad,
            m_radarOpacity,
            gpuRadarCenterY,
            static_cast<u32>(m_radarSrcRadius));
    }
#endif

    if (osdUploaded)
    {
        MelonPrime::VulkanPresenter::Quad quad;
        quad.Axis[0] = static_cast<float>(osdSize.width()) * scaleX;
        quad.Axis[1] = 0.0f;
        quad.Axis[2] = 0.0f;
        quad.Axis[3] = static_cast<float>(osdSize.height()) * scaleY;
        quad.Origin[0] = static_cast<float>(kOSDMarginPx) * scaleX;
        quad.Origin[1] = static_cast<float>(kOSDMarginPx) * scaleY;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;
        quad.UvRect[2] =
            static_cast<float>(osdSize.width()) / static_cast<float>(vulkan->presenter.GetWidth());
        quad.UvRect[3] =
            static_cast<float>(osdSize.height()) / static_cast<float>(vulkan->presenter.GetHeight());

        vulkan->presenter.DrawLayer(
            MelonPrime::VulkanPresenter::Layer::Osd,
            quad,
            MelonPrime::VulkanPresenter::Blend::Premultiplied,
            false);
    }

    if (!vulkan->presenter.EndFrame())
    {
        reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
        return;
    }

    noteFramePresented();
    requestNativeSurfaceVisible(true);
}


bool ScreenPanelVulkan::buildOsdStrip(QSize& outSize)
{
    outSize = QSize();

    osdUpdate();
    if (!osdEnabled)
        return false;

    QMutexLocker lock(&osdMutex);
    if (osdItems.empty())
        return false;

    int stripWidth = 0;
    int stripHeight = 0;
    for (const OSDItem& item : osdItems)
    {
        if (item.bitmap.isNull())
            continue;
        stripWidth = std::max(stripWidth, item.bitmap.width());
        stripHeight += item.bitmap.height();
    }
    if (stripWidth <= 0 || stripHeight <= 0)
        return false;

    if (vulkan->osdStrip.width() != stripWidth
        || vulkan->osdStrip.height() != stripHeight
        || vulkan->osdStrip.format() != QImage::Format_ARGB32_Premultiplied)
    {
        vulkan->osdStrip = QImage(stripWidth, stripHeight, QImage::Format_ARGB32_Premultiplied);
    }
    vulkan->osdStrip.fill(Qt::transparent);

    // Row copies rather than a QPainter pass: the item bitmaps are already
    // rasterized premultiplied ARGB and only need stacking, so there is nothing
    // for the raster engine to do that memcpy does not.
    int y = 0;
    for (const OSDItem& item : osdItems)
    {
        if (item.bitmap.isNull())
            continue;
        const int rowBytes = item.bitmap.width() * 4;
        for (int row = 0; row < item.bitmap.height(); ++row)
        {
            std::memcpy(
                vulkan->osdStrip.scanLine(y + row),
                item.bitmap.constScanLine(row),
                static_cast<std::size_t>(rowBytes));
        }
        y += item.bitmap.height();
    }

    outSize = QSize(stripWidth, stripHeight);
    return true;
}


#ifdef MELONPRIME_CUSTOM_HUD
bool ScreenPanelVulkan::renderHudOverlay(EmuThread* emuThread, QImage* bottomScreen, QRect& outDirty)
{
    outDirty = QRect();

    auto* mp = emuThread ? emuThread->GetMelonPrimeCore() : nullptr;
    const bool editMode = mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
    if (!mp || !mp->IsRomDetected() || !(mp->IsInGame() || editMode))
        return false;

    auto& instcfg = emuInstance->getLocalConfig();

    // One epoch check drives every cached config value, exactly as the OpenGL
    // and software paths do: these are hash-map lookups and font construction,
    // far too expensive to repeat per frame.
    const uint32_t epoch = MelonPrime::CustomHud_GetCacheEpoch(mp->HudConfigState());
    if (epoch != m_hudCfgEpoch)
    {
        m_hudCfgEpoch = epoch;
        m_hudEnabled = MelonPrime::CustomHud_IsEnabled(instcfg);
    }
    if (epoch != m_hudFontEpoch)
    {
        m_hudFontEpoch = epoch;
        overlayFont = MelonPrime::CustomHud_ResolveBaseFont(instcfg);
        overlayFont.setPixelSize(MelonPrime::CustomHud_ResolveFontPixelSize(instcfg));
    }
    if (epoch != m_radarCfgEpoch)
    {
        m_radarCfgEpoch = epoch;
        m_radarEnable = instcfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable);
        m_radarAnchor = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayAnchor);
        m_radarDstX = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstX);
        m_radarDstY = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstY);
        m_radarDstSize = std::max(instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstSize), 1);
        m_radarOpacity =
            std::clamp(static_cast<float>(instcfg.GetDouble(MP_HUD_PROP_KEY_BtmOverlayOpacity)), 0.0f, 1.0f);
        m_radarSrcRadius = instcfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius);
        m_radarAnchorDsX = (m_radarAnchor % 3) * 128.0f;
        m_radarAnchorDsY = (m_radarAnchor / 3) * 96.0f;
    }

    if (!(m_hudEnabled || editMode))
    {
        MelonPrime::CustomHud_EnsurePatchRestored(
            mp->HudConfigState(), emuInstance, instcfg,
            mp->GetCurrentRom(), mp->GetPlayerPosition(), mp->IsInGame());
        return false;
    }

    // The overlay covers the whole widget in logical pixels so HUD elements
    // placed outside the DS rect (x < 0 or x > 256 when pillarboxed) stay
    // visible in the black bars.
    const int overlayWidth = std::max(1, width());
    const int overlayHeight = std::max(1, height());
    const QRect previousDirty = m_hudPrevDirty;
    bool overlayRecreated = false;
    {
        MelonPrimePerf::ScopedHudPhase clearTimer(MelonPrimePerf::HudPhase::Clear);
        if (Overlay[0].width() != overlayWidth
            || Overlay[0].height() != overlayHeight
            || Overlay[0].format() != QImage::Format_ARGB32_Premultiplied)
        {
            Overlay[0] = QImage(overlayWidth, overlayHeight, QImage::Format_ARGB32_Premultiplied);
            Overlay[0].fill(Qt::transparent);
            m_hudPrevDirty = QRect();
            overlayRecreated = true;
        }
        else if (!m_hudPrevDirty.isEmpty())
        {
            // Direct scanline clear of last frame's dirty region only. Transparent
            // is 0 in ARGB32_Premultiplied, so memset is the whole operation.
            const int left = std::max(0, m_hudPrevDirty.left());
            const int right = std::min(Overlay[0].width() - 1, m_hudPrevDirty.right());
            const int top = std::max(0, m_hudPrevDirty.top());
            const int bottom = std::min(Overlay[0].height() - 1, m_hudPrevDirty.bottom());
            if (left <= right && top <= bottom)
            {
                const std::size_t clearBytes = static_cast<std::size_t>(right - left + 1) * 4u;
                for (int row = top; row <= bottom; ++row)
                    std::memset(Overlay[0].scanLine(row) + left * 4, 0, clearBytes);
            }
        }
    }

    QRect dirty;
    {
        QPainter painter(&Overlay[0]);
        painter.setFont(overlayFont);
        QImage* radarSource = MelonPrime::CustomHud_PrepareRadarColorKeySource(
            m_radarEnable ? bottomScreen : nullptr,
            &Overlay[1],
            mp->GetHunterID(),
            m_radarSrcRadius);
        const Uint64 hudRenderStart = MelonPrimePerf::ReadTicksIfActive();
        dirty = MelonPrime::CustomHud_Render(
            mp->HudConfigState(),
            emuInstance, instcfg,
            mp->GetCurrentRom(), mp->GetAddrHot(),
            mp->GetPlayerPosition(),
            &painter, nullptr,
            &Overlay[0], radarSource,
            mp->IsInGame(),
            m_topStretchX, m_hudScale,
            m_hudOriginX / m_hudScale, m_hudOriginY / m_hudScale);
        if (hudRenderStart)
            MelonPrimePerf::AddCustomHudRenderTicks(
                MelonPrimePerf::ReadTicksIfActive() - hudRenderStart);
    }

    if (MelonPrimePerf::IsFrameActive() && !dirty.isEmpty())
    {
        MelonPrimePerf::AddHudDirtyArea(dirty.width() * dirty.height());
        MelonPrimePerf::CountCustomHudDrawn();
    }

    m_hudPrevDirty = dirty;
    outDirty = overlayRecreated
        ? QRect(0, 0, Overlay[0].width(), Overlay[0].height())
        : dirty.united(previousDirty);
    return true;
}
#endif


// ---------------------------------------------------------------------------
// Qt painting
//
// Only ever reached when the native presentation surface is hidden: no ROM is
// loaded, or the presenter failed and the panel is waiting for the renderer
// fallback to swap it out. The presented frame itself never goes through
// QPainter.
// ---------------------------------------------------------------------------

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);

    auto* emuThread = emuInstance ? emuInstance->getEmuThread() : nullptr;
    if (emuThread && emuThread->emuIsActive())
        return;

    osdUpdate();
    QMutexLocker osdLock(&osdMutex);
    painter.drawPixmap(QRect(splashPos[3], QSize(kSplashLogoWidth, kSplashLogoWidth)), splashLogo);
    for (int index = 0; index < 3; ++index)
        painter.drawImage(splashPos[index], splashText[index].bitmap);
}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
