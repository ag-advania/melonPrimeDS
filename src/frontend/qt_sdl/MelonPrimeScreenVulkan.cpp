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
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
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
#if defined(__linux__)  // scatter-budget-exempt: Linux Vulkan WSI host ownership, not input dispatch
#include "MelonPrimeVulkanSurfaceHostLinux.h"
#endif
#include "NDS.h"
#include "Platform.h"
#include "main.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeHudRadar.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudPresentationState.h"
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

template <typename Function>
class ScopeExit final
{
public:
    explicit ScopeExit(Function function)
        : FunctionValue(std::move(function))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ~ScopeExit() { FunctionValue(); }

private:
    Function FunctionValue;
};

template <typename Function>
ScopeExit<Function> MakeScopeExit(Function function)
{
    return ScopeExit<Function>(std::move(function));
}

MelonPrime::VulkanSurface::NativeWindowSnapshot MakeVulkanSnapshot(
    const MelonPrime::NativeSurfaceSnapshot& source)
{
    MelonPrime::VulkanSurface::NativeWindowSnapshot snapshot;
    snapshot.Generation = source.IdentityGeneration;
    snapshot.WindowId = static_cast<unsigned long long>(source.NativeHandle);
    snapshot.XcbConnection = reinterpret_cast<void*>(source.XcbConnection);
    snapshot.XlibDisplay = reinterpret_cast<void*>(source.XlibDisplay);
    snapshot.Width = source.PhysicalWidth;
    snapshot.Height = source.PhysicalHeight;
    snapshot.Valid = source.Valid;
#if defined(_WIN32)
    snapshot.Platform = "windows";
#elif defined(__APPLE__)  // scatter-budget-exempt: WSI snapshot platform label, not input dispatch
    snapshot.Platform = "cocoa";
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    snapshot.Platform = "xcb";
#else
    snapshot.Platform = "unknown";
#endif
    return snapshot;
}

