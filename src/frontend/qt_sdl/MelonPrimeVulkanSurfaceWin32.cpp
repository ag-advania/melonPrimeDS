/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Windows WSI: VK_KHR_win32_surface over the panel's child HWND.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(_WIN32)

#include "MelonPrimeVulkanSurface.h"

#include <QWidget>

#include "Platform.h"

using namespace melonDS;

namespace MelonPrime::VulkanSurface
{

Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWidget* widget)
{
    Surface surface;
    surface.Backend = "VK_KHR_win32_surface";

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr || !widget)
    {
        surface.Failure = "internal error: no Vulkan instance or no target widget";
        return surface;
    }

    const auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        getInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!create)
    {
        surface.Failure =
            "the Vulkan runtime does not provide vkCreateWin32SurfaceKHR "
            "(VK_KHR_win32_surface is missing)";
        return surface;
    }

    // winId() realizes the window if it has not been already, which is why the
    // panel marks the surface host Qt::WA_NativeWindow before this runs: a
    // non-native QWidget would otherwise return its top-level parent's HWND and
    // the swapchain would cover the whole window including the menu bar.
    const HWND window = reinterpret_cast<HWND>(widget->winId());
    if (!window)
    {
        surface.Failure = "the Vulkan surface widget has no native window handle";
        return surface;
    }

    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
    info.hwnd = window;

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.Failure = "vkCreateWin32SurfaceKHR failed: " + melonDS::Vk::FormatResult(result);
        return surface;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] presentation surface created backend=%s hwnd=%p\n",
        surface.Backend.c_str(),
        static_cast<void*>(window));
    return surface;
}


Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    const NativeWindowSnapshot& snapshot)
{
    Surface surface;
    surface.Backend = "VK_KHR_win32_surface";

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr)
    {
        surface.Failure = "internal error: no Vulkan instance or instance procedure resolver";
        return surface;
    }
    if (!snapshot.IsValid() || snapshot.WindowId == 0)
    {
        surface.Failure = "the Vulkan native-surface snapshot has no valid HWND";
        return surface;
    }

    const auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        getInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!create)
    {
        surface.Failure =
            "the Vulkan runtime does not provide vkCreateWin32SurfaceKHR "
            "(VK_KHR_win32_surface is missing)";
        return surface;
    }

    const HWND window = reinterpret_cast<HWND>(
        static_cast<std::uintptr_t>(snapshot.WindowId));
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
    info.hwnd = window;

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.FailureResult = result;
        surface.Failure = "vkCreateWin32SurfaceKHR failed: " + melonDS::Vk::FormatResult(result);
        return surface;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] presentation surface created from snapshot backend=%s hwnd=%p generation=%llu\n",
        surface.Backend.c_str(),
        static_cast<void*>(window),
        static_cast<unsigned long long>(snapshot.Generation));
    return surface;
}


void UpdateGeometry(const Surface& surface, QWidget* widget)
{
    // The Win32 window system owns the surface extent: the swapchain follows
    // the HWND's client rect, which Qt already resizes for us. Nothing to do.
    (void)surface;
    (void)widget;
}

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && _WIN32
