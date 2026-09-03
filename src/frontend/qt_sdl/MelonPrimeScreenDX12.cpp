/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// ScreenPanelDX12 -- the Qt seam for the native DirectX 12 presentation path.
//
// The class is declared in Screen.h next to its siblings, but its body lives
// here rather than in Screen.cpp for the same reason ScreenPanelVulkan does:
// Screen.cpp is upstream-owned and cross-platform, while this panel is
// entirely MelonPrime-owned and Windows-only (DX12 presenter, native child
// surface, Custom HUD compositing, renderer-transition quiescing). Splitting
// the translation unit also keeps every DX12 header out of the generic screen
// TU, so a non-Windows or DX12-off build never parses one.
//
// Threading, stated once because everything below depends on it:
//
//   GUI thread          constructs and destroys the panel, owns every QWidget
//                       operation, creates and destroys the native child
//                       surface, publishes the surface/layout snapshots, and
//                       reconciles native surface visibility.
//   Emulation thread    calls drawScreen() (once per emulated frame, and while
//                       paused at the presenter's own cadence). It is the only
//                       thread that records or submits DX12 work.
//
// The two never touch a DX12 object at the same time: the panel is not
// reachable from the emulation thread until initDX12() has returned and the
// panel is published to MainWindow::panel, and it is unpublished under
// MainWindow::screenPanelLock before the destructor runs. GUI-thread resize,
// DPI and fullscreen handling therefore only publishes snapshots, and the
// swapchain is rebuilt on the emulation thread at the next frame boundary.
//
// Locks used below, in the only order they may be taken:
//
//   g_dx12PanelRegistryLock   process-wide panel list for renderer
//                             transitions. Held for the list walk only; never
//                             held across a panel method that can block. The
//                             transition caller is the emulation-thread
//                             renderer barrier: the GUI is synchronously
//                             waiting for that barrier and cannot destroy a
//                             panel until it returns, which makes the copied
//                             raw pointers lifetime-safe after unlock.
//   DX12State::layoutLock     GUI-published layout snapshot read by the
//                             emulation thread.
//   DX12State::fallbackLock   CPU fallback frame shared with paintEvent().
//
// The registry is a short flat vector walked only on renderer transitions,
// not a per-frame lookup; it is intentionally not a map or a lock-free
// structure.

#include "Screen.h"

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QApplication>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#include "Config.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "GPU.h"
#include "GPU_DX12.h"
#include "DX12Perf.h"
#include "MelonPrime.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeDX12FeatureCheck.h"
#include "MelonPrimeDX12SurfacePresenter.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeRendererTransitionPerf.h"
#include "NDS.h"
#include "Platform.h"
#include "main.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudRadar.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeHudEdit.h"
#include "MelonPrimeHudPresentationState.h"
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeHudScreenOverlay.h"
#endif

using namespace melonDS;

namespace
{

// Mirrors the value Screen.cpp uses for the same purpose. It is a layout
// constant of the shared splash drawing, not configuration, and every panel
// must place the splash identically.
const u32 kOSDMargin = 6;
const int kLogoWidth = 192;

} // namespace

namespace
{
class DX12SurfaceHost final : public QWidget
{
public:
    using LifecycleCallback = std::function<void(QEvent::Type, bool)>;

    explicit DX12SurfaceHost(QWidget* parent, LifecycleCallback callback = {})
        : QWidget(parent), Lifecycle(std::move(callback))
    {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAutoFillBackground(false);
    }

protected:
    QPaintEngine* paintEngine() const override { return nullptr; }
    void paintEvent(QPaintEvent*) override {}

    bool event(QEvent* event) override
    {
        const bool aboutToDestroy = event
            && event->type() == QEvent::PlatformSurface
            && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed;
        if (aboutToDestroy && Lifecycle)
            Lifecycle(event->type(), true);

        const bool accepted = QWidget::event(event);
        if (Lifecycle && !aboutToDestroy && event
            && (event->type() == QEvent::PlatformSurface
                || event->type() == QEvent::Show
                || event->type() == QEvent::WindowStateChange
                || event->type() == QEvent::ScreenChangeInternal))
        {
            Lifecycle(event->type(), false);
        }
        return accepted;
    }

private:
    LifecycleCallback Lifecycle;
};
} // namespace

QMutex g_dx12PanelRegistryLock;
std::vector<ScreenPanelDX12*> g_dx12PanelRegistry;

