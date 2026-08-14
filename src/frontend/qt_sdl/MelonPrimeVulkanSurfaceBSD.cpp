/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// BSD X11 WSI: public Qt 6.5+ QX11Application handles with an XCB-first,
// Xlib-fallback Vulkan surface path. This translation unit deliberately does
// not include Qt's private QPA headers or define VK_USE_PLATFORM_* macros:
// VulkanLoader's dispatch layout must remain identical in every translation
// unit, and BSD packages do not consistently ship private Qt headers.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) \
    && (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__))  // scatter-budget-exempt: platform-owned BSD WSI adapter

#include "MelonPrimeVulkanSurface.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <QGuiApplication>
#include <QString>
#include <QWidget>

#include "Platform.h"

using namespace melonDS;

namespace MelonPrime::VulkanSurface
{

namespace
{

// These are the Vulkan registry layouts without enabling VK_USE_PLATFORM_XCB_KHR
// or VK_USE_PLATFORM_XLIB_KHR in a shared header. The platform handles are
// opaque here, so the adapter does not impose X11/XCB headers on the target.
struct XcbSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* connection;       // xcb_connection_t*
    std::uint32_t window;   // xcb_window_t
};

struct XlibSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* display;          // Display*
    unsigned long window;   // Window (XID)
};

using PFN_CreateXcbSurface = VkResult(VKAPI_PTR*) (
    VkInstance,
    const XcbSurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);
using PFN_CreateXlibSurface = VkResult(VKAPI_PTR*) (
    VkInstance,
    const XlibSurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);

// Compile-time ABI guard: both local layouts must retain the Vulkan registry
// field order and the same alignment as the corresponding 64-bit X11 structs.
static_assert(std::is_standard_layout_v<XcbSurfaceCreateInfo>);
static_assert(std::is_standard_layout_v<XlibSurfaceCreateInfo>);
static_assert(sizeof(VkStructureType) == sizeof(std::uint32_t));
static_assert(offsetof(XcbSurfaceCreateInfo, sType) == 0);
static_assert(offsetof(XcbSurfaceCreateInfo, sType) <
              offsetof(XcbSurfaceCreateInfo, pNext));
static_assert(offsetof(XcbSurfaceCreateInfo, pNext) <
              offsetof(XcbSurfaceCreateInfo, flags));
static_assert(offsetof(XcbSurfaceCreateInfo, flags) <
              offsetof(XcbSurfaceCreateInfo, connection));
static_assert(offsetof(XcbSurfaceCreateInfo, connection) <
              offsetof(XcbSurfaceCreateInfo, window));
static_assert(offsetof(XlibSurfaceCreateInfo, sType) == 0);
static_assert(offsetof(XlibSurfaceCreateInfo, sType) <
              offsetof(XlibSurfaceCreateInfo, pNext));
static_assert(offsetof(XlibSurfaceCreateInfo, pNext) <
              offsetof(XlibSurfaceCreateInfo, flags));
static_assert(offsetof(XlibSurfaceCreateInfo, flags) <
              offsetof(XlibSurfaceCreateInfo, display));
static_assert(offsetof(XlibSurfaceCreateInfo, display) <
              offsetof(XlibSurfaceCreateInfo, window));
static_assert(sizeof(XcbSurfaceCreateInfo) == sizeof(XlibSurfaceCreateInfo));

Surface UnsupportedQtVersion()
{
    Surface surface;
    surface.Backend = "unsupported";
    surface.Failure =
        "BSD Vulkan X11 WSI requires Qt 6.5+ public QNativeInterface::QX11Application; "
        "private Qt QPA is not used";
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
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

    const unsigned long long windowId =
        static_cast<unsigned long long>(widget->winId());
    const QString platform = QGuiApplication::platformName();
    if (platform != QStringLiteral("xcb"))
    {
        surface.Backend = "unsupported";
        surface.Failure =
            "BSD Vulkan WSI: Qt platform plugin '" + platform.toStdString()
            + "' is not supported by the BSD X11 adapter";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    surface = UnsupportedQtVersion();
    Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
    return surface;
#else
    if (windowId == 0)
    {
        surface.Failure = "the Vulkan surface widget has no native X11 window handle";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

    const QNativeInterface::QX11Application* x11 =
        qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (!x11)
    {
        surface.Failure =
            "Qt platform plugin 'xcb' did not expose QNativeInterface::QX11Application";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

    const auto createXcb = reinterpret_cast<PFN_CreateXcbSurface>(
        getInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR"));
    const void* connection = x11->connection();
    std::string xcbFailure;

    if (createXcb && connection)
    {
        surface.Backend = "VK_KHR_xcb_surface";
        XcbSurfaceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        info.connection = const_cast<void*>(connection);
        info.window = static_cast<std::uint32_t>(windowId);

        const VkResult result = createXcb(instance, &info, nullptr, &surface.Handle);
        if (result == VK_SUCCESS)
        {
            Platform::Log(
                Platform::LogLevel::Info,
                "[Vulkan] BSD presentation surface created backend=%s platform=xcb window=%llu\n",
                surface.Backend.c_str(),
                windowId);
            return surface;
        }

        surface.Handle = VK_NULL_HANDLE;
        xcbFailure = "vkCreateXcbSurfaceKHR failed: " + melonDS::Vk::FormatResult(result);
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] BSD %s; trying VK_KHR_xlib_surface\n",
            xcbFailure.c_str());
    }
    else if (!createXcb)
    {
        xcbFailure = "vkCreateXcbSurfaceKHR is unavailable";
    }
    else
    {
        xcbFailure = "Qt did not expose an XCB connection";
    }

    const auto createXlib = reinterpret_cast<PFN_CreateXlibSurface>(
        getInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
    const void* display = x11->display();
    if (!createXlib)
    {
        surface.Backend = "VK_KHR_xlib_surface";
        surface.Failure =
            xcbFailure + "; vkCreateXlibSurfaceKHR is also unavailable";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }
    if (!display)
    {
        surface.Backend = "VK_KHR_xlib_surface";
        surface.Failure = xcbFailure + "; Qt did not expose an Xlib Display*";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

    surface.Backend = "VK_KHR_xlib_surface";
    XlibSurfaceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.display = const_cast<void*>(display);
    info.window = static_cast<unsigned long>(windowId);

    const VkResult result = createXlib(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.Failure = xcbFailure + "; vkCreateXlibSurfaceKHR failed: "
            + melonDS::Vk::FormatResult(result);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
        return surface;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] BSD presentation surface created backend=%s platform=xcb window=%llu\n",
        surface.Backend.c_str(),
        windowId);
    return surface;
#endif
}


void UpdateGeometry(const Surface& surface, QWidget* widget)
{
    // X11 owns the surface extent and the presenter recreates the swapchain
    // after Qt reports a resize; there is no platform layer to update here.
    (void)surface;
    (void)widget;
}

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && BSD  // scatter-budget-exempt: closing guard of the platform-owned WSI adapter above