#if defined(__linux__)  // scatter-budget-exempt: CI-only Vulkan WSI smoke gate, not input dispatch
bool IsVulkanRuntimeSmokeEnabled()
{
    static const bool enabled = [] {
        const char* const value = std::getenv("MELONPRIME_VULKAN_SMOKE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
#endif

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
#if defined(__linux__)  // scatter-budget-exempt: Linux Vulkan WSI host selection, not input dispatch
using VulkanSurfaceHost = MelonPrime::VulkanSurfaceHostLinux;
#else
class VulkanSurfaceHost final : public QWidget
{
public:
    using LifecycleCallback = std::function<void(QEvent::Type, bool)>;

    explicit VulkanSurfaceHost(QWidget* parent, LifecycleCallback callback = {})
        : QWidget(parent), Lifecycle(std::move(callback))
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
#endif

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
    std::atomic<std::uint32_t> layoutRevision{0};
    // Emulation-thread cache. The GUI-published arrays above are copied only
    // when layoutRevision changes; a stable frame never takes layoutLock.
    float cachedScreenMatrix[kMaxScreenTransforms][6]{};
    int cachedScreenKind[kMaxScreenTransforms]{};
    int cachedNumScreens = 0;
    std::uint32_t cachedLayoutRevision = ~0u;

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

    // Identity of the last native Vulkan renderer frame whose screen layers
    // are retained in the presenter. This is deliberately POD-only: the
    // presentation hot path compares renderer-owned handles/serials instead
    // of hashing pixels or allocating a cache object.
    struct RetainedScreenKey
    {
        const void* rendererIdentity = nullptr;
        u64 serial = 0;
        u64 resourceGeneration = 0;
        std::uint64_t presentationSurfaceGeneration = 0;
        u32 width = 0;
        u32 height = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage directImageTop = VK_NULL_HANDLE;
        VkImageView directImageViewTop = VK_NULL_HANDLE;
        VkImage directImageBottom = VK_NULL_HANDLE;
        VkImageView directImageViewBottom = VK_NULL_HANDLE;
        bool directSampled = false;
        bool valid = false;
    } retainedScreenKey;
    u8 retainedScreenLayerMask = 0;
    // A direct sampled frame remains owned while the presenter can draw its
    // retained image across multiple presenter slots. Buffer output is copied
    // into presenter-owned images and therefore needs no extra source lease.
    RendererOutputLease retainedScreenLease;

    // Renderer the VBlank observer is currently installed on, so the hook is
    // (re)installed exactly once per renderer instance.
    melonDS::VulkanRenderer* hookedRenderer = nullptr;
    std::uint32_t rendererSnapshotRevision = ~0u;
    melonDS::VulkanRenderer* cachedVulkanRenderer = nullptr;

    QImage osdStrip;

    std::atomic_bool surfaceVisibleRequested{false};
    // Set by the GUI thread while either the on-screen editor or the Custom
    // HUD settings live preview owns the paused presentation path.
    std::atomic_bool hudEditLivePresentation{false};
    std::atomic_bool hudLivePreviewPresentation{false};
    std::atomic_bool modalPauseActive{false};
#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface lifecycle state, not input dispatch
    // GUI-thread lifecycle publication and presenter-thread consumption. The
    // lifecycle object is the sole authority for native-surface eligibility;
    // the generation, not WId, is the native-surface identity on Wayland.
    std::atomic_bool linuxSurfaceDirty{true};
    std::shared_ptr<MelonPrime::VulkanSurfaceLifecycle> linuxSurfaceLifecycle =
        std::make_shared<MelonPrime::VulkanSurfaceLifecycle>();
    // Only the emulation thread writes this frame-local copy after the
    // lifecycle lease has published it. GUI code publishes through
    // VulkanSurfaceLifecycle and never touches this member.
    MelonPrime::VulkanSurface::NativeWindowSnapshot linuxFrameSnapshot;
#endif

    // Published by GUI resize/lifecycle callbacks so the emulation thread does
    // not query QWidget geometry or devicePixelRatioF().
    std::atomic_uint surfaceLogicalWidth{1};
    std::atomic_uint surfaceLogicalHeight{1};
    std::atomic_uint surfacePhysicalWidth{1};
    std::atomic_uint surfacePhysicalHeight{1};
    MelonPrime::NativeSurfaceSnapshotStore surfaceSnapshot;
    // GUI-thread-only publication bookkeeping. The emulation thread consumes
    // only surfaceSnapshot and the presenter-bound copies below.
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

    // The values the emulation thread most recently asked for. The presenter
    // is the authority once it exists; these are the panel-side record, read by
    // initVulkanPresenter() so a presenter created mid-session (renderer switch,
    // native handle change) starts at the current setting instead of off.
    std::atomic_int reflexMode{0};
    std::atomic_bool antiLag2Enabled{false};
    std::atomic_bool windowFullscreen{false};

    bool initialized = false;
    bool runtimeFailureReported = false;
    bool vsyncApplied = true;
    // A native surface stays visible during transient producer backpressure,
    // but startup must never expose the Software 2D + placeholder 3D hybrid.
    MelonPrime::NativeVisibilityState nativeVisibility;

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

#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface lifecycle callback, not input dispatch
    vulkan->surface = new VulkanSurfaceHost(
        this,
        [this](
            MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent event,
            const MelonPrime::VulkanSurface::NativeWindowSnapshot& snapshot) {
            if (!vulkan)
                return;

            VulkanPerf::AddCounter(
                VulkanPerf::Counter::VulkanSurfaceEventCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::VulkanSurfaceSnapshotPublishCount);
            VulkanPerf::AddCounter(
                VulkanPerf::Counter::VulkanNativeIdentityGenerationChangeCount);

            const bool invalidated = event
                == MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::Hide
                || event
                == MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::SurfaceAboutToBeDestroyed;
            // VulkanSurfaceHostLinux owns the atomic post-event publication of
            // Snapshot, HostShown, and lifecycle State. This callback only
            // marks dependent work dirty and records the already-published
            // snapshot; it must not create a second lifecycle authority.
            vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);

            const char* eventName = "lifecycle";
            switch (event)
            {
            case MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::Show:
                eventName = "Show";
                break;
            case MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::Hide:
                eventName = "Hide";
                break;
            case MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::WinIdChange:
                eventName = "WinIdChange";
                break;
            case MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::SurfaceCreated:
                eventName = "SurfaceCreated";
                break;
            case MelonPrime::VulkanSurfaceHostLinux::LifecycleEvent::SurfaceAboutToBeDestroyed:
                eventName = "SurfaceAboutToBeDestroyed";
                break;
            }
            const void* native = snapshot.WaylandSurface
                ? snapshot.WaylandSurface
                : (snapshot.XcbConnection ? snapshot.XcbConnection : snapshot.XlibDisplay);
            Platform::Log(
                Platform::LogLevel::Info,
                "[Vulkan][LinuxWSI] %s generation=%llu native=%p platform=%s\n",
                eventName,
                static_cast<unsigned long long>(snapshot.Generation),
                native,
                snapshot.Platform.empty() ? "unknown" : snapshot.Platform.c_str());
            if (invalidated)
            {
                Platform::Log(
                    Platform::LogLevel::Info,
                    "[Vulkan][LinuxWSI] surface invalidated new-generation=%llu\n",
                    static_cast<unsigned long long>(snapshot.Generation));
            }
        },
        vulkan->linuxSurfaceLifecycle);
#else
    vulkan->surface = new VulkanSurfaceHost(
        this,
        [this](QEvent::Type eventType, bool aboutToDestroy) {
            handleNativeSurfaceHostLifecycleGuiThread(eventType, aboutToDestroy);
        });
#endif
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
    // its `panel` pointer under screenPanelLock before deleting it. The
    // renderer itself might already be gone during application teardown, so do
    // not dereference the borrowed renderer hook pointer from this destructor.
    prepareForRendererTransition(false);
#if defined(__linux__)  // scatter-budget-exempt: Linux Vulkan native presenter teardown, not input/runtime dispatch
    retireLinuxPresenterForPanelDestruction();
#endif
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

    // Keep the native child hidden during startup. Linux/Wayland must expose
    // it before the presenter attempts to bind a VkSurfaceKHR; the first bind
    // therefore happens on the emulation thread after the host publishes a
    // post-Show snapshot.
    vulkan->surface->setGeometry(rect());
#if defined(__linux__)  // scatter-budget-exempt: Linux GUI geometry publication, not input dispatch
    vulkan->surfaceLogicalWidth.store(
        static_cast<unsigned>(std::max(1, width())), std::memory_order_release);
    vulkan->surfaceLogicalHeight.store(
        static_cast<unsigned>(std::max(1, height())), std::memory_order_release);
    vulkan->surfacePhysicalWidth.store(
        static_cast<unsigned>(std::max(1, qRound(width() * devicePixelRatioF()))),
        std::memory_order_release);
    vulkan->surfacePhysicalHeight.store(
        static_cast<unsigned>(std::max(1, qRound(height() * devicePixelRatioF()))),
        std::memory_order_release);
    vulkan->initialized = true;
    refreshNativeSurfaceGuiThread();
    return true;
#else
    // The GUI thread realizes and publishes the native child identity. The
    // presenter consumes that immutable snapshot, including on a later
    // fullscreen/native-surface rebind.
    refreshNativeSurfaceGuiThread();

    if (!initVulkanPresenter())
        return false;

    vulkan->initialized = true;
    refreshNativeSurfaceGuiThread();
    return true;
#endif
}


bool ScreenPanelVulkan::initVulkanPresenter()
{
    if (!vulkan || !vulkan->surface)
        return false;
    if (vulkan->presenter.IsInitialized())
        return true;
#ifdef MELONPRIME_CUSTOM_HUD
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif
#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface readiness, not input dispatch
    if (!vulkan->linuxSurfaceLifecycle->presentationReady())
    {
        return false;
    }
    const MelonPrime::VulkanSurface::NativeWindowSnapshot snapshot =
        vulkan->linuxFrameSnapshot;
    if (!snapshot.IsValid())
        return false;
#else
    MelonPrime::NativeSurfaceSnapshot published;
    if (!vulkan->surfaceSnapshot.Read(published) || !published.Valid)
        return false;
    const MelonPrime::VulkanSurface::NativeWindowSnapshot snapshot =
        MakeVulkanSnapshot(published);
    if (!snapshot.IsValid())
        return false;
#endif

    // Before Init(), not after: the present mode is baked into the swapchain,
    // so applying the setting afterwards would build one swapchain with the
    // default mode and immediately throw it away -- and the startup log line
    // would claim a VSync state the user did not ask for.
    Config::Table config = emuInstance->getGlobalConfig();
    vulkan->vsyncApplied = config.GetBool("Screen.VSync");
    vulkan->presenter.SetVSync(vulkan->vsyncApplied);
#if defined(__linux__)  // scatter-budget-exempt: Linux presenter fullscreen snapshot, not input dispatch
    vulkan->presenter.SetWindowFullscreen(
        vulkan->windowFullscreen.load(std::memory_order_acquire));
#else
    vulkan->presenter.SetWindowFullscreen(published.Fullscreen);
#endif
    vulkan->presenter.SetGenericPresentPacingPolicy(
        config.GetInt(MelonPrime::CfgKey::VulkanPresentPacingPolicy));

    const int reflexMode = config.GetInt(MelonPrime::CfgKey::NvidiaReflexMode);
    const bool antiLag2Enabled = config.GetBool(MelonPrime::CfgKey::AmdAntiLag2Enabled);
    vulkan->reflexMode.store(reflexMode, std::memory_order_relaxed);
    vulkan->antiLag2Enabled.store(antiLag2Enabled, std::memory_order_relaxed);

    const bool presenterInitialized = vulkan->presenter.Init(snapshot);
    if (!presenterInitialized)
    {
#if defined(__linux__)  // scatter-budget-exempt: Linux surface-loss recovery, not input dispatch
        if (vulkan->presenter.NeedsSurfaceRebind())
        {
            vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
            vulkan->linuxSurfaceLifecycle->markSurfaceLost();
            return false;
        }
#endif
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

#if defined(__linux__)  // scatter-budget-exempt: Linux presenter generation publication, not input dispatch
    vulkan->presenterSurfaceIdentityGeneration = snapshot.Generation;
    vulkan->linuxSurfaceDirty.store(false, std::memory_order_release);
    vulkan->linuxSurfaceLifecycle->markBound(snapshot.Generation);
    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan][LinuxWSI] presenter bind generation=%llu\n",
        static_cast<unsigned long long>(snapshot.Generation));
#else
    vulkan->presenterSurfaceIdentityGeneration = published.IdentityGeneration;
    vulkan->presenterSurfaceGeometryRevision = published.GeometryRevision;
    vulkan->presenterSurfaceHandle = published.NativeHandle;
#endif

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
    if (vulkan->presenter.NeedsSurfaceRebind())
    {
        // VK_ERROR_SURFACE_LOST_KHR is a compositor lifecycle transition, not
        // a renderer failure. The Linux frame boundary retires and rebinds the
        // presenter without touching VulkanFeatureCheck's sticky failure bit.
        return;
    }
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

#if defined(__linux__)  // scatter-budget-exempt: Linux presentation lease handshake, not input dispatch
bool ScreenPanelVulkan::beginLinuxPresentationFrame()
{
    if (!vulkan || !vulkan->linuxSurfaceLifecycle)
        return false;

    MelonPrime::VulkanSurface::NativeWindowSnapshot snapshot;
    if (!vulkan->linuxSurfaceLifecycle->beginFrame(snapshot))
        return false;

    // beginFrame() copied an immutable, post-Show snapshot while holding the
    // lifecycle lease. Do not re-check a second readiness/generation mirror:
    // the lifecycle object is authoritative, and a GUI transition that races
    // this point is retired through the active-frame lease.
    if (!snapshot.IsValid())
    {
        vulkan->linuxSurfaceLifecycle->endFrame();
        return false;
    }

    vulkan->linuxFrameSnapshot = snapshot;
    return true;
}


void ScreenPanelVulkan::finishLinuxPresentationFrame()
{
    if (!vulkan || !vulkan->linuxSurfaceLifecycle)
        return;

    // If the GUI requested a native transition while this frame was in
    // progress, retire the presenter before releasing the active-frame lease.
    // The GUI-side wait can then observe DestroySafe without covering any
    // Vulkan call with a lifecycle mutex.
    if (vulkan->linuxSurfaceLifecycle->retireRequested())
        serviceLinuxSurfaceRetire();
    vulkan->linuxSurfaceLifecycle->endFrame();
}


void ScreenPanelVulkan::serviceLinuxSurfaceRetire()
{
    if (!vulkan || !vulkan->linuxSurfaceLifecycle
        || !vulkan->linuxSurfaceLifecycle->retireRequested())
    {
        return;
    }

    vulkan->linuxSurfaceLifecycle->beginRetiring();
    if (vulkan->presenter.IsInitialized())
    {
        retireLinuxPresentationSurface("native lifecycle transition");
    }
    else
    {
        for (RendererOutputLease& lease : vulkan->frameLeases)
            lease.ReleaseNow();
        invalidateScreenRetention();
        // The presenter may already have been torn down by a recoverable
        // surface-loss path. Complete the lifecycle handshake even in that
        // case; otherwise Retiring remains latched and the GUI-side native
        // destruction barrier cannot complete.
        vulkan->linuxSurfaceLifecycle->markPresenterRetired();
    }
}
#endif

bool ScreenPanelVulkan::nativeSurfaceReady() const
{
#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface readiness query, not input dispatch
    return vulkan && vulkan->linuxSurfaceLifecycle
        && vulkan->linuxSurfaceLifecycle->presentationReady();
#else
    MelonPrime::NativeSurfaceSnapshot snapshot;
    return vulkan && vulkan->surfaceSnapshot.Read(snapshot) && snapshot.Valid;
#endif
}


void ScreenPanelVulkan::publishNativeSurfaceSnapshotGuiThread()
{
    if (!vulkan || !vulkan->surface)
        return;

#if defined(__linux__)  // scatter-budget-exempt: Linux surface snapshot authority deferred to VulkanSurfaceHostLinux, not input dispatch
    // VulkanSurfaceHostLinux owns the richer X11/Wayland snapshot and its
    // lifecycle lease. Do not create a second authority here.
    return;
#else
    const std::uintptr_t handle = static_cast<std::uintptr_t>(vulkan->surface->winId());
    const bool identityChanged = vulkan->guiSurfaceIdentityGeneration == 0
        || vulkan->guiSurfaceHandle != handle
        || vulkan->guiSurfaceIdentityDirty;
    if (identityChanged)
    {
        vulkan->guiSurfaceHandle = handle;
        ++vulkan->guiSurfaceIdentityGeneration;
        vulkan->guiSurfaceIdentityDirty = false;
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanNativeIdentityGenerationChangeCount);
    }

    const int logicalWidth = std::max(1, width());
    const int logicalHeight = std::max(1, height());
    const qreal dpr = devicePixelRatioF();
    const std::uint32_t physicalWidth = static_cast<std::uint32_t>(
        std::max(1, qRound(logicalWidth * dpr)));
    const std::uint32_t physicalHeight = static_cast<std::uint32_t>(
        std::max(1, qRound(logicalHeight * dpr)));
    const bool fullscreen = window() && window()->isFullScreen();
    if (!vulkan->guiSurfaceGeometryInitialized
        || vulkan->guiSurfaceLastLogicalWidth != static_cast<std::uint32_t>(logicalWidth)
        || vulkan->guiSurfaceLastLogicalHeight != static_cast<std::uint32_t>(logicalHeight)
        || vulkan->guiSurfaceLastPhysicalWidth != physicalWidth
        || vulkan->guiSurfaceLastPhysicalHeight != physicalHeight
        || vulkan->guiSurfaceLastFullscreen != fullscreen)
    {
        vulkan->guiSurfaceGeometryInitialized = true;
        vulkan->guiSurfaceLastLogicalWidth = static_cast<std::uint32_t>(logicalWidth);
        vulkan->guiSurfaceLastLogicalHeight = static_cast<std::uint32_t>(logicalHeight);
        vulkan->guiSurfaceLastPhysicalWidth = physicalWidth;
        vulkan->guiSurfaceLastPhysicalHeight = physicalHeight;
        vulkan->guiSurfaceLastFullscreen = fullscreen;
        ++vulkan->guiSurfaceGeometryRevision;
    }
    MelonPrime::NativeSurfaceSnapshot snapshot;
    snapshot.NativeHandle = handle;
    snapshot.IdentityGeneration = vulkan->guiSurfaceIdentityGeneration;
    snapshot.GeometryRevision = vulkan->guiSurfaceGeometryRevision;
    snapshot.LogicalWidth = static_cast<std::uint32_t>(logicalWidth);
    snapshot.LogicalHeight = static_cast<std::uint32_t>(logicalHeight);
    snapshot.PhysicalWidth = physicalWidth;
    snapshot.PhysicalHeight = physicalHeight;
    snapshot.Fullscreen = fullscreen;
    snapshot.Valid = handle != 0 && vulkan->surface->windowHandle() != nullptr;
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (snapshot.Valid && QGuiApplication::platformName() == QStringLiteral("xcb"))
    {
        const QNativeInterface::QX11Application* x11 =
            qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
        if (x11)
        {
            snapshot.XcbConnection = reinterpret_cast<std::uintptr_t>(x11->connection());
            snapshot.XlibDisplay = reinterpret_cast<std::uintptr_t>(x11->display());
        }
        snapshot.Valid = snapshot.XcbConnection != 0 || snapshot.XlibDisplay != 0;
    }
    else
#endif
    {
        snapshot.Valid = false;
    }
#endif
    vulkan->surfaceSnapshot.Publish(snapshot);
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::VulkanSurfaceSnapshotPublishCount);
    vulkan->windowFullscreen.store(snapshot.Fullscreen, std::memory_order_release);
#endif
}


