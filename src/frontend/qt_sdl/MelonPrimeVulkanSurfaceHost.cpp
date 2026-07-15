#include "MelonPrimeVulkanSurfaceHost.h"

#if defined(__APPLE__)
#error "Apple builds must compile MelonPrimeVulkanSurfaceHost.mm"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

#include "Platform.h"
#include "VulkanContext.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QtGui/qguiapplication_platform.h>
using namespace QNativeInterface;
#else
#include <qpa/qplatformnativeinterface.h>
#endif
#if defined(WAYLAND_ENABLED) && defined(__linux__)
#include <qpa/qplatformnativeinterface.h>
#endif
#endif

namespace
{
std::atomic<melonDS::u64> gNextSurfaceGeneration{0};

struct NativeSurfaceIdentity
{
    VulkanWindowSystem system{VulkanWindowSystem::None};
    quintptr display{};
    quintptr window{};
};

melonDS::u64 NextSurfaceGeneration()
{
    melonDS::u64 generation = ++gNextSurfaceGeneration;
    if (generation == 0)
        generation = ++gNextSurfaceGeneration;
    return generation;
}

QSize WidgetPixelSize(const QWidget& widget)
{
    const qreal scale = std::max<qreal>(widget.devicePixelRatioF(), 1.0);
    return {
        std::max(1, static_cast<int>(std::ceil(widget.width() * scale))),
        std::max(1, static_cast<int>(std::ceil(widget.height() * scale))),
    };
}

bool HasEnabledInstanceExtension(const char* name)
{
    const auto& extensions =
        melonDS::VulkanContext::Get().GetPlatformRequirements().InstanceExtensions;
    return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}

NativeSurfaceIdentity QueryNativeIdentity(QWidget& widget)
{
    NativeSurfaceIdentity identity{};
    const WId nativeWindowId = widget.winId();
#if defined(_WIN32)
    identity.system = VulkanWindowSystem::Win32;
    identity.display = reinterpret_cast<quintptr>(GetModuleHandleW(nullptr));
    identity.window = static_cast<quintptr>(nativeWindowId);
#else
    const QString platformName = QGuiApplication::platformName();
    if (platformName == QStringLiteral("xcb"))
    {
        void* connection = nullptr;
        void* display = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (const QX11Application* x11 = qGuiApp->nativeInterface<QX11Application>())
        {
            connection = x11->connection();
            display = x11->display();
        }
#else
        if (QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface())
        {
            connection = native->nativeResourceForIntegration("connection");
            display = native->nativeResourceForIntegration("display");
            if (display == nullptr && widget.windowHandle() != nullptr)
                display = native->nativeResourceForWindow("display", widget.windowHandle());
        }
#endif
        if (connection != nullptr
            && HasEnabledInstanceExtension(VK_KHR_XCB_SURFACE_EXTENSION_NAME))
        {
            identity.system = VulkanWindowSystem::Xcb;
            identity.display = reinterpret_cast<quintptr>(connection);
        }
        else if (display != nullptr
            && HasEnabledInstanceExtension(VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
        {
            identity.system = VulkanWindowSystem::Xlib;
            identity.display = reinterpret_cast<quintptr>(display);
        }
        identity.window = static_cast<quintptr>(nativeWindowId);
    }
#if defined(WAYLAND_ENABLED)
    else if (platformName == QStringLiteral("wayland"))
    {
        void* display = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (const QWaylandApplication* wayland =
                qGuiApp->nativeInterface<QWaylandApplication>())
        {
            display = wayland->display();
        }
#else
        QWindow* nativeWindow = widget.windowHandle();
        if (QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface())
        {
            if (nativeWindow != nullptr)
                display = native->nativeResourceForWindow("display", nativeWindow);
        }
#endif
        void* surface = nullptr;
#if defined(__linux__)
        QWindow* windowHandle = widget.windowHandle();
        QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
        surface = native && windowHandle
            ? native->nativeResourceForWindow("surface", windowHandle)
            : nullptr;
#else
        surface = reinterpret_cast<void*>(static_cast<quintptr>(nativeWindowId));
#endif
        if (HasEnabledInstanceExtension(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
        {
            identity.system = VulkanWindowSystem::Wayland;
            identity.display = reinterpret_cast<quintptr>(display);
            identity.window = reinterpret_cast<quintptr>(surface);
        }
    }
#endif
#endif
    return identity;
}
} // namespace

bool MelonPrimeVulkanSurfaceHost::createForWidget(QWidget& newWidget, VkInstance instance)
{
    if (instance == VK_NULL_HANDLE || surfaceHandle != VK_NULL_HANDLE)
        return false;

    const NativeSurfaceIdentity identity = QueryNativeIdentity(newWidget);
    if (identity.system == VulkanWindowSystem::None
        || identity.display == 0
        || identity.window == 0)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan surface host: native window identity unavailable\n");
        return false;
    }

    VkSurfaceKHR newSurface = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    createInfo.hinstance = reinterpret_cast<HINSTANCE>(identity.display);
    createInfo.hwnd = reinterpret_cast<HWND>(identity.window);
    const auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (createSurface != nullptr)
        result = createSurface(instance, &createInfo, nullptr, &newSurface);
#else
    if (identity.system == VulkanWindowSystem::Xcb)
    {
        VkXcbSurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
        createInfo.connection = reinterpret_cast<xcb_connection_t*>(identity.display);
        createInfo.window = static_cast<xcb_window_t>(identity.window);
        const auto createSurface = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
            vkGetInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR"));
        if (createSurface != nullptr)
            result = createSurface(instance, &createInfo, nullptr, &newSurface);
    }
    else if (identity.system == VulkanWindowSystem::Xlib)
    {
        VkXlibSurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        createInfo.dpy = reinterpret_cast<Display*>(identity.display);
        createInfo.window = static_cast<Window>(identity.window);
        const auto createSurface = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
            vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
        if (createSurface != nullptr)
            result = createSurface(instance, &createInfo, nullptr, &newSurface);
    }
#if defined(WAYLAND_ENABLED)
    else if (identity.system == VulkanWindowSystem::Wayland)
    {
        VkWaylandSurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
        createInfo.display = reinterpret_cast<wl_display*>(identity.display);
        createInfo.surface = reinterpret_cast<wl_surface*>(identity.window);
        const auto createSurface = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
        if (createSurface != nullptr)
            result = createSurface(instance, &createInfo, nullptr, &newSurface);
    }
#endif
#endif
    if (result != VK_SUCCESS || newSurface == VK_NULL_HANDLE)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan surface host: surface creation failed (%d)\n",
            static_cast<int>(result));
        return false;
    }

    if (!melonDS::VulkanContext::Get().ResolvePresentQueue(newSurface))
    {
        vkDestroySurfaceKHR(instance, newSurface, nullptr);
        return false;
    }

    widget = &newWidget;
    surfaceHandle = newSurface;
    system = identity.system;
    nativeDisplayIdentity = identity.display;
    nativeWindowIdentity = identity.window;
    surfaceGeneration = NextSurfaceGeneration();
    return true;
}

void MelonPrimeVulkanSurfaceHost::destroy(VkInstance instance)
{
    if (surfaceHandle != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surfaceHandle, nullptr);
    surfaceHandle = VK_NULL_HANDLE;
    system = VulkanWindowSystem::None;
    nativeDisplayIdentity = 0;
    nativeWindowIdentity = 0;
    platformLayer = nullptr;
    widget.clear();
}

bool MelonPrimeVulkanSurfaceHost::matchesWidget(QWidget& candidate) const
{
    if (surfaceHandle == VK_NULL_HANDLE)
        return false;
    return matchesNativeIdentity(candidate) && widget == &candidate;
}

bool MelonPrimeVulkanSurfaceHost::matchesNativeIdentity(QWidget& candidate) const
{
    if (surfaceHandle == VK_NULL_HANDLE)
        return false;
    const NativeSurfaceIdentity identity = QueryNativeIdentity(candidate);
    return identity.system == system
        && identity.display == nativeDisplayIdentity
        && identity.window == nativeWindowIdentity;
}

void MelonPrimeVulkanSurfaceHost::rebindWidget(QWidget& candidate)
{
    if (matchesNativeIdentity(candidate))
        widget = &candidate;
}

QSize MelonPrimeVulkanSurfaceHost::pixelSize() const
{
    return widget ? WidgetPixelSize(*widget) : QSize{};
}