struct ScreenPanelDX12::DX12State
{
    MelonPrime::DX12SurfacePresenter presenter;
    QPointer<DX12SurfaceHost> surface;
    QMutex layoutLock;
    float screenMatrix[kMaxScreenTransforms][6]{};
    int screenKind[kMaxScreenTransforms]{};
    int numScreens = 0;
    std::atomic<std::uint32_t> layoutRevision{0};
    // Emulation-thread cache. The GUI-published arrays above are copied only
    // when layoutRevision changes; a stable frame never takes layoutLock.
    float cachedScreenMatrix[kMaxScreenTransforms][6]{};
    int cachedScreenKind[kMaxScreenTransforms]{};
    int cachedNumScreens = 0;
    std::uint32_t cachedLayoutRevision = ~0u;
    std::uint32_t rendererSnapshotRevision = ~0u;
    melonDS::DX12Renderer* cachedDX12Renderer = nullptr;
    RendererOutputLease frameLease;
    QMutex fallbackLock;
    QImage fallbackFrame;
    QImage hudFrame;
    QRect hudRect;
    QImage osdStrip;
    std::atomic_bool surfaceVisibleRequested{false};
    MelonPrime::NativeSurfaceSnapshotStore surfaceSnapshot;
    // GUI-thread-only identity publication state.
    std::uintptr_t guiSurfaceHandle = 0;
    std::uint64_t guiSurfaceIdentityGeneration = 0;
    std::uint64_t guiSurfaceGeometryRevision = 0;
    std::uint32_t guiSurfaceLastLogicalWidth = 0;
    std::uint32_t guiSurfaceLastLogicalHeight = 0;
    std::uint32_t guiSurfaceLastPhysicalWidth = 0;
    std::uint32_t guiSurfaceLastPhysicalHeight = 0;
    bool guiSurfaceLastFullscreen = false;
    bool guiSurfaceGeometryInitialized = false;
    bool guiSurfaceIdentityDirty = true;
    std::uint64_t presenterSurfaceIdentityGeneration = 0;
    std::uint64_t presenterSurfaceGeometryRevision = 0;
    std::uintptr_t presenterSurfaceHandle = 0;
    // Set by the GUI thread while the Custom HUD on-screen editor owns the
    // panel, read by the emulation thread's paused draw pass. The live settings
    // preview shares this presentation exception without entering edit mode.
    std::atomic_bool hudEditLivePresentation{false};
    std::atomic_bool hudLivePreviewPresentation{false};
    bool initialized = false;
    bool runtimeFailureReported = false;
    MelonPrime::NativeVisibilityState nativeVisibility;
};

ScreenPanelDX12::ScreenPanelDX12(QWidget* parent)
    : ScreenPanel(parent), dx12(std::make_unique<DX12State>())
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    // The factor is passed explicitly: ScreenPanel::screenGetMinSize()'s
    // default argument is declared in Screen.cpp and is not visible here.
    setMinimumSize(screenGetMinSize(1));

    dx12->surface = new DX12SurfaceHost(
        this,
        [this](QEvent::Type eventType, bool aboutToDestroy) {
            handleDX12SurfaceHostLifecycleGuiThread(eventType, aboutToDestroy);
        });
    dx12->surface->setGeometry(rect());
    dx12->surface->hide();
    publishDX12SurfaceSnapshotGuiThread();

    QMutexLocker lock(&g_dx12PanelRegistryLock);
    g_dx12PanelRegistry.push_back(this);
}

ScreenPanelDX12::~ScreenPanelDX12()
{
    {
        QMutexLocker lock(&g_dx12PanelRegistryLock);
        g_dx12PanelRegistry.erase(
            std::remove(g_dx12PanelRegistry.begin(), g_dx12PanelRegistry.end(), this),
            g_dx12PanelRegistry.end());
    }

    if (dx12)
    {
        prepareForRendererTransition();
        dx12->presenter.Shutdown();
    }
}

void ScreenPanelDX12::prepareForRendererTransition(bool recordTransitionPerf)
{
    if (!dx12)
        return;

#ifdef MELONPRIME_CUSTOM_HUD
    // The presenter/layer resources are about to be detached from the active
    // renderer. Do not let an identical visual key skip the first upload after
    // the transition.
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif

    // Keep the same lifetime contract as the Vulkan presenter: old queue work
    // must be complete before descriptor identity is cleared or the renderer
    // output lease is dropped.
    const auto quiesceStart = recordTransitionPerf
        ? MelonPrime::g_rendererTransitionPerf.Now() : 0;
    dx12->presenter.Quiesce();
    if (recordTransitionPerf) {
        MelonPrime::g_rendererTransitionPerf.Record(
            MelonPrime::RendererTransitionMetric::QuiesceDuration,
            quiesceStart, MelonPrime::g_rendererTransitionPerf.Now());
    }
    dx12->presenter.InvalidateDirectDescriptorCache();
    dx12->frameLease.ReleaseNow();
    dx12->nativeVisibility.Reset();
    dx12->rendererSnapshotRevision = ~0u;
    dx12->cachedDX12Renderer = nullptr;
}

void ScreenPanelDX12::PrepareForInstanceRendererTransition(EmuInstance* instance)
{
    const auto transitionStart = MelonPrime::g_rendererTransitionPerf.Now();
    // Snapshot only matching panels while the registry is locked. Do not call
    // Quiesce (or any other panel method) under this process-global mutex: it
    // can wait for GPU work. The caller has already established the
    // prepareVideoBackendTransition() GUI/emu barrier documented above, so
    // GUI-owned panel destruction cannot race this short transition window.
    std::vector<ScreenPanelDX12*> panels;
    {
        const auto lockStart = MelonPrime::g_rendererTransitionPerf.Now();
        QMutexLocker lock(&g_dx12PanelRegistryLock);
        MelonPrime::g_rendererTransitionPerf.Record(
            MelonPrime::RendererTransitionMetric::RegistryLockWait,
            lockStart, MelonPrime::g_rendererTransitionPerf.Now());
        for (ScreenPanelDX12* panel : g_dx12PanelRegistry)
        {
            if (panel->emuInstance == instance)
                panels.push_back(panel);
        }
    }

    for (ScreenPanelDX12* panel : panels)
    {
        panel->prepareForRendererTransition(true);
    }
    MelonPrime::g_rendererTransitionPerf.Record(
        MelonPrime::RendererTransitionMetric::TransitionTotal,
        transitionStart, MelonPrime::g_rendererTransitionPerf.Now());
    MelonPrime::g_rendererTransitionPerf.Report(
        instance ? instance->getInstanceID() : 0, "dx12");
}