void ScreenPanelVulkan::handleNativeSurfaceHostLifecycleGuiThread(
    QEvent::Type eventType,
    bool aboutToDestroy)
{
    if (!vulkan)
        return;

    VulkanPerf::AddCounter(
        VulkanPerf::Counter::VulkanSurfaceEventCount);

    if (aboutToDestroy || eventType == QEvent::PlatformSurface)
        vulkan->guiSurfaceIdentityDirty = true;
    if (aboutToDestroy)
    {
        MelonPrime::NativeSurfaceSnapshot invalid;
        invalid.IdentityGeneration = ++vulkan->guiSurfaceIdentityGeneration;
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanNativeIdentityGenerationChangeCount);
        invalid.GeometryRevision = vulkan->guiSurfaceGeometryRevision;
        vulkan->surfaceSnapshot.Publish(invalid);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanSurfaceSnapshotPublishCount);
        return;
    }

    // The child has completed QWidget::event(), so winId()/windowHandle() are
    // now safe to sample on the GUI thread. Coalesce multiple native events;
    // the next queued publication carries one new identity generation.
    (void)eventType;
    QMetaObject::invokeMethod(
        this, [this]() { refreshNativeSurfaceGuiThread(); }, Qt::QueuedConnection);
}


void ScreenPanelVulkan::refreshNativeSurfaceGuiThread()
{
    if (!vulkan || !vulkan->surface)
        return;

    vulkan->surface->setGeometry(rect());
#if defined(__linux__)  // scatter-budget-exempt: Linux GUI geometry publication, not input dispatch
    vulkan->surfaceLogicalWidth.store(
        static_cast<unsigned>(std::max(1, width())), std::memory_order_release);
    vulkan->surfaceLogicalHeight.store(
        static_cast<unsigned>(std::max(1, height())), std::memory_order_release);
    vulkan->surfacePhysicalWidth.store(
        static_cast<unsigned>(std::max(1, qRound(width() * devicePixelRatioF()))),
        std::memory_order_release);
    vulkan->surfacePhysicalHeight.store(
        static_cast<unsigned>(std::max(1, qRound(height() * devicePixelRatioF()))),
        std::memory_order_release);
    vulkan->windowFullscreen.store(
        window() && window()->isFullScreen(), std::memory_order_release);
    vulkan->presenter.NotifySurfaceChanged();
#else
#if defined(__APPLE__)  // scatter-budget-exempt: macOS presentation-layer geometry sync, not input dispatch
    // macOS is the only platform where the presentation layer has a size of its
    // own; it remains a GUI-thread-only geometry update.
    if (vulkan->presenter.IsInitialized())
    MelonPrime::VulkanSurface::UpdateGeometry(
        vulkan->presenter.GetPlatformSurface(), vulkan->surface.data());
#endif
    publishNativeSurfaceSnapshotGuiThread();
    MelonPrime::NativeSurfaceSnapshot snapshot;
    if (vulkan->surfaceSnapshot.Read(snapshot))
    {
        vulkan->presenter.SetWindowFullscreen(snapshot.Fullscreen);
        vulkan->presenter.NotifySurfaceChanged(snapshot.GeometryRevision);
    }
#endif
}


