/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__)  // scatter-budget-exempt: Linux-only native Vulkan surface host

#include "MelonPrimeVulkanSurfaceHostLinux.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include <QByteArray>
#include <QGuiApplication>
#include <QPaintEvent>
#include <QPlatformSurfaceEvent>
#include <QtGlobal>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include "Platform.h"

namespace MelonPrime
{

void VulkanSurfaceLifecycle::publishSnapshot(
    const VulkanSurface::NativeWindowSnapshot& snapshot,
    bool valid)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Snapshot = snapshot;
    Snapshot.Valid = valid && snapshot.IsValid();

    // A valid native handle is not presentation-ready until the child has
    // completed its Show lifecycle. XCB can deliver WinIdChange and
    // SurfaceCreated before Show, so keep that snapshot pending instead of
    // admitting a frame against the pre-Show generation.
    if (Snapshot.Valid)
    {
        if (StateValue != State::RetireRequested && StateValue != State::Retiring)
            StateValue = HostShown ? State::Ready : State::WaitingForShow;
    }
    else if (StateValue != State::RetireRequested
        && StateValue != State::Retiring
        && StateValue != State::DestroySafe)
    {
        StateValue = HostShown ? State::WaitingForNativeSurface : State::Hidden;
    }
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::requestRetire()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
        return;

    Snapshot.Valid = false;
    if (!PresenterActive && ActiveFrames == 0)
        StateValue = State::DestroySafe;
    else
        StateValue = State::RetireRequested;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::requestNativeTransitionRetire()
{
    std::lock_guard<std::mutex> lock(Mutex);
    HostShown = false;
    Snapshot.Valid = false;
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
    {
        RetireCondition.notify_all();
        return;
    }

    if (!PresenterActive && ActiveFrames == 0)
        StateValue = State::DestroySafe;
    else
        StateValue = State::RetireRequested;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markHostShown(bool shown)
{
    std::lock_guard<std::mutex> lock(Mutex);
    HostShown = shown;
    if (StateValue != State::RetireRequested && StateValue != State::Retiring)
    {
        if (Snapshot.IsValid())
            StateValue = HostShown ? State::Ready : State::WaitingForShow;
        else if (StateValue != State::DestroySafe)
            StateValue = HostShown ? State::WaitingForNativeSurface : State::Hidden;
    }
    RetireCondition.notify_all();
}


bool VulkanSurfaceLifecycle::waitForDestroySafe(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(Mutex);
    return RetireCondition.wait_for(lock, timeout, [this]() {
        return StateValue == State::DestroySafe && ActiveFrames == 0;
    });
}


bool VulkanSurfaceLifecycle::beginFrame(VulkanSurface::NativeWindowSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if ((StateValue != State::Ready && StateValue != State::Bound)
        || !HostShown || !Snapshot.IsValid())
    {
        return false;
    }

    snapshot = Snapshot;
    ++ActiveFrames;
    return true;
}


void VulkanSurfaceLifecycle::endFrame()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (ActiveFrames > 0)
        --ActiveFrames;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markBound(std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(Mutex);
    BoundGeneration = generation;
    PresenterActive = true;
    if (StateValue == State::Ready)
        StateValue = State::Bound;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markSurfaceLost()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Snapshot.Valid = false;
    if (StateValue != State::RetireRequested && StateValue != State::Retiring)
        StateValue = State::WaitingForNativeSurface;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::beginRetiring()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (StateValue == State::RetireRequested)
        StateValue = State::Retiring;
    RetireCondition.notify_all();
}


void VulkanSurfaceLifecycle::markPresenterRetired()
{
    std::lock_guard<std::mutex> lock(Mutex);
    BoundGeneration = 0;
    PresenterActive = false;
    if (StateValue == State::RetireRequested || StateValue == State::Retiring)
        StateValue = Snapshot.IsValid() && HostShown ? State::Ready : State::DestroySafe;
    RetireCondition.notify_all();
}


bool VulkanSurfaceLifecycle::retireRequested() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return StateValue == State::RetireRequested || StateValue == State::Retiring;
}


bool VulkanSurfaceLifecycle::presentationReady() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return HostShown && Snapshot.IsValid()
        && (StateValue == State::Ready || StateValue == State::Bound);
}


VulkanSurfaceLifecycle::State VulkanSurfaceLifecycle::state() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return StateValue;
}