bool ScreenPanelDX12::initDX12()
{
    if (!dx12 || !dx12->surface)
        return false;
#ifdef MELONPRIME_CUSTOM_HUD
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif
    publishDX12SurfaceSnapshotGuiThread();
    MelonPrime::NativeSurfaceSnapshot snapshot;
    if (!dx12->surfaceSnapshot.Read(snapshot) || !snapshot.Valid)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12 native presenter initialization refused: invalid GUI surface snapshot\n");
        return false;
    }
    const HWND window = reinterpret_cast<HWND>(snapshot.NativeHandle);
    dx12->initialized = dx12->presenter.Init(
        window,
        snapshot.IdentityGeneration,
        snapshot.PhysicalWidth,
        snapshot.PhysicalHeight);
    if (!dx12->initialized)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12 native presenter initialization failed reason=%s\n",
            dx12->presenter.LastError().c_str());
    }
    if (dx12->initialized)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::DX12PresenterReinitCount);
        dx12->presenterSurfaceIdentityGeneration = snapshot.IdentityGeneration;
        dx12->presenterSurfaceGeometryRevision = snapshot.GeometryRevision;
        dx12->presenterSurfaceHandle = snapshot.NativeHandle;
        dx12->nativeVisibility.Reset();
    }
    return dx12->initialized;
}

void ScreenPanelDX12::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    if (!dx12)
        return;

    {
        QMutexLocker lock(&dx12->layoutLock);
        dx12->numScreens = std::min(numScreens, kMaxScreenTransforms);
        for (int index = 0; index < dx12->numScreens; ++index)
        {
            std::memcpy(dx12->screenMatrix[index], screenMatrix[index], sizeof(float) * 6);
            dx12->screenKind[index] = screenKind[index];
        }
    }
    dx12->layoutRevision.fetch_add(1, std::memory_order_release);
}

void ScreenPanelDX12::resizeEvent(QResizeEvent* event)
{
    if (dx12 && dx12->surface)
        dx12->surface->setGeometry(rect());
    ScreenPanel::resizeEvent(event);
    publishDX12SurfaceSnapshotGuiThread();
}

void ScreenPanelDX12::publishDX12SurfaceSnapshotGuiThread()
{
    if (!dx12 || !dx12->surface)
        return;

    const std::uintptr_t handle = static_cast<std::uintptr_t>(dx12->surface->winId());
    const bool identityChanged = dx12->guiSurfaceIdentityGeneration == 0
        || dx12->guiSurfaceHandle != handle
        || dx12->guiSurfaceIdentityDirty;
    if (identityChanged)
    {
        dx12->guiSurfaceHandle = handle;
        ++dx12->guiSurfaceIdentityGeneration;
        dx12->guiSurfaceIdentityDirty = false;
        DX12Perf::AddCounter(
            DX12Perf::Counter::DX12NativeIdentityGenerationChangeCount);
    }

    const int logicalWidth = std::max(1, width());
    const int logicalHeight = std::max(1, height());
    const qreal dpr = devicePixelRatioF();
    const std::uint32_t physicalWidth = static_cast<std::uint32_t>(
        std::max(1, qRound(logicalWidth * dpr)));
    const std::uint32_t physicalHeight = static_cast<std::uint32_t>(
        std::max(1, qRound(logicalHeight * dpr)));
    const bool fullscreen = window() && window()->isFullScreen();
    if (!dx12->guiSurfaceGeometryInitialized
        || dx12->guiSurfaceLastLogicalWidth != static_cast<std::uint32_t>(logicalWidth)
        || dx12->guiSurfaceLastLogicalHeight != static_cast<std::uint32_t>(logicalHeight)
        || dx12->guiSurfaceLastPhysicalWidth != physicalWidth
        || dx12->guiSurfaceLastPhysicalHeight != physicalHeight
        || dx12->guiSurfaceLastFullscreen != fullscreen)
    {
        dx12->guiSurfaceGeometryInitialized = true;
        dx12->guiSurfaceLastLogicalWidth = static_cast<std::uint32_t>(logicalWidth);
        dx12->guiSurfaceLastLogicalHeight = static_cast<std::uint32_t>(logicalHeight);
        dx12->guiSurfaceLastPhysicalWidth = physicalWidth;
        dx12->guiSurfaceLastPhysicalHeight = physicalHeight;
        dx12->guiSurfaceLastFullscreen = fullscreen;
        ++dx12->guiSurfaceGeometryRevision;
    }
    MelonPrime::NativeSurfaceSnapshot snapshot;
    snapshot.NativeHandle = handle;
    snapshot.IdentityGeneration = dx12->guiSurfaceIdentityGeneration;
    snapshot.GeometryRevision = dx12->guiSurfaceGeometryRevision;
    snapshot.LogicalWidth = static_cast<std::uint32_t>(logicalWidth);
    snapshot.LogicalHeight = static_cast<std::uint32_t>(logicalHeight);
    snapshot.PhysicalWidth = physicalWidth;
    snapshot.PhysicalHeight = physicalHeight;
    snapshot.Fullscreen = fullscreen;
    snapshot.Valid = handle != 0 && dx12->surface->windowHandle() != nullptr;
    dx12->surfaceSnapshot.Publish(snapshot);
    DX12Perf::AddCounter(
        DX12Perf::Counter::DX12SurfaceSnapshotPublishCount);
}

