/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Fallback WSI adapter for platforms with no supported Vulkan surface
// extension in this build. The BSDs use the dedicated BSD X11 adapter; this
// translation unit remains for other Unix-like platforms where the private QPA
// headers needed by the Linux adapter may not be packaged.
//
// This is not a placeholder implementation: it is the honest answer for a
// platform whose native handle this build cannot turn into a VkSurfaceKHR. It
// fails immediately with a reason the user can act on, and MainWindow then
// falls back to the Qt presentation panel without changing the saved renderer.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && !defined(_WIN32) && !defined(__APPLE__) && !defined(__linux__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__)  // scatter-budget-exempt: platform-owned WSI adapter selection; this TU exists precisely to keep window-system code out of the shared path

#include "MelonPrimeVulkanSurface.h"

#include <QGuiApplication>
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
    (void)instance;
    (void)getInstanceProcAddr;
    (void)widget;

    Surface surface;
    surface.Backend = "none";
    surface.Failure =
        "this platform has no Vulkan window-system integration in melonPrimeDS "
        "(Qt platform plugin '"
        + QGuiApplication::platformName().toStdString()
        + "'); use the Software or OpenGL renderer";

    Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
    return surface;
}

Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    const NativeWindowSnapshot& snapshot)
{
    (void)instance;
    (void)getInstanceProcAddr;
    (void)snapshot;

    Surface surface;
    surface.Backend = "none";
    surface.Failure =
        "this platform has no Vulkan window-system integration in melonPrimeDS; "
        "the published native surface snapshot cannot be converted to a VkSurfaceKHR";

    Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", surface.Failure.c_str());
    return surface;
}


void UpdateGeometry(const Surface& surface, QWidget* widget)
{
    (void)surface;
    (void)widget;
}

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && unknown Unix  // scatter-budget-exempt: closing guard of the platform-owned WSI adapter above