namespace
{

constexpr auto kNativeSurfaceRetireTimeout = std::chrono::milliseconds(250);

void* NativeResourceForWindow(const char* name, QWindow* window)
{
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (!pni || !window)
        return nullptr;
    return pni->nativeResourceForWindow(QByteArray(name), window);
}

void* NativeResourceForIntegration(const char* name)
{
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (!pni)
        return nullptr;
    return pni->nativeResourceForIntegration(QByteArray(name));
}

bool IsLifecycleEvent(QEvent* event)
{
    if (!event)
        return false;

    switch (event->type())
    {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WinIdChange:
        return true;
    case QEvent::PlatformSurface:
    {
        const auto* surfaceEvent = static_cast<const QPlatformSurfaceEvent*>(event);
        return surfaceEvent->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceCreated
            || surfaceEvent->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed;
    }
    default:
        return false;
    }
}

} // namespace


VulkanSurfaceHostLinux::VulkanSurfaceHostLinux(
    QWidget* parent,
    LifecycleCallback callback,
    std::shared_ptr<VulkanSurfaceLifecycle> lifecycle)
    : QWidget(parent), Callback(std::move(callback)), Lifecycle(std::move(lifecycle))
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Qt's paint-on-screen path is only implemented by the XCB plugin. Setting
    // it on Wayland makes the child participate in the wrong paint contract and
    // can keep the compositor from exposing the wl_surface.
    if (QGuiApplication::platformName() == QStringLiteral("xcb"))
        setAttribute(Qt::WA_PaintOnScreen, true);
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(false);
}


VulkanSurface::NativeWindowSnapshot VulkanSurfaceHostLinux::captureSnapshot() const
{
    VulkanSurface::NativeWindowSnapshot snapshot;
    snapshot.Generation = Generation;
    snapshot.Platform = QGuiApplication::platformName().toStdString();

    const QWindow* const window = windowHandle();
    if (!window)
        return snapshot;

    const QSize size = this->size();
    const qreal dpr = devicePixelRatioF();
    snapshot.Width = static_cast<std::uint32_t>(
        std::max(1, qRound(size.width() * dpr)));
    snapshot.Height = static_cast<std::uint32_t>(
        std::max(1, qRound(size.height() * dpr)));

    // WId is required by XCB/Xlib, but is deliberately not used as the
    // identity on Wayland. A compositor may reuse it while replacing the
    // underlying wl_surface.
    snapshot.WindowId = static_cast<unsigned long long>(winId());

    if (snapshot.Platform.rfind("wayland", 0) == 0)
    {
        snapshot.WaylandDisplay = NativeResourceForWindow("display", const_cast<QWindow*>(window));
        if (!snapshot.WaylandDisplay)
            snapshot.WaylandDisplay = NativeResourceForIntegration("wl_display");
        snapshot.WaylandSurface = NativeResourceForWindow("surface", const_cast<QWindow*>(window));
        snapshot.Valid = snapshot.WaylandDisplay != nullptr
            && snapshot.WaylandSurface != nullptr;
    }
    else if (snapshot.Platform == "xcb")
    {
        snapshot.XcbConnection = NativeResourceForIntegration("connection");
        snapshot.XlibDisplay = NativeResourceForWindow(
            "display", const_cast<QWindow*>(window));
        if (!snapshot.XlibDisplay)
            snapshot.XlibDisplay = NativeResourceForIntegration("display");
        snapshot.Valid = snapshot.WindowId != 0
            && (snapshot.XcbConnection != nullptr || snapshot.XlibDisplay != nullptr);
    }

    return snapshot;
}


void VulkanSurfaceHostLinux::notifyLifecycle(
    LifecycleEvent event,
    const VulkanSurface::NativeWindowSnapshot& snapshot)
{
    if (Callback)
        Callback(event, snapshot);
}