void ScreenPanelDX12::handleDX12SurfaceHostLifecycleGuiThread(
    QEvent::Type eventType,
    bool aboutToDestroy)
{
    if (!dx12)
        return;

    DX12Perf::AddCounter(
        DX12Perf::Counter::DX12SurfaceEventCount);

    if (aboutToDestroy || eventType == QEvent::PlatformSurface)
        dx12->guiSurfaceIdentityDirty = true;
    if (aboutToDestroy)
    {
        MelonPrime::NativeSurfaceSnapshot invalid;
        invalid.IdentityGeneration = ++dx12->guiSurfaceIdentityGeneration;
        DX12Perf::AddCounter(
            DX12Perf::Counter::DX12NativeIdentityGenerationChangeCount);
        invalid.GeometryRevision = dx12->guiSurfaceGeometryRevision;
        dx12->surfaceSnapshot.Publish(invalid);
        DX12Perf::AddCounter(
            DX12Perf::Counter::DX12SurfaceSnapshotPublishCount);
        return;
    }

    (void)eventType;
    QMetaObject::invokeMethod(
        this, [this]() { publishDX12SurfaceSnapshotGuiThread(); }, Qt::QueuedConnection);
}

bool ScreenPanelDX12::event(QEvent* event)
{
    if (dx12 && event)
    {
        switch (event->type())
        {
        case QEvent::PlatformSurface:
        case QEvent::WindowStateChange:
        case QEvent::Show:
            QMetaObject::invokeMethod(
                this,
                [this]() { publishDX12SurfaceSnapshotGuiThread(); },
                Qt::QueuedConnection);
            break;
        case QEvent::ScreenChangeInternal:
            QMetaObject::invokeMethod(
                this,
                [this]() { publishDX12SurfaceSnapshotGuiThread(); },
                Qt::QueuedConnection);
            break;
        default:
            break;
        }
    }
    return ScreenPanel::event(event);
}

void ScreenPanelDX12::requestNativeSurfaceVisible(bool visible)
{
    if (!dx12)
        return;
    if (dx12->surfaceVisibleRequested.load(std::memory_order_relaxed) == visible)
        return;
    if (dx12->surfaceVisibleRequested.exchange(
            visible, std::memory_order_acq_rel) == visible)
        return;
    MelonPrimePerf::CountSurfaceVisibilityStateChange();

    QMetaObject::invokeMethod(
        this,
        [this, visible]() {
            if (!dx12 || !dx12->surface)
                return;
            if (visible)
            {
                dx12->surface->setGeometry(rect());
                dx12->surface->show();
                dx12->surface->raise();
                publishDX12SurfaceSnapshotGuiThread();
            }
            else
            {
                dx12->surface->hide();
                update();
            }
        },
        Qt::QueuedConnection);
}

void ScreenPanelDX12::reportRuntimeFailure(const char* reason)
{
    if (!dx12 || dx12->runtimeFailureReported)
        return;
    dx12->runtimeFailureReported = true;
    MelonPrime::DX12FeatureCheck::ReportRuntimeFailure(
        reason ? reason : "DX12 native presentation failed");
    if (auto* emuThread = emuInstance->getEmuThread())
    {
        QMetaObject::invokeMethod(
            emuThread,
            [emuThread]() { emit emuThread->rendererRuntimeFallback(); },
            Qt::QueuedConnection);
    }
}

#ifdef MELONPRIME_CUSTOM_HUD
void ScreenPanelDX12::setHudEditModeActive(bool active)
{
    ScreenPanel::setHudEditModeActive(active);
    if (!dx12)
        return;

    dx12->hudEditLivePresentation.store(active, std::memory_order_relaxed);
}

void ScreenPanelDX12::setHudLivePreviewActive(bool active)
{
    if (!dx12)
        return;

    dx12->hudLivePreviewPresentation.store(active, std::memory_order_release);
}
#endif

