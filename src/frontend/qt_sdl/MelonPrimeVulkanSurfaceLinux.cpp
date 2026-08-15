/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Linux/BSD WSI: VK_KHR_xcb_surface, VK_KHR_xlib_surface or
// VK_KHR_wayland_surface, chosen from the *live* Qt platform plugin.
//
// The backend is decided by QGuiApplication::platformName() and the native
// handles Qt actually hands out, never by a compile-time guess: the same binary
// runs on X11, on XWayland and on native Wayland, and the WSI extension it must
// use differs in all three.

// Deliberately gated on __linux__ rather than "not Windows and not Apple":  // scatter-budget-exempt: prose describing this platform-owned WSI adapter
// qpa/qplatformnativeinterface.h is private Qt API, and the repository's
// standing position (see ScreenPanelGL::getWindowInfo) is that BSD builds must
// not require the private QPA headers. Those platforms get the stub adapter,
// which fails loudly instead of failing to compile.
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__)  // scatter-budget-exempt: platform-owned WSI adapter; the whole file is the Linux surface implementation, not input dispatch

#include "MelonPrimeVulkanSurface.h"

#include <QGuiApplication>
#include <QString>
#include <QWidget>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include "Platform.h"

using namespace melonDS;

namespace MelonPrime::VulkanSurface
{

namespace
{

// ---------------------------------------------------------------------------
// The three platform create-info structures, declared locally.
//
// The alternative -- #define VK_USE_PLATFORM_XCB_KHR and friends before
// including vulkan.h -- has two costs this file refuses to pay:
//
//   1. It drags <X11/Xlib.h>, <xcb/xcb.h> and <wayland-client.h> into the
//      build, so a Linux build would need all three sets of development
//      headers even on a machine that only ever runs one of them.
//   2. Vk::InstanceDispatch (VulkanLoader.h) gains members under those macros.
//      Defining one here and not in the other Vulkan translation units would
//      give the struct two different layouts in one binary, which is an ODR
//      violation with no diagnostic.
//
// The layouts below are fixed by the Vulkan specification and the sType values
// are already declared by vulkan_core.h, so nothing here can drift from the
// registry: `Display*`, `xcb_connection_t*`, `wl_display*` and `wl_surface*`
// are all opaque pointers, `Window` is an XID (unsigned long) and
// `xcb_window_t` is a uint32_t.
// ---------------------------------------------------------------------------

struct XlibSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* dpy;              // Display*
    unsigned long window;   // Window (XID)
};

struct XcbSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* connection;       // xcb_connection_t*
    std::uint32_t window;   // xcb_window_t
};

struct WaylandSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* display;          // struct wl_display*
    void* surface;          // struct wl_surface*
};

using PFN_CreateXlibSurface = VkResult(VKAPI_PTR*)(
    VkInstance, const XlibSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);
using PFN_CreateXcbSurface = VkResult(VKAPI_PTR*)(
    VkInstance, const XcbSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);
using PFN_CreateWaylandSurface = VkResult(VKAPI_PTR*)(
    VkInstance, const WaylandSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);


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


Surface CreateWayland(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWindow* window)
{
    Surface surface;
    surface.Backend = "VK_KHR_wayland_surface";

    const auto create = reinterpret_cast<PFN_CreateWaylandSurface>(
        getInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
    if (!create)
    {
        surface.Failure =
            "the Qt platform plugin is 'wayland' but the Vulkan runtime does not "
            "provide vkCreateWaylandSurfaceKHR (VK_KHR_wayland_surface is missing)";
        return surface;
    }

    void* display = NativeResourceForWindow("display", window);
    if (!display)
        display = NativeResourceForIntegration("wl_display");
    void* wlSurface = NativeResourceForWindow("surface", window);

    if (!display || !wlSurface)
    {
        surface.Failure =
            "the Wayland compositor did not hand out a wl_display/wl_surface pair "
            "for the presentation widget";
        return surface;
    }

    WaylandSurfaceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    info.display = display;
    info.surface = wlSurface;

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.Failure = "vkCreateWaylandSurfaceKHR failed: " + melonDS::Vk::FormatResult(result);
    }
    return surface;
}


