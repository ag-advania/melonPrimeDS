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

namespace MelonPrime
{

namespace
{

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
    std::shared_ptr<std::shared_mutex> lifecycleLock)
    : QWidget(parent), Callback(std::move(callback)), LifecycleLock(std::move(lifecycleLock))
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

    // The emulation thread holds a shared lock while it calls the WSI and
    // presents. Holding the exclusive side over Qt's native transition keeps
    // wl_surface/XID destruction from racing a Vulkan call that still carries
    // the old snapshot.
    //
    // QWidget::event(Show) may synchronously re-enter this handler with a
    // PlatformSurface or WinIdChange event. Do not acquire the same
    // non-recursive mutex twice; the outer handler already covers the whole
    // native transition.
    const bool reentrantLifecycleEvent = HandlingLifecycleEvent;
    std::unique_lock<std::shared_mutex> lifecycleGuard;
    if (!reentrantLifecycleEvent && LifecycleLock)
        lifecycleGuard = std::unique_lock<std::shared_mutex>(*LifecycleLock);

    const bool wasHandlingLifecycleEvent = HandlingLifecycleEvent;
    HandlingLifecycleEvent = true;
    const bool result = QWidget::event(event);

    LifecycleEvent lifecycleEvent = LifecycleEvent::Show;
    bool validAfterEvent = true;
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

    ++Generation;
    VulkanSurface::NativeWindowSnapshot snapshot;
    if (validAfterEvent)
        snapshot = captureSnapshot();
    snapshot.Generation = Generation;
    notifyLifecycle(lifecycleEvent, snapshot);
    HandlingLifecycleEvent = wasHandlingLifecycleEvent;
    return result;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__  // scatter-budget-exempt: closing guard of the Linux-only host