void ScreenPanelDX12::drawScreen()
{
    refreshClipForGameStateChange();
    if (!dx12 || !dx12->initialized)
        return;

    auto* emuThread = emuInstance->getEmuThread();
    if (!emuThread->emuIsActive())
    {
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }

    // Paused normally means "leave the swapchain showing the last presented
    // image": nothing new is emulated, and the native child keeps the frame up
    // even while a modal dialog is exposed.
    //
    // The Custom HUD on-screen editor is the exception. The settings dialog
    // pauses emulation before handing the panel to the editor, so this pass is
    // the only thing that can ever put the editor overlay on screen; without
    // this the DX12 panel just keeps presenting the frozen pre-pause frame and
    // the editor is invisible. Same contract as ScreenPanelVulkan; the
    // software/OpenGL panels gate on emuIsActive() and keep drawing anyway.
#ifdef MELONPRIME_CUSTOM_HUD
    const bool hudLivePresentation =
        dx12->hudEditLivePresentation.load(std::memory_order_relaxed)
        || dx12->hudLivePreviewPresentation.load(std::memory_order_acquire);
#else
    constexpr bool hudLivePresentation = false;
#endif
    if (!emuThread->emuIsRunning() && !hudLivePresentation)
        return;

    MelonPrime::NativeSurfaceSnapshot publishedSurface;
    if (!dx12->surfaceSnapshot.Read(publishedSurface) || !publishedSurface.Valid)
    {
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }

    const bool surfaceIdentityChanged =
        dx12->presenter.IsInitialized()
        && (dx12->presenterSurfaceIdentityGeneration != publishedSurface.IdentityGeneration
            || dx12->presenterSurfaceHandle != publishedSurface.NativeHandle);
    if (surfaceIdentityChanged)
    {
        // HWND generation changes are a full presentation transition. Drain
        // the queue before dropping renderer leases or descriptor identity;
        // same-HWND extent changes remain the cheaper ResizeBuffers path.
        dx12->presenter.Quiesce();
        dx12->presenter.InvalidateDirectDescriptorCache();
        dx12->frameLease.ReleaseNow();
        dx12->presenter.Shutdown();
        dx12->presenterSurfaceIdentityGeneration = 0;
        dx12->presenterSurfaceGeometryRevision = 0;
        dx12->presenterSurfaceHandle = 0;
        dx12->nativeVisibility.Reset();
    }

    if (!dx12->presenter.IsInitialized())
    {
        const HWND window = reinterpret_cast<HWND>(publishedSurface.NativeHandle);
        if (!dx12->presenter.Init(
                window,
                publishedSurface.IdentityGeneration,
                publishedSurface.PhysicalWidth,
                publishedSurface.PhysicalHeight))
        {
            reportRuntimeFailure(dx12->presenter.LastError().c_str());
            return;
        }
        dx12->presenterSurfaceIdentityGeneration = publishedSurface.IdentityGeneration;
        dx12->presenterSurfaceGeometryRevision = publishedSurface.GeometryRevision;
        dx12->presenterSurfaceHandle = publishedSurface.NativeHandle;
    }

    auto* nds = emuInstance->getNDS();
    if (!nds)
        return;
    const MelonPrime::PresentationConfigSnapshot presentation =
        emuInstance->getPresentationConfigSnapshot();
    if (presentation.revision != dx12->rendererSnapshotRevision)
    {
        dx12->rendererSnapshotRevision = presentation.revision;
        dx12->cachedDX12Renderer = nullptr;
        if (presentation.activeRenderer == renderer3D_DX12 || presentation.revision == 0)
        {
            dx12->cachedDX12Renderer =
                dynamic_cast<melonDS::DX12Renderer*>(&nds->GPU.GetRenderer());
        }
    }
    auto* renderer = dx12->cachedDX12Renderer;
    RendererOutputLease outputLease = nds->GPU.AcquireRendererOutputLease();
    const RendererOutput& output = outputLease.Output;
    const DX12PresentedFrame* gpuFrame = nullptr;
    const u32* cpuTop = nullptr;
    const u32* cpuBottom = nullptr;
    u32 sourceWidth = output.Width;
    u32 sourceHeight = output.Height;
    if (output.Kind == RendererOutputKind::DX12Buffer && output.Top)
    {
        gpuFrame = static_cast<const DX12PresentedFrame*>(output.Top);
        sourceWidth = gpuFrame->Width;
        sourceHeight = gpuFrame->Height;
    }
    else if (output.Kind == RendererOutputKind::CpuBgra && output.Top && output.Bottom)
    {
        cpuTop = static_cast<const u32*>(output.Top);
        cpuBottom = static_cast<const u32*>(output.Bottom);
    }
    // Two different things publish CpuBgra here, and only the live renderer
    // tells them apart:
    //
    //  - No DX12Renderer at all ("3D.ForceSoftwareOutsideMatch" swapped the
    //    renderer to Software while this panel keeps owning the swapchain).
    //    The software renderer flattens its own 3D layer, so this frame is a
    //    complete picture and belongs on screen through the CPU upload path.
    //  - A live DX12Renderer whose pipeline is not ready yet. That output is a
    //    Software-2D plus placeholder-3D hybrid and must never become visible.
    const bool softwarePresentation = !renderer && cpuTop && cpuBottom;
    if (!gpuFrame && !softwarePresentation)
    {
        // Keep the last native frame once one exists, otherwise leave the Qt
        // splash/black path active until a complete GPU frame is published.
        if (!dx12->nativeVisibility.FirstCompleteFrameVisible)
        {
            requestNativeSurfaceVisible(false);
            QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        }
        return;
    }
    if (sourceWidth == 0 || sourceHeight == 0)
        return;

    const std::uint32_t layoutRevision =
        dx12->layoutRevision.load(std::memory_order_acquire);
    if (layoutRevision != dx12->cachedLayoutRevision)
    {
        QMutexLocker lock(&dx12->layoutLock);
        dx12->cachedNumScreens =
            std::min(dx12->numScreens, kMaxScreenTransforms);
        for (int index = 0; index < dx12->cachedNumScreens; ++index)
        {
            std::memcpy(
                dx12->cachedScreenMatrix[index],
                dx12->screenMatrix[index],
                sizeof(float) * 6);
            dx12->cachedScreenKind[index] = dx12->screenKind[index];
        }
        // Keep the revision observed before taking the lock. If the GUI
        // publishes another layout immediately after this copy, retaining the
        // older value forces one harmless refresh next frame instead of
        // labelling an older cache with the newer arrays' revision.
        dx12->cachedLayoutRevision = layoutRevision;
    }
    const float (*matrices)[6] = dx12->cachedScreenMatrix;
    const int* kinds = dx12->cachedScreenKind;
    const int screens = dx12->cachedNumScreens;

    const int logicalWidth = static_cast<int>(std::max(1u, publishedSurface.LogicalWidth));
    const int logicalHeight = static_cast<int>(std::max(1u, publishedSurface.LogicalHeight));
    const u32 physicalWidth = std::max(1u, publishedSurface.PhysicalWidth);
    const u32 physicalHeight = std::max(1u, publishedSurface.PhysicalHeight);
    // A software CPU frame has no renderer frame identity. Publishing a zero
    // serial is how the presenter is told so: it skips the monotonic gate for
    // this frame and leaves the last accepted GPU identity untouched, so the
    // renderer that resumes at match start is still compared against its own
    // predecessor rather than against this interlude.
    dx12->presenter.SetPresentedFrameIdentity(
        gpuFrame ? gpuFrame->Serial : 0, gpuFrame ? gpuFrame->Epoch : 0);
    if (!dx12->presenter.IsPresentedFrameIdentityMonotonic())
        return;
    if (gpuFrame && gpuFrame->HasDirectSampledOutput())
        dx12->presenter.PrepareDirectOutputDescriptors(*gpuFrame);
    if (!dx12->presenter.BeginFrame(physicalWidth, physicalHeight))
    {
        if (dx12->presenter.LastBeginWasBackpressure())
            return;
        requestNativeSurfaceVisible(false);
        reportRuntimeFailure(dx12->presenter.LastError().c_str());
        return;
    }

    // BeginFrame retires the preceding presentation command list, so its
    // compositor slot can be returned before this frame retains another one.
    dx12->frameLease.ReleaseNow();
    if (gpuFrame)
        dx12->frameLease = std::move(outputLease);

    bool screenUploaded[2]{false, false};
    for (int index = 0; index < screens; ++index)
    {
        const int kind = kinds[index] & 1;
        if (screenUploaded[kind])
            continue;
        const auto layer = kind == 0
            ? MelonPrime::DX12SurfacePresenter::Layer::ScreenTop
            : MelonPrime::DX12SurfacePresenter::Layer::ScreenBottom;
        if (gpuFrame)
        {
            screenUploaded[kind] = gpuFrame->HasDirectSampledOutput()
                ? dx12->presenter.UploadLayerFromTexture(layer, *gpuFrame)
                : dx12->presenter.UploadLayerFromBuffer(
                    layer, *gpuFrame,
                    kind == 0 ? gpuFrame->TopOffset : gpuFrame->BottomOffset);
        }
        else
        {
            screenUploaded[kind] = dx12->presenter.UploadLayer(
                layer,
                kind == 0 ? static_cast<const void*>(cpuTop) : static_cast<const void*>(cpuBottom),
                sourceWidth,
                sourceHeight,
                static_cast<std::size_t>(sourceWidth) * sizeof(u32));
        }
    }

    const float viewportWidth = static_cast<float>(dx12->presenter.GetWidth());
    const float viewportHeight = static_cast<float>(dx12->presenter.GetHeight());
    const float scaleX = viewportWidth / static_cast<float>(logicalWidth);
    const float scaleY = viewportHeight / static_cast<float>(logicalHeight);

#ifdef MELONPRIME_CUSTOM_HUD
    QImage bottomScreenImage;
    if (cpuBottom)
    {
        bottomScreenImage = QImage(
            reinterpret_cast<const uchar*>(cpuBottom),
            static_cast<int>(sourceWidth),
            static_cast<int>(sourceHeight),
            static_cast<int>(sourceWidth * sizeof(u32)),
            QImage::Format_RGB32);
    }

    bool hudLayerReady = false;
    bool hudVisible = false;
    m_hudVisualFrameWasReused = false;
    auto* mpForHud = emuThread->GetMelonPrimeCore();
    const bool hudEditMode = mpForHud
        && MelonPrime::CustomHud_IsEditMode(mpForHud->HudConfigState());
    if (MelonPrimeHud_CanRenderForCore(mpForHud, hudEditMode))
    {
        if (dx12->hudFrame.width() != logicalWidth
            || dx12->hudFrame.height() != logicalHeight
            || dx12->hudFrame.format() != QImage::Format_ARGB32_Premultiplied)
        {
            dx12->hudFrame = QImage(
                logicalWidth, logicalHeight, QImage::Format_ARGB32_Premultiplied);
            dx12->hudFrame.fill(Qt::transparent);
        }
        const QRect previousHudDirty = m_hudPrevDirty;
        QPainter painter(&dx12->hudFrame);
        // hudFrame is retained between frames so only the current HUD dirty
        // rectangle needs uploading. SourceOver would leave old pixels behind
        // wherever Overlay[0] became transparent (most visibly a moving
        // crosshair), because transparent source pixels do not erase the
        // destination. Source replacement makes the transparent pixels clear
        // the retained image while preserving the dirty-only upload.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
#define MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE (cpuBottom ? &bottomScreenImage : nullptr)
#define MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE 1
#include "MelonPrimeHudScreenCppOverlayOfSoftware.inc"
#undef MELONPRIME_HUD_SKIP_RETAINED_TARGET_REUSE_COMPOSITE
#undef MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE
        painter.end();

        auto& instcfg = emuInstance->getLocalConfig();
        hudVisible = MelonPrimeHud_IsHudVisibleOrRestorePatch(
            emuInstance, instcfg, mpForHud, m_hudEnabled, hudEditMode);
        // DX12 retains the HUD texture between compositions. The source
        // painter clears the previous dirty region before drawing the current
        // one, so both regions must be uploaded when the visual changes.
        dx12->hudRect = m_hudPrevDirty.united(previousHudDirty).intersected(
            QRect(0, 0, logicalWidth, logicalHeight));
        const auto hudLayer = MelonPrime::DX12SurfacePresenter::Layer::Hud;
        if (hudVisible && m_hudVisualFrameWasReused
            && dx12->presenter.HasLayerContent(hudLayer)
            && !dx12->hudRect.isEmpty())
        {
            // Upload is intentionally skipped, but the retained layer still
            // has to participate in this presentation's composition.
            hudLayerReady = true;
        }
        else if (hudVisible && !m_hudVisualFrameWasReused
                 && !dx12->hudRect.isEmpty())
        {
            MelonPrimePerf::ScopedHudPhase uploadPrepareTimer(
                MelonPrimePerf::HudPhase::UploadPrepare);
            MelonPrimePerf::ScopedHudPhase gpuUploadTimer(
                MelonPrimePerf::HudPhase::GpuUpload);
            MelonPrimePerf::CountHudUploadCall();
            const int patchWidth = dx12->hudRect.width();
            const int patchHeight = dx12->hudRect.height();
            const bool hudUploadPerformed = dx12->presenter.UploadLayerRegion(
                hudLayer,
                dx12->hudFrame.constBits(),
                static_cast<u32>(dx12->hudRect.x()),
                static_cast<u32>(dx12->hudRect.y()),
                static_cast<u32>(patchWidth),
                static_cast<u32>(patchHeight),
                static_cast<std::size_t>(dx12->hudFrame.bytesPerLine()));
            hudLayerReady = hudUploadPerformed;
        }
    }

    bool gpuRadarVisible = false;
    MelonPrime::DX12SurfacePresenter::Quad gpuRadarQuad;
    u32 gpuRadarCenterY = 0;
    // Keep the native colour-key pass under the same visibility contract as
    // the CPU HUD image. BtmOverlayEnable remains configured when CustomHUD is
    // toggled off and must not independently paint the top screen.
    if (gpuFrame && hudVisible && m_radarEnable)
    {
        const float* topMatrix = nullptr;
        for (int index = 0; index < screens; ++index)
        {
            if ((kinds[index] & 1) == 0)
            {
                topMatrix = matrices[index];
                break;
            }
        }
        if (mpForHud && topMatrix && MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                mpForHud->HudConfigState(),
                emuInstance, mpForHud->GetCurrentRom(), mpForHud->GetPlayerPosition()))
        {
            if (!screenUploaded[1])
            {
                screenUploaded[1] = gpuFrame->HasDirectSampledOutput()
                    ? dx12->presenter.UploadLayerFromTexture(
                        MelonPrime::DX12SurfacePresenter::Layer::ScreenBottom, *gpuFrame)
                    : dx12->presenter.UploadLayerFromBuffer(
                        MelonPrime::DX12SurfacePresenter::Layer::ScreenBottom,
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
            const u8 hunter = std::min<u8>(
                mpForHud->GetHunterID(), MelonPrime::kHunterCount - 1);
            gpuRadarCenterY = static_cast<u32>(MelonPrime::kBtmOverlaySrcCenterY[hunter]);
            gpuRadarVisible = screenUploaded[1]
                && m_radarSrcRadius > 0 && m_radarOpacity > 0.0f;
        }
    }
#endif

    QSize osdSize;
    bool osdUploaded = false;
    osdUpdate();
    if (osdEnabled)
    {
        QMutexLocker osdLock(&osdMutex);
        int stripWidth = 0;
        int stripHeight = 0;
        for (const OSDItem& item : osdItems)
        {
            if (!item.bitmap.isNull())
            {
                stripWidth = std::max(stripWidth, item.bitmap.width());
                stripHeight += item.bitmap.height();
            }
        }
        if (stripWidth > 0 && stripHeight > 0)
        {
            if (dx12->osdStrip.width() != stripWidth
                || dx12->osdStrip.height() != stripHeight
                || dx12->osdStrip.format() != QImage::Format_ARGB32_Premultiplied)
            {
                dx12->osdStrip = QImage(
                    stripWidth, stripHeight, QImage::Format_ARGB32_Premultiplied);
            }
            dx12->osdStrip.fill(Qt::transparent);
            int y = 0;
            for (const OSDItem& item : osdItems)
            {
                if (item.bitmap.isNull())
                    continue;
                const int rowBytes = item.bitmap.width() * 4;
                for (int row = 0; row < item.bitmap.height(); ++row)
                {
                    std::memcpy(
                        dx12->osdStrip.scanLine(y + row),
                        item.bitmap.constScanLine(row),
                        static_cast<std::size_t>(rowBytes));
                }
                y += item.bitmap.height();
            }
            osdSize = QSize(stripWidth, stripHeight);
            osdUploaded = dx12->presenter.UploadLayer(
                MelonPrime::DX12SurfacePresenter::Layer::Osd,
                dx12->osdStrip.constBits(),
                static_cast<u32>(stripWidth),
                static_cast<u32>(stripHeight),
                static_cast<std::size_t>(dx12->osdStrip.bytesPerLine()));
        }
    }

    dx12->presenter.BeginComposition();
    for (int index = 0; index < screens; ++index)
    {
        const int kind = kinds[index] & 1;
        if (!screenUploaded[kind])
            continue;
        const float* matrix = matrices[index];
        MelonPrime::DX12SurfacePresenter::Quad quad;
        quad.Axis[0] = matrix[0] * 256.0f * scaleX;
        quad.Axis[1] = matrix[1] * 256.0f * scaleY;
        quad.Axis[2] = matrix[2] * 192.0f * scaleX;
        quad.Axis[3] = matrix[3] * 192.0f * scaleY;
        quad.Origin[0] = matrix[4] * scaleX;
        quad.Origin[1] = matrix[5] * scaleY;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;
        dx12->presenter.DrawLayer(
            kind == 0 ? MelonPrime::DX12SurfacePresenter::Layer::ScreenTop
                      : MelonPrime::DX12SurfacePresenter::Layer::ScreenBottom,
            quad,
            MelonPrime::DX12SurfacePresenter::Blend::Opaque,
            filter);
    }

#ifdef MELONPRIME_CUSTOM_HUD
    if (hudLayerReady)
    {
        MelonPrime::DX12SurfacePresenter::Quad quad;
        quad.Axis[0] = static_cast<float>(dx12->hudRect.width()) * scaleX;
        quad.Axis[3] = static_cast<float>(dx12->hudRect.height()) * scaleY;
        quad.Origin[0] = static_cast<float>(dx12->hudRect.x()) * scaleX;
        quad.Origin[1] = static_cast<float>(dx12->hudRect.y()) * scaleY;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;
        dx12->presenter.DrawLayer(
            MelonPrime::DX12SurfacePresenter::Layer::Hud,
            quad,
            MelonPrime::DX12SurfacePresenter::Blend::Premultiplied,
            filter);
    }

    // The GPU colour-key pass is intentionally after the SVG/frame HUD layer:
    // picked radar pixels are the foremost Custom HUD layer.
    if (gpuRadarVisible)
    {
        dx12->presenter.DrawRadar(
            gpuRadarQuad, m_radarOpacity, gpuRadarCenterY,
            static_cast<u32>(m_radarSrcRadius));
    }
#endif

    if (osdUploaded)
    {
        MelonPrime::DX12SurfacePresenter::Quad quad;
        quad.Axis[0] = static_cast<float>(osdSize.width()) * scaleX;
        quad.Axis[3] = static_cast<float>(osdSize.height()) * scaleY;
        quad.Origin[0] = static_cast<float>(kOSDMargin) * scaleX;
        quad.Origin[1] = static_cast<float>(kOSDMargin) * scaleY;
        quad.Origin[2] = viewportWidth;
        quad.Origin[3] = viewportHeight;
        dx12->presenter.DrawLayer(
            MelonPrime::DX12SurfacePresenter::Layer::Osd,
            quad,
            MelonPrime::DX12SurfacePresenter::Blend::Premultiplied,
            false);
    }

    if (!dx12->presenter.EndFrame())
    {
        requestNativeSurfaceVisible(false);
        reportRuntimeFailure(dx12->presenter.LastError().c_str());
        return;
    }

    const bool vsync = presentation.vsync;
    // The presenter fires the vendor Present markers itself, around the DXGI
    // call. This panel does not mediate low-latency state.
    const bool presented = dx12->presenter.Present(vsync);

    if (!presented)
    {
        requestNativeSurfaceVisible(false);
        reportRuntimeFailure(dx12->presenter.LastError().c_str());
        return;
    }

    requestNativeSurfaceVisible(true);
    if (gpuFrame)
        dx12->nativeVisibility.Accept(gpuFrame->Epoch, gpuFrame->Serial);
    else
        dx12->nativeVisibility.AcceptWithoutIdentity();
}

void ScreenPanelDX12::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);

    auto* emuThread = emuInstance->getEmuThread();
    if (emuThread->emuIsActive())
    {
        QMutexLocker fallbackLock(&dx12->fallbackLock);
        if (!dx12->fallbackFrame.isNull())
            painter.drawImage(QPoint(0, 0), dx12->fallbackFrame);
        return;
    }

    osdUpdate();
    QMutexLocker osdLock(&osdMutex);
    painter.drawPixmap(QRect(splashPos[3], QSize(kLogoWidth, kLogoWidth)), splashLogo);
    for (int index = 0; index < 3; ++index)
        painter.drawImage(splashPos[index], splashText[index].bitmap);
}

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