bool VulkanSurfaceHostLinux::event(QEvent* event)
{
    if (!IsLifecycleEvent(event))
        return QWidget::event(event);

    const bool reentrantLifecycleEvent = HandlingLifecycleEvent;
    const bool wasHandlingLifecycleEvent = HandlingLifecycleEvent;
    HandlingLifecycleEvent = true;

    LifecycleEvent lifecycleEvent = LifecycleEvent::Show;
    bool validAfterEvent = true;
    bool surfaceAboutToBeDestroyed = false;
    switch (event->type())
    {
    case QEvent::Show:
        lifecycleEvent = LifecycleEvent::Show;
        break;
    case QEvent::Hide:
        lifecycleEvent = LifecycleEvent::Hide;
        validAfterEvent = false;
        break;
    case QEvent::WinIdChange:
        lifecycleEvent = LifecycleEvent::WinIdChange;
        break;
    case QEvent::PlatformSurface:
    {
        const auto* surfaceEvent = static_cast<const QPlatformSurfaceEvent*>(event);
        if (surfaceEvent->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        {
            lifecycleEvent = LifecycleEvent::SurfaceAboutToBeDestroyed;
            validAfterEvent = false;
            surfaceAboutToBeDestroyed = true;
        }
        else
        {
            lifecycleEvent = LifecycleEvent::SurfaceCreated;
        }
        break;
    }
    default:
        break;
    }

    // Hide/Show/WinIdChange can replace a native object as well as the
    // explicit PlatformSurface notification. Request retirement before Qt is
    // allowed to perform that transition. The request is only a short state
    // publication; the Vulkan work is drained by the emulation-thread frame
    // lease, and no lifecycle mutex is held across it.
    const bool nativeTransition = lifecycleEvent == LifecycleEvent::Show
        || lifecycleEvent == LifecycleEvent::Hide
        || lifecycleEvent == LifecycleEvent::WinIdChange
        || surfaceAboutToBeDestroyed;
    if (nativeTransition && Lifecycle && !reentrantLifecycleEvent)
    {
        Lifecycle->requestNativeTransitionRetire();
        if (!surfaceAboutToBeDestroyed && !Lifecycle->waitForDestroySafe(
                kNativeSurfaceRetireTimeout))
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[Vulkan][LinuxWSI] native transition retire timed out generation=%llu event=%d\n",
                static_cast<unsigned long long>(Generation),
                static_cast<int>(lifecycleEvent));
        }
    }

    // SurfaceAboutToBeDestroyed is the one event whose callback must precede
    // QWidget::event(): Qt is about to release the wl_surface/X11 native
    // object. The callback publishes the invalid generation, then the bounded
    // handshake waits for the emulation thread to stop using the old
    // VkSurfaceKHR before native destruction is permitted.
    if (surfaceAboutToBeDestroyed)
    {
        ++Generation;
        VulkanSurface::NativeWindowSnapshot snapshot;
        snapshot.Generation = Generation;
        snapshot.Platform = QGuiApplication::platformName().toStdString();
        const QSize size = this->size();
        const qreal dpr = devicePixelRatioF();
        snapshot.Width = static_cast<std::uint32_t>(
            std::max(1, qRound(size.width() * dpr)));
        snapshot.Height = static_cast<std::uint32_t>(
            std::max(1, qRound(size.height() * dpr)));
        notifyLifecycle(lifecycleEvent, snapshot);

        if (Lifecycle && !Lifecycle->waitForDestroySafe(kNativeSurfaceRetireTimeout))
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[Vulkan][LinuxWSI] native surface destruction retire timed out generation=%llu\n",
                static_cast<unsigned long long>(Generation));
        }
    }

    const bool result = QWidget::event(event);

    if (surfaceAboutToBeDestroyed)
    {
        HandlingLifecycleEvent = wasHandlingLifecycleEvent;
        return result;
    }

    ++Generation;
    VulkanSurface::NativeWindowSnapshot snapshot;
    // The host is presentation-eligible only after QWidget::event() returns.
    // WinIdChange/SurfaceCreated can occur while the child is still hidden or
    // re-entrantly inside QWidget::event(Show). Do not let a nested event
    // promote a pre-Show snapshot; only the completed outer Show (or a later
    // non-reentrant event on an already-visible child) may set HostShown.
    if (Lifecycle)
    {
        const bool hostShown = validAfterEvent
            && !reentrantLifecycleEvent
            && (lifecycleEvent == LifecycleEvent::Show || isVisible());
        Lifecycle->markHostShown(hostShown);
    }
    if (validAfterEvent)
        snapshot = captureSnapshot();
    snapshot.Generation = Generation;
    notifyLifecycle(lifecycleEvent, snapshot);
    HandlingLifecycleEvent = wasHandlingLifecycleEvent;
    return result;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__  // scatter-budget-exempt: closing guard of the Linux-only host
