/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Platform-independent half of the Vulkan WSI adapter: destruction.
//
// vkDestroySurfaceKHR belongs to VK_KHR_surface, not to any platform block, so
// it is the same call everywhere and does not need one copy per platform.
// Platform layers, where they exist, are owned by the native view that Create()
// attached them to and are released with it.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanSurface.h"

#include "Platform.h"

using namespace melonDS;

namespace MelonPrime::VulkanSurface
{

void Destroy(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    Surface& surface)
{
    if (surface.Handle == VK_NULL_HANDLE)
    {
        surface = Surface{};
        return;
    }

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr)
    {
        // Cannot happen through the presenter, which keeps its instance
        // reference for as long as it owns a surface. Leaking loudly beats
        // calling through a null function pointer.
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] surface destroyed without an instance; VkSurfaceKHR leaked\n");
        surface = Surface{};
        return;
    }

    const auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        getInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (destroy)
        destroy(instance, surface.Handle, nullptr);
    else
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] vkDestroySurfaceKHR is unavailable; VkSurfaceKHR leaked\n");

    surface = Surface{};
}


#if !defined(__APPLE__)  // scatter-budget-exempt: macOS supplies its own @autoreleasepool implementation of this function in the .mm adapter; not input dispatch
void RunFrameInPlatformScope(void (*body)(void* context), void* context)
{
    // No platform frame boundary is needed outside macOS: nothing in the
    // Win32/X11/Wayland present path allocates objects that a scope has to
    // drain.
    if (body)
        body(context);
}
#endif

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
