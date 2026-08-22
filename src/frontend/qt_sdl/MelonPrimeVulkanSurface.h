/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_SURFACE_H
#define MELONPRIME_VULKAN_SURFACE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdint>
#include <string>

#include "VulkanCommon.h"

class QWidget;

namespace MelonPrime::VulkanSurface
{

// Captured on the GUI thread after a Qt native-surface lifecycle event. The
// emulation thread consumes this immutable value and never calls QWidget/QPA
// accessors while creating or rebinding a VkSurfaceKHR.
struct NativeWindowSnapshot
{
    std::uint64_t Generation = 0;
    std::string Platform;
    unsigned long long WindowId = 0;
    void* XcbConnection = nullptr;
    void* XlibDisplay = nullptr;
    void* WaylandDisplay = nullptr;
    void* WaylandSurface = nullptr;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    bool Valid = false;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Valid && Generation != 0 && Width != 0 && Height != 0;
    }
};

// ---------------------------------------------------------------------------
// VkSurfaceKHR creation from a Qt native window.
//
// This header carries only core Vulkan types on purpose. Each platform's
// implementation lives in its own translation unit and is the ONLY place that
// defines VK_USE_PLATFORM_*; defining one of those macros in a unit that also
// includes VulkanLoader.h would change the layout of Vk::InstanceDispatch for
// that unit alone, which is an ODR violation. The platform units therefore
// resolve their vkCreate*SurfaceKHR entry point through the caller-supplied
// vkGetInstanceProcAddr instead of going through the shared dispatch table.
//
// VulkanContext already enables the matching instance extensions (see
// BuildInstanceExtensionList), so the lookup below succeeds exactly when the
// runtime really offers the platform's WSI extension.
// ---------------------------------------------------------------------------

struct Surface
{
    VkSurfaceKHR Handle = VK_NULL_HANDLE;

    // Platform-owned object that has to outlive the VkSurfaceKHR and be
    // released with it. On macOS this is the CAMetalLayer MoltenVK presents to;
    // every other platform leaves it null.
    void* PlatformLayer = nullptr;

    // Extension the surface was created with, for the startup log.
    std::string Backend;

    // Non-empty when Handle is VK_NULL_HANDLE. Always names the concrete
    // failure (missing entry point, unusable native handle, VkResult).
    std::string Failure;
    // The raw WSI result when creation reached vkCreate*SurfaceKHR. This lets
    // the presenter distinguish a recoverable surface loss from a permanent
    // unsupported-platform/device failure.
    VkResult FailureResult = VK_SUCCESS;

    [[nodiscard]] bool IsValid() const noexcept { return Handle != VK_NULL_HANDLE; }
};

// GUI thread only: `widget` must already be a realized native window
// (Qt::WA_NativeWindow), because every platform below reads its native handle.
Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWidget* widget);

// Presenter-thread entry point. The snapshot must have been captured by the
// GUI-thread native host after its latest lifecycle boundary. The same
// contract is used on Windows/macOS so a rebind never calls QWidget/QPA APIs
// from the emulation thread.
Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    const NativeWindowSnapshot& snapshot);

// Destroys the VkSurfaceKHR and any platform layer, and clears `surface`.
// Callers must have made sure no swapchain still references it.
void Destroy(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    Surface& surface);

// GUI thread only. Keeps a platform-owned layer's drawable size and scale in
// step with the widget after a resize or DPI change. No-op where the window
// system sizes the surface itself (Win32, X11, Wayland).
void UpdateGeometry(const Surface& surface, QWidget* widget);

// Runs one presentation frame inside a platform frame boundary.
//
// On macOS that boundary is an @autoreleasepool: MoltenVK's present path
// creates autoreleased CAMetalDrawable and Metal command-buffer temporaries on
// whatever thread calls it, and the emulation thread has no run loop to drain
// them, so without a pool per frame the process leaks one drawable per frame.
// Everywhere else this is a direct call.
void RunFrameInPlatformScope(void (*body)(void* context), void* context);

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_SURFACE_H