Surface CreateX11(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWindow* window,
    unsigned long long windowId)
{
    // XCB first. It is the transport Qt's own xcb plugin speaks, so the
    // connection below is the one the window really lives on; VK_KHR_xcb_surface
    // is also the extension Mesa and the proprietary drivers implement natively,
    // with the Xlib entry point layered on top of it.
    if (const auto create = reinterpret_cast<PFN_CreateXcbSurface>(
            getInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR")))
    {
        if (void* connection = NativeResourceForIntegration("connection"))
        {
            Surface surface;
            surface.Backend = "VK_KHR_xcb_surface";

            XcbSurfaceCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            info.connection = connection;
            info.window = static_cast<std::uint32_t>(windowId);

            const VkResult result = create(instance, &info, nullptr, &surface.Handle);
            if (result == VK_SUCCESS)
                return surface;

            surface.Handle = VK_NULL_HANDLE;
            Platform::Log(
                Platform::LogLevel::Warn,
                "[Vulkan] vkCreateXcbSurfaceKHR failed: %s; trying VK_KHR_xlib_surface\n",
                melonDS::Vk::FormatResult(result).c_str());
        }
    }

    Surface surface;
    surface.Backend = "VK_KHR_xlib_surface";

    const auto create = reinterpret_cast<PFN_CreateXlibSurface>(
        getInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
    if (!create)
    {
        surface.Failure =
            "the Qt platform plugin is 'xcb' but the Vulkan runtime provides neither "
            "vkCreateXcbSurfaceKHR nor vkCreateXlibSurfaceKHR";
        return surface;
    }

    void* display = NativeResourceForWindow("display", window);
    if (!display)
        display = NativeResourceForIntegration("display");
    if (!display)
    {
        surface.Failure = "the X server connection (Display*) could not be obtained from Qt";
        return surface;
    }

    XlibSurfaceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.dpy = display;
    info.window = static_cast<unsigned long>(windowId);

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.Failure = "vkCreateXlibSurfaceKHR failed: " + melonDS::Vk::FormatResult(result);
    }
    return surface;
}

} // namespace


Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWidget* widget)
{
    Surface surface;

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr || !widget)
    {
        surface.Failure = "internal error: no Vulkan instance or no target widget";
        return surface;
    }

    // Forces the native window to exist before its handle is read.
    const unsigned long long windowId = static_cast<unsigned long long>(widget->winId());
    QWindow* window = widget->windowHandle();
    if (!window || windowId == 0)
    {
        surface.Failure = "the Vulkan surface widget has no native window handle";
        return surface;
    }

    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QStringLiteral("wayland")))
        surface = CreateWayland(instance, getInstanceProcAddr, window);
    else if (platform == QStringLiteral("xcb"))
        surface = CreateX11(instance, getInstanceProcAddr, window, windowId);
    else
    {
        surface.Failure =
            "the Qt platform plugin '" + platform.toStdString()
            + "' exposes no window-system handle this build can create a Vulkan surface from";
        return surface;
    }

    if (surface.IsValid())
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] presentation surface created backend=%s platform=%s window=%llu\n",
            surface.Backend.c_str(),
            platform.toStdString().c_str(),
            windowId);
    }
    return surface;
}


void UpdateGeometry(const Surface& surface, QWidget* widget)
{
    // X11 and Wayland both size the presentable surface from the native window,
    // which Qt resizes. The presenter still has to react to the resize -- it
    // recreates the swapchain -- but there is no platform layer to poke here.
    (void)surface;
    (void)widget;
}

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__  // scatter-budget-exempt: closing guard of the platform-owned WSI adapter above