void ScreenPanelVulkan::setNativeSurfaceVisibleGuiThread(bool visible)
{
    if (!vulkan || !vulkan->surface)
        return;

    if (visible)
    {
        vulkan->surface->setGeometry(rect());
#if defined(__linux__)  // scatter-budget-exempt: Linux host visibility lifecycle, not input dispatch
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan][LinuxWSI] host show requested platform=%s\n",
            QGuiApplication::platformName().toStdString().c_str());
#endif
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
    if (vulkan->surfaceVisibleRequested.load(std::memory_order_relaxed) == visible)
        return;
    if (vulkan->surfaceVisibleRequested.exchange(
            visible, std::memory_order_acq_rel) == visible)
        return;
    MelonPrimePerf::CountSurfaceVisibilityStateChange();

    QMetaObject::invokeMethod(
        this,
        [this, visible]() { setNativeSurfaceVisibleGuiThread(visible); },
        Qt::QueuedConnection);
}


void ScreenPanelVulkan::releaseNativeSurface()
{
    if (!vulkan)
        return;

    vulkan->initialized = false;
    vulkan->guiSurfaceIdentityDirty = true;
#if defined(__linux__)  // scatter-budget-exempt: Linux surface teardown notification, not input dispatch
    vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
#endif

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
    {
        if (vulkan->presenter.IsInitialized())
            retireLinuxPresentationSurface("native surface invalidated");
        return false;
    }

    const MelonPrime::VulkanSurface::NativeWindowSnapshot& snapshot =
        vulkan->linuxFrameSnapshot;
    const std::uint64_t generation = snapshot.Generation;
    if (!snapshot.IsValid())
        return false;

    if (vulkan->presenter.NeedsSurfaceRebind())
    {
        retireLinuxPresentationSurface("VK_ERROR_SURFACE_LOST_KHR");
        vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
        vulkan->linuxSurfaceLifecycle->markSurfaceLost();
        // A lost VkSurfaceKHR is not rebound against the same native snapshot.
        // The next SurfaceCreated/Show generation must publish a new native
        // identity first, which prevents a same-generation rebind storm.
        return false;
    }

    if (vulkan->presenter.IsInitialized()
        && vulkan->presenterSurfaceIdentityGeneration != generation)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan][LinuxWSI] presenter rebind old-generation=%llu new-generation=%llu\n",
            static_cast<unsigned long long>(vulkan->presenterSurfaceIdentityGeneration),
            static_cast<unsigned long long>(generation));
        retireLinuxPresentationSurface("native surface generation changed");
    }

    if (vulkan->presenter.IsInitialized())
        return true;

    const bool initialized = initVulkanPresenter();
    if (initialized)
    {
        vulkan->linuxSurfaceDirty.store(false, std::memory_order_release);
        return true;
    }
    return false;
}


void ScreenPanelVulkan::retireLinuxPresentationSurface(const char* reason)
{
    if (!vulkan || !vulkan->presenter.IsInitialized())
        return;

#ifdef MELONPRIME_CUSTOM_HUD
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif

    const std::uint64_t oldGeneration = vulkan->presenterSurfaceIdentityGeneration;
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::VulkanSurfaceRebindCount);
    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan][LinuxWSI] presenter retired generation=%llu reason=%s\n",
        static_cast<unsigned long long>(oldGeneration),
        reason ? reason : "unspecified");
    vulkan->presenter.Quiesce();
    invalidateScreenRetention();
    for (RendererOutputLease& lease : vulkan->frameLeases)
        lease.ReleaseNow();
    vulkan->presenter.Shutdown();
    vulkan->presenterSurfaceIdentityGeneration = 0;
    vulkan->presenterSurfaceGeometryRevision = 0;
    vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
    assert(!vulkan->presenter.IsInitialized());
    if (vulkan->linuxSurfaceLifecycle)
        vulkan->linuxSurfaceLifecycle->markPresenterRetired();
}


void ScreenPanelVulkan::retireLinuxPresenterForPanelDestruction()
{
    if (!vulkan || !vulkan->linuxSurfaceLifecycle)
        return;

    // The panel was removed from the MainWindow/instance registry before its
    // destructor reaches here, so the GUI thread is the exclusive owner of
    // this final native-presenter teardown. Keep the lifecycle retirement
    // request paired with the actual Shutdown performed by the helper below.
    vulkan->linuxSurfaceLifecycle->requestNativeTransitionRetire();
    vulkan->linuxSurfaceLifecycle->beginRetiring();

    if (vulkan->presenter.IsInitialized())
    {
        retireLinuxPresentationSurface("panel destruction");
    }
    else
    {
        assert(!vulkan->presenter.IsInitialized());
        vulkan->linuxSurfaceLifecycle->markPresenterRetired();
    }

    // SurfaceAboutToBeDestroyed is a hard native-destruction barrier. The
    // panel destructor follows the same rule and never hides/deletes the Qt
    // child while a lifecycle frame lease remains active.
    vulkan->linuxSurfaceLifecycle->waitForDestroySafe();
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

    {
        QMutexLocker lock(&vulkan->layoutLock);
        vulkan->numScreens = numScreens;
        for (int index = 0; index < numScreens && index < kMaxScreenTransforms; ++index)
        {
            std::memcpy(vulkan->screenMatrix[index], screenMatrix[index], sizeof(float) * 6);
            vulkan->screenKind[index] = screenKind[index];
        }
    }
    vulkan->layoutRevision.fetch_add(1, std::memory_order_release);
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
#if defined(__linux__)  // scatter-budget-exempt: Linux resize publication, not input dispatch
        vulkan->surfaceLogicalWidth.store(
            static_cast<unsigned>(std::max(1, width())), std::memory_order_release);
        vulkan->surfaceLogicalHeight.store(
            static_cast<unsigned>(std::max(1, height())), std::memory_order_release);
        vulkan->surfacePhysicalWidth.store(
            static_cast<unsigned>(std::max(1, qRound(width() * devicePixelRatioF()))),
            std::memory_order_release);
        vulkan->surfacePhysicalHeight.store(
            static_cast<unsigned>(std::max(1, qRound(height() * devicePixelRatioF()))),
            std::memory_order_release);
#else
        refreshNativeSurfaceGuiThread();
#endif
        // Coalesced, not immediate. Rebuilding here would mean one swapchain
        // per resize event during a window drag, and the GUI thread must not
        // destroy a swapchain the emulation thread may be presenting from.
#if defined(__linux__)  // scatter-budget-exempt: Linux resize publication companion, not input dispatch
        vulkan->presenter.NotifySurfaceChanged();
#endif
    }
}


bool ScreenPanelVulkan::event(QEvent* event)
{
    bool refreshSurfaceAfterBase = false;
    if (vulkan && event)
    {
        switch (event->type())
        {
        case QEvent::PlatformSurface:
        {
            const auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
            // The dedicated Linux child owns this lifecycle event and
            // publishes its generation after QWidget::event(). The panel must
            // never destroy a presenter from this GUI callback.
            (void)surfaceEvent;
#if !defined(__linux__)  // scatter-budget-exempt: non-Linux identity-authority note, not input dispatch
            // The native child publishes a lifecycle identity after its own
            // QWidget::event(); this parent event is not an identity authority.
#endif
            break;
        }

        case QEvent::ScreenChangeInternal:
        case QEvent::WindowStateChange:
        case QEvent::Show:
            // A move to another monitor, a fullscreen transition or a DPI
            // change all alter the surface's physical pixel size without
            // necessarily producing a resize event first.
#if defined(__linux__)  // scatter-budget-exempt: Linux fullscreen publication, not input dispatch
            vulkan->windowFullscreen.store(
                window() && window()->isFullScreen(), std::memory_order_release);
            vulkan->presenter.NotifySurfaceChanged();
#else
            // The base QWidget event applies the new fullscreen/screen state.
            // Publish synchronously after it returns so the emulation thread
            // never rebuilds against a pre-transition snapshot.
            refreshSurfaceAfterBase = true;
#endif
#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface refresh dispatch, not input dispatch
            QMetaObject::invokeMethod(
                this, [this]() { refreshNativeSurfaceGuiThread(); }, Qt::QueuedConnection);
#endif
            break;

        default:
            break;
        }
    }

    const bool handled = ScreenPanel::event(event);
#if !defined(__linux__)  // scatter-budget-exempt: non-Linux fullscreen refresh authority, not input dispatch
    if (refreshSurfaceAfterBase)
        refreshNativeSurfaceGuiThread();
#endif
    return handled;
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

void ScreenPanelVulkan::setHudLivePreviewActive(bool active)
{
    if (!vulkan)
        return;

    vulkan->hudLivePreviewPresentation.store(active, std::memory_order_release);
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

void ScreenPanelVulkan::beginVulkanLowLatencyFrame(
    int reflexMode,
    bool antiLag2Enabled,
    bool normalSpeed,
    melonDS::u64 targetFrameIntervalNs,
    melonDS::u64 logicalFrameId)
{
    if (!vulkan)
        return;

    // Kept for the panel's own diagnostics and so a presenter created later in
    // this session starts from the current setting rather than a stale one.
    vulkan->reflexMode.store(reflexMode, std::memory_order_relaxed);
    vulkan->antiLag2Enabled.store(antiLag2Enabled, std::memory_order_relaxed);

    if (!vulkan->presenter.IsInitialized())
        return;
    vulkan->presenter.BeginLowLatencyFrame(
        reflexMode,
        antiLag2Enabled,
        normalSpeed,
        targetFrameIntervalNs,
        logicalFrameId);
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


void ScreenPanelVulkan::invalidateScreenRetention()
{
    if (!vulkan)
        return;

    vulkan->retainedScreenLease.ReleaseNow();
    vulkan->retainedScreenKey = {};
    vulkan->retainedScreenLayerMask = 0;
    vulkan->presenter.InvalidateScreenLayerRetention();
}


void ScreenPanelVulkan::prepareForRendererTransition(bool detachRendererObserver)
{
    if (!vulkan)
        return;

#ifdef MELONPRIME_CUSTOM_HUD
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif

    // Drop the borrowed composed-frame pointers *before* the renderer that owns
    // them is destroyed, and take the observer back off it. After this the panel
    // has no native frame to show. What it presents next depends on which
    // renderer takes over: another native one publishes GPU frames again, while
    // the software renderer ("3D.ForceSoftwareOutsideMatch") publishes complete
    // CPU frames that drawScreenFrame() uploads through the presenter.
    if (detachRendererObserver && vulkan->hookedRenderer)
    {
        vulkan->hookedRenderer->SetVBlankObserver(nullptr, nullptr);
    }
    vulkan->hookedRenderer = nullptr;
    vulkan->rendererSnapshotRevision = ~0u;
    vulkan->cachedVulkanRenderer = nullptr;

    // Renderer transitions do not change the native presentation identity.
    // Quiesce GPU work and release renderer-owned output leases, but keep the
    // VkSurfaceKHR/VkSwapchainKHR presenter lifetime paired with the lifecycle
    // state until an actual native-surface transition retires it.
    vulkan->presenter.Quiesce();
    invalidateScreenRetention();
    vulkan->presenter.InvalidateDirectDescriptorCache();
    vulkan->nativeVisibility.Reset();
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


void ScreenPanelVulkan::clearPresentationStall()
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


void ScreenPanelVulkan::noteFramePresented(melonDS::u64 epoch, melonDS::u64 serial)
{
    if (!vulkan)
        return;

    clearPresentationStall();
    vulkan->nativeVisibility.Accept(epoch, serial);
}


void ScreenPanelVulkan::noteFramePresentedWithoutIdentity()
{
    if (!vulkan)
        return;

    clearPresentationStall();
    vulkan->nativeVisibility.AcceptWithoutIdentity();
}


void ScreenPanelVulkan::drawScreenFrame()
{
    if (!vulkan)
        return;

#if defined(__linux__)  // scatter-budget-exempt: service native-surface retire before frame admission
    // Lifecycle retirement must also be serviced while the emulator is
    // paused or has no active ROM; otherwise a GUI native-destruction event
    // would have to wait for a future presentation frame to become safe.
    serviceLinuxSurfaceRetire();
#endif

    auto* emuThread = emuInstance->getEmuThread();
    if (!emuThread)
    {
        noteFrameStalled("the emulation thread is gone");
        return;
    }

    if (!emuThread->emuIsActive())
    {
#if defined(__linux__)  // scatter-budget-exempt: CI-only no-ROM Vulkan WSI smoke path
        if (IsVulkanRuntimeSmokeEnabled())
        {
            // Normal no-ROM operation keeps the native child hidden so the
            // Qt splash remains visible. CI has no ROM to drive a real frame,
            // so this opt-in path exercises the same show -> snapshot lease ->
            // presenter bind sequence without submitting a fake frame.
            requestNativeSurfaceVisible(true);
            if (!beginLinuxPresentationFrame())
                return;

            const auto linuxFrameLease = MakeScopeExit(
                [this]() { finishLinuxPresentationFrame(); });
            const std::uint64_t currentGeneration =
                vulkan->linuxFrameSnapshot.Generation;
            if (vulkan->linuxSurfaceDirty.load(std::memory_order_acquire)
                || !vulkan->presenter.IsInitialized()
                || vulkan->presenterSurfaceIdentityGeneration != currentGeneration
                || vulkan->presenter.NeedsSurfaceRebind())
            {
                if (!prepareLinuxPresentationSurface())
                {
                    if (vulkan->presenter.HasFailed())
                        reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
                    return;
                }
            }
            return;
        }
#endif
        // No ROM: the splash screen is Qt-drawn (it composites a QPixmap, which
        // is GUI-thread-only), so the native surface steps aside and the
        // panel's own paintEvent takes over.
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        noteFrameIdle();
        return;
    }

#ifdef MELONPRIME_CUSTOM_HUD
    const bool hudLivePresentation =
        vulkan->hudEditLivePresentation.load(std::memory_order_acquire)
        || vulkan->hudLivePreviewPresentation.load(std::memory_order_acquire);
#else
    constexpr bool hudLivePresentation = false;
#endif

    if (vulkan->modalPauseActive.load(std::memory_order_acquire) && !hudLivePresentation)
    {
        noteFrameIdle();
        return;
    }
    if (!emuThread->emuIsRunning() && !hudLivePresentation)
    {
        noteFrameIdle();
        return;
    }

#if defined(__linux__)  // scatter-budget-exempt: Linux native-surface lifecycle boundary
    // Showing the child is a prerequisite for binding the presenter. It is
    // never posted after EndFrame(), which avoids the expose/show circular
    // dependency that leaves Wayland with no usable wl_surface at startup.
    requestNativeSurfaceVisible(true);
    if (!beginLinuxPresentationFrame())
    {
        serviceLinuxSurfaceRetire();
        if (vulkan->presenter.HasFailed())
            reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
        else
            noteFrameStalled(
                "the presentation surface is not ready for the current native generation");
        return;
    }
    const auto linuxFrameLease = MakeScopeExit(
        [this]() { finishLinuxPresentationFrame(); });
    const std::uint64_t currentGeneration = vulkan->linuxFrameSnapshot.Generation;
    if (vulkan->linuxSurfaceDirty.load(std::memory_order_acquire)
        || !vulkan->presenter.IsInitialized()
        || vulkan->presenterSurfaceIdentityGeneration != currentGeneration
        || vulkan->presenter.NeedsSurfaceRebind())
    {
        if (!prepareLinuxPresentationSurface())
        {
            if (vulkan->presenter.HasFailed())
                reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
            else
                noteFrameStalled(
                    "the presentation surface is not ready for the current native generation");
            return;
        }
    }
#else
    MelonPrime::NativeSurfaceSnapshot publishedSurface;
    if (!vulkan->surfaceSnapshot.Read(publishedSurface) || !publishedSurface.Valid)
    {
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        noteFrameStalled("the presentation surface snapshot is not valid");
        return;
    }

    const bool surfaceIdentityChanged =
        vulkan->presenter.IsInitialized()
        && (vulkan->presenterSurfaceIdentityGeneration != publishedSurface.IdentityGeneration
            || vulkan->presenterSurfaceHandle != publishedSurface.NativeHandle);
    if (surfaceIdentityChanged)
    {
        // A native HWND/NSView transition invalidates every retained direct
        // binding. Quiesce before releasing presenter leases, then destroy the
        // old WSI objects; the next Init(snapshot) creates a fresh surface and
        // swapchain for the published generation.
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanSurfaceRebindCount);
        vulkan->presenter.Quiesce();
        invalidateScreenRetention();
        vulkan->presenter.InvalidateDirectDescriptorCache();
        for (RendererOutputLease& lease : vulkan->frameLeases)
            lease.ReleaseNow();
        vulkan->presenter.Shutdown();
        vulkan->presenterSurfaceIdentityGeneration = 0;
        vulkan->presenterSurfaceGeometryRevision = 0;
        vulkan->presenterSurfaceHandle = 0;
        vulkan->nativeVisibility.Reset();
    }

    if (!vulkan->presenter.IsInitialized()
        && !initVulkanPresenter())
    {
        noteFrameStalled("the Vulkan presenter is not initialized");
        return;
    }

    if (vulkan->presenterSurfaceGeometryRevision != publishedSurface.GeometryRevision)
    {
        vulkan->presenter.NotifySurfaceChanged(publishedSurface.GeometryRevision);
        vulkan->presenterSurfaceGeometryRevision = publishedSurface.GeometryRevision;
    }
#endif

    auto* nds = emuInstance->getNDS();
    if (!nds)
    {
        noteFrameStalled("the emulated console is gone");
        return;
    }

    // Keeps the VBlank observer bound to the live renderer across every
    // renderer switch, including switches away from and back to Vulkan. The
    // renderer/config snapshot changes only at the cold transition boundary;
    // the dynamic_cast is therefore a transition cost, not a frame cost.
    const MelonPrime::PresentationConfigSnapshot presentation =
        emuInstance->getPresentationConfigSnapshot();
    if (presentation.revision != vulkan->rendererSnapshotRevision)
    {
        vulkan->rendererSnapshotRevision = presentation.revision;
        vulkan->cachedVulkanRenderer = nullptr;
        if (presentation.activeRenderer == renderer3D_Vulkan || presentation.revision == 0)
        {
            vulkan->cachedVulkanRenderer =
                dynamic_cast<melonDS::VulkanRenderer*>(&nds->GPU.GetRenderer());
        }
    }
    auto* vulkanRenderer = vulkan->cachedVulkanRenderer;
    installVulkanComposeHook(vulkanRenderer);

    const bool vsync = presentation.vsync;
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

    // Two different things publish CpuBgra here, and only the live renderer
    // tells them apart:
    //
    //  - No VulkanRenderer at all ("3D.ForceSoftwareOutsideMatch" swapped the
    //    renderer to Software while this panel keeps owning the swapchain).
    //    The software renderer flattens its own 3D layer, so this frame is a
    //    complete picture and belongs on screen through the CPU upload path.
    //  - A live VulkanRenderer whose pipeline is not ready yet. That output is
    //    a Software-2D plus placeholder-3D hybrid and must never be shown.
    const bool softwarePresentation = !vulkanRenderer
        && topPixels && bottomPixels && sourceWidth > 0 && sourceHeight > 0;
    if (!gpuFrame && !softwarePresentation)
    {
        // Vulkan/DX12 startup fallback is a hybrid frame: Software 2D is
        // paired with a 3D placeholder because the native pipeline is still
        // compiling. Keep the Qt black/splash surface until the first complete
        // native frame instead of making that hybrid visible.
        if (!vulkan->nativeVisibility.FirstCompleteFrameVisible)
        {
            requestNativeSurfaceVisible(false);
            QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
            noteFrameStalled("native startup frame is not ready");
        }
        else
        {
            noteFrameIdle();
        }
        return;
    }

#if defined(__linux__)  // scatter-budget-exempt: Linux physical extent selection, not input dispatch
    const int logicalWidth = static_cast<int>(
        vulkan->surfaceLogicalWidth.load(std::memory_order_acquire));
    const int logicalHeight = static_cast<int>(
        vulkan->surfaceLogicalHeight.load(std::memory_order_acquire));
    const u32 physicalWidth = vulkan->surfacePhysicalWidth.load(std::memory_order_acquire);
    const u32 physicalHeight = vulkan->surfacePhysicalHeight.load(std::memory_order_acquire);
#else
    const int logicalWidth = static_cast<int>(std::max(1u, publishedSurface.LogicalWidth));
    const int logicalHeight = static_cast<int>(std::max(1u, publishedSurface.LogicalHeight));
    const u32 physicalWidth = std::max(1u, publishedSurface.PhysicalWidth);
    const u32 physicalHeight = std::max(1u, publishedSurface.PhysicalHeight);
#endif

    const bool directSampledFrame = gpuFrame && gpuFrame->HasDirectSampledOutput();
    if (gpuFrame)
    {
        vulkan->presenter.SetPresentedFrameIdentity(gpuFrame->Serial, gpuFrame->Epoch);
        if (!vulkan->presenter.IsPresentedFrameIdentityMonotonic())
        {
            noteFrameIdle();
            return;
        }
    }
    else
    {
        // A software CPU frame carries no renderer frame identity. Serial 0 is
        // how the presenter is told so: it leaves the last accepted GPU
        // epoch/serial untouched, so the renderer that resumes at match start
        // is still compared against its own predecessor, not this interlude.
        vulkan->presenter.SetPresentedFrameIdentity(0, 0);
    }
    bool sameRendererFrame = false;
    if (gpuFrame)
    {
        const auto& retained = vulkan->retainedScreenKey;
        sameRendererFrame = retained.valid
            && retained.rendererIdentity == vulkanRenderer
            && retained.serial == gpuFrame->Serial
            && retained.resourceGeneration == gpuFrame->ResourceGeneration
            && retained.presentationSurfaceGeneration
                == vulkan->presenterSurfaceIdentityGeneration
            && retained.width == gpuFrame->Width
            && retained.height == gpuFrame->Height
            && retained.directSampled == directSampledFrame
            && (!directSampledFrame || vulkan->retainedScreenLease.Context != nullptr)
            && (directSampledFrame
                ? retained.directImageTop == gpuFrame->DirectImageTop
                    && retained.directImageViewTop == gpuFrame->DirectImageViewTop
                    && retained.directImageBottom == gpuFrame->DirectImageBottom
                    && retained.directImageViewBottom == gpuFrame->DirectImageViewBottom
                : retained.buffer == gpuFrame->Buffer);
    }

    const bool waitForPresentSlot =
        !vulkanRenderer || !vulkan->presenter.HasEffectiveLowLatencyAuthority();
    if (!vulkan->presenter.BeginFrame(
            physicalWidth, physicalHeight, waitForPresentSlot))
    {
        if (vulkan->presenter.LastBeginWasLatencySkip())
        {
            noteFrameIdle();
            return;
        }
        if (vulkan->presenter.NeedsSurfaceRebind())
        {
#if defined(__linux__)  // scatter-budget-exempt: Linux acquire surface-loss recovery, not input dispatch
            retireLinuxPresentationSurface("acquire returned VK_ERROR_SURFACE_LOST_KHR");
            vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
            vulkan->linuxSurfaceLifecycle->markSurfaceLost();
#endif
            noteFrameStalled("the Vulkan presentation surface was lost and is being rebound");
        }
        else if (vulkan->presenter.HasFailed())
            reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
        else
            noteFrameStalled("the presenter could not begin a frame (swapchain not ready)");
        return;
    }

    const u32 presenterFrameIndex = vulkan->presenter.GetFrameIndex();
    vulkan->frameLeases[presenterFrameIndex].ReleaseNow();

    // A new renderer frame, backend output, or CPU fallback invalidates the
    // retained screen source only after presenter admission succeeded. A
    // latency skip therefore leaves both the previous binding and its
    // dedicated direct-output lease untouched.
    if (!sameRendererFrame)
        invalidateScreenRetention();

    // The swapchain may be a different size than the widget for one frame
    // after a resize; every quad below is expressed in the swapchain's own
    // pixels so the frame is still correct rather than skipped.
    const float viewportWidth = static_cast<float>(vulkan->presenter.GetWidth());
    const float viewportHeight = static_cast<float>(vulkan->presenter.GetHeight());
    const float scaleX = viewportWidth / static_cast<float>(logicalWidth);
    const float scaleY = viewportHeight / static_cast<float>(logicalHeight);

    // --- uploads (must precede BeginComposition) ---------------------------

    const std::uint32_t layoutRevision =
        vulkan->layoutRevision.load(std::memory_order_acquire);
    if (layoutRevision != vulkan->cachedLayoutRevision)
    {
        QMutexLocker lock(&vulkan->layoutLock);
        vulkan->cachedNumScreens =
            std::min(vulkan->numScreens, kMaxScreenTransforms);
        for (int index = 0; index < vulkan->cachedNumScreens; ++index)
        {
            std::memcpy(
                vulkan->cachedScreenMatrix[index],
                vulkan->screenMatrix[index],
                sizeof(float) * 6);
            vulkan->cachedScreenKind[index] = vulkan->screenKind[index];
        }
        // Keep the revision observed before taking the lock. If the GUI
        // publishes another layout immediately after this copy, retaining the
        // older value forces one harmless refresh next frame instead of
        // labelling an older cache with the newer arrays' revision.
        vulkan->cachedLayoutRevision = layoutRevision;
    }
    const float (*matrices)[6] = vulkan->cachedScreenMatrix;
    const int* kinds = vulkan->cachedScreenKind;
    const int screens = vulkan->cachedNumScreens;

    bool screenUploaded[2] = {false, false};
    bool screenFrameReused = false;
    bool rendererLeaseAssigned = false;
    auto retainRendererOutputForFrame = [&]() {
        if (!rendererLeaseAssigned)
        {
            // Even if a later upload or EndFrame fails, this presenter slot
            // keeps the source alive until its fence retires or the transition
            // path explicitly quiesces it.
            vulkan->frameLeases[presenterFrameIndex] = std::move(rendererOutputLease);
            rendererLeaseAssigned = true;
        }
    };
    bool directDescriptorsPrepared = false;
    auto prepareDirectDescriptors = [&]() {
        if (directSampledFrame && !directDescriptorsPrepared)
        {
            // Descriptor preparation can invalidate/rebuild the direct-resource
            // cache and may wait for old renderer resources. It is reached only
            // after presenter admission, so a latency skip never touches it.
            vulkan->presenter.PrepareDirectOutputDescriptors(*gpuFrame);
            directDescriptorsPrepared = true;
        }
    };

    if (gpuFrame)
    {
        // First restore only layers whose source identity and presenter-owned
        // binding are both retained. Uploads are decided separately so a
        // layout that newly needs the other screen can still acquire a lease.
        for (int index = 0; index < screens; ++index)
        {
            const int kind = kinds[index] & 1;
            if (screenUploaded[kind])
                continue;
            const auto layer = kind == 0
                ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                : MelonPrime::VulkanPresenter::Layer::ScreenBottom;
            if (sameRendererFrame
                && (vulkan->retainedScreenLayerMask & (1u << kind)) != 0
                && vulkan->presenter.ReuseScreenLayerFromFrame(layer, *gpuFrame))
            {
                screenUploaded[kind] = true;
                screenFrameReused = true;
                if (directSampledFrame)
                {
                    // A retained direct image is sampled by this presentation
                    // too. Keep the renderer output slot leased by the current
                    // presenter frame until its fence retires; the dedicated
                    // retained lease may be released as soon as a new renderer
                    // frame is admitted without exposing an older submission
                    // to renderer-slot reuse.
                    retainRendererOutputForFrame();
                }
            }
        }

        for (int index = 0; index < screens; ++index)
        {
            const int kind = kinds[index] & 1;
            if (screenUploaded[kind])
                continue;
            const auto layer = kind == 0
                ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                : MelonPrime::VulkanPresenter::Layer::ScreenBottom;
            prepareDirectDescriptors();
            retainRendererOutputForFrame();
            screenUploaded[kind] = directSampledFrame
                ? vulkan->presenter.UploadLayerFromImage(layer, *gpuFrame)
                : vulkan->presenter.UploadLayerFromBuffer(
                    layer,
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
    if (screenFrameReused)
    {
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanScreenFrameReuseCount);
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
    const bool hudVisible = renderHudOverlay(
        emuThread,
        bottomPixels ? &bottomScreenImage : nullptr,
        logicalWidth,
        logicalHeight,
        hudRect);
    bool gpuRadarVisible = false;
    MelonPrime::VulkanPresenter::Quad gpuRadarQuad;
    u32 gpuRadarCenterY = 0;
    // The native colour-key pass is part of the Custom HUD, just like the CPU
    // overlay rendered above. Do not let a remembered radar preference draw
    // over the top screen while CustomHUD itself is disabled.
    if (gpuFrame && hudVisible && m_radarEnable)
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
                prepareDirectDescriptors();
                retainRendererOutputForFrame();
                screenUploaded[1] = gpuFrame->HasDirectSampledOutput()
                    ? vulkan->presenter.UploadLayerFromImage(
                        MelonPrime::VulkanPresenter::Layer::ScreenBottom, *gpuFrame)
                    : vulkan->presenter.UploadLayerFromBuffer(
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
            MelonPrimePerf::ScopedHudPhase gpuUploadTimer(
                MelonPrimePerf::HudPhase::GpuUpload);
            MelonPrimePerf::CountHudUploadCall();
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

    if (gpuFrame)
    {
        u8 retainedLayerMask = 0;
        for (const int kind : {0, 1})
        {
            if (!screenUploaded[kind])
                continue;
            const auto layer = kind == 0
                ? MelonPrime::VulkanPresenter::Layer::ScreenTop
                : MelonPrime::VulkanPresenter::Layer::ScreenBottom;
            const bool retainable = directSampledFrame
                ? vulkan->presenter.HasRetainedDirectLayer(layer)
                : vulkan->presenter.HasLayerContent(layer);
            if (retainable)
                retainedLayerMask |= static_cast<u8>(1u << kind);
        }

        if (directSampledFrame && retainedLayerMask != 0
            && vulkan->retainedScreenLease.Context == nullptr)
        {
            RendererOutputLease retainedLease = nds->GPU.AcquireRendererOutputLease();
            const RendererOutput& retainedOutput = retainedLease.Output;
            if (retainedOutput.Kind == RendererOutputKind::VulkanBuffer
                && retainedOutput.Top == gpuFrame
                && retainedOutput.FrameSerial == gpuFrame->Serial)
            {
                vulkan->retainedScreenLease = std::move(retainedLease);
            }
        }

        if (retainedLayerMask != 0
            && (!directSampledFrame || vulkan->retainedScreenLease.Context != nullptr))
        {
            auto& retained = vulkan->retainedScreenKey;
            retained.rendererIdentity = vulkanRenderer;
            retained.serial = gpuFrame->Serial;
            retained.resourceGeneration = gpuFrame->ResourceGeneration;
            retained.presentationSurfaceGeneration = vulkan->presenterSurfaceIdentityGeneration;
            retained.width = gpuFrame->Width;
            retained.height = gpuFrame->Height;
            retained.buffer = gpuFrame->Buffer;
            retained.directImageTop = gpuFrame->DirectImageTop;
            retained.directImageViewTop = gpuFrame->DirectImageViewTop;
            retained.directImageBottom = gpuFrame->DirectImageBottom;
            retained.directImageViewBottom = gpuFrame->DirectImageViewBottom;
            retained.directSampled = directSampledFrame;
            retained.valid = true;
            vulkan->retainedScreenLayerMask = retainedLayerMask;
        }
        else
        {
            vulkan->retainedScreenKey = {};
            vulkan->retainedScreenLayerMask = 0;
        }
    }

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

    MelonPrimePerf::ScopedHudPhase compositeTimer(
        MelonPrimePerf::HudPhase::Composite);
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
        if (vulkan->presenter.NeedsSurfaceRebind())
        {
#if defined(__linux__)  // scatter-budget-exempt: Linux present surface-loss recovery, not input dispatch
            retireLinuxPresentationSurface("present returned VK_ERROR_SURFACE_LOST_KHR");
            vulkan->linuxSurfaceDirty.store(true, std::memory_order_release);
            vulkan->linuxSurfaceLifecycle->markSurfaceLost();
#endif
            noteFrameStalled("the Vulkan presentation surface was lost and is being rebound");
        }
        else if (vulkan->presenter.HasFailed())
        {
            reportVulkanRuntimeFailure(vulkan->presenter.LastError().c_str());
        }
        else
        {
            noteFrameStalled("the presenter could not present the frame");
        }
        return;
    }

    if (gpuFrame)
        noteFramePresented(gpuFrame->Epoch, gpuFrame->Serial);
    else
        noteFramePresentedWithoutIdentity();

    // Linux has already requested visibility before presenter binding because
    // Wayland requires a mapped/exposed native surface. Other platforms keep
    // the native child hidden until the first successful present so an
    // uninitialized presentation surface never obscures the Qt fallback or
    // splash. requestNativeSurfaceVisible() coalesces duplicate state
    // requests, so this is a no-op on the steady-state Linux path.
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
bool ScreenPanelVulkan::renderHudOverlay(
    EmuThread* emuThread,
    QImage* bottomScreen,
    int logicalWidth,
    int logicalHeight,
    QRect& outDirty)
{
    outDirty = QRect();
    m_hudVisualFrameWasReused = false;

    auto* mp = emuThread ? emuThread->GetMelonPrimeCore() : nullptr;
    const bool editMode = mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
    if (!mp || !mp->IsRomDetected() || !(mp->IsInGame() || editMode))
    {
        m_hudVisualFrameValid = false;
        return false;
    }

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
        m_hudVisualFrameValid = false;
        MelonPrime::CustomHud_EnsurePatchRestored(
            mp->HudConfigState(), emuInstance, instcfg,
            mp->GetCurrentRom(), mp->GetPlayerPosition(), mp->IsInGame());
        return false;
    }

    // The overlay covers the whole widget in logical pixels so HUD elements
    // placed outside the DS rect (x < 0 or x > 256 when pillarboxed) stay
    // visible in the black bars.
    const int overlayWidth = std::max(1, logicalWidth);
    const int overlayHeight = std::max(1, logicalHeight);
    const HudVisualFrameIdentity visualIdentity =
        MelonPrimeHud_ProbeVisualFrameIdentity(emuInstance);
    MelonPrimePerf::CountHudVisualIdentityProbe();
    const bool sameGameFrame = m_hudVisualFrameValid
        && MelonPrimeHud_IsSameVisualGameFrame(
            visualIdentity, m_hudVisualFrameKey);
    HudVisualFrameKey visualKey{};
    if (sameGameFrame) {
        visualKey = MelonPrimeHud_MakeVisualFrameKey(
            visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
            overlayWidth, overlayHeight, m_topStretchX, m_hudScale,
            m_hudOriginX, m_hudOriginY,
            m_hudVisualRendererGeneration, m_hudEnabled, editMode);
        MelonPrimePerf::CountHudVisualStampCheck();
    }
    const bool reuseVisual = m_hudVisualFrameValid
        && sameGameFrame
        && visualKey == m_hudVisualFrameKey
        && vulkan->presenter.HasLayerContent(
            MelonPrime::VulkanPresenter::Layer::Hud);
    m_hudVisualFrameWasReused = reuseVisual;
    if (reuseVisual) {
        // The presenter retains the HUD layer between compositions. Reuse the
        // existing GPU image and report no dirty region; the caller still
        // draws the retained layer in the current composition.
        MelonPrimePerf::CountHudVisualReuse();
        return true;
    }
    MelonPrimePerf::CountHudVisualRender();
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
            m_hudEnabled,
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
    if (!sameGameFrame) {
        visualKey = MelonPrimeHud_MakeVisualFrameKey(
            visualIdentity, mp->HudConfigState(), m_hudCfgEpoch, m_hudFontEpoch,
            overlayWidth, overlayHeight, m_topStretchX, m_hudScale,
            m_hudOriginX, m_hudOriginY,
            m_hudVisualRendererGeneration, m_hudEnabled, editMode);
    }
    MelonPrimePerf::CountHudVisualStampCommit();
    m_hudVisualFrameKey = visualKey;
    m_hudVisualFrameValid = true;
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
