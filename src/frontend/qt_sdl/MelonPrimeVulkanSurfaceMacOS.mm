/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// macOS WSI: VK_EXT_metal_surface over a CAMetalLayer.
//
// Vulkan on macOS is MoltenVK, and MoltenVK can only present to a CAMetalLayer
// -- there is no NSView path and no "native" macOS WSI extension. So this
// adapter has to create the layer itself, make the panel's NSView layer-backed
// by it, and hand the layer to vkCreateMetalSurfaceEXT.
//
// Compiled with ARC (see the APPLE block in CMakeLists.txt). Every call below
// touches AppKit/CoreAnimation and is therefore GUI-thread only, which is
// exactly the contract MelonPrime::VulkanSurface::Create/UpdateGeometry declare.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__APPLE__)

#include "MelonPrimeVulkanSurface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include <QWidget>
#include <QWindow>

#include "Platform.h"

using namespace melonDS;

namespace MelonPrime::VulkanSurface
{

namespace
{

// Declared locally rather than via VK_USE_PLATFORM_METAL_EXT, for the same
// reason as the Linux adapter: that macro adds a member to Vk::InstanceDispatch
// (VulkanLoader.h), and defining it in this translation unit alone would give
// the struct two layouts in one binary. The layout is fixed by the Vulkan
// specification and VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT is already
// declared by vulkan_core.h.
struct MetalSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    const void* pLayer;     // const CAMetalLayer*
};

using PFN_CreateMetalSurface = VkResult(VKAPI_PTR*)(
    VkInstance, const MetalSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);

CGFloat BackingScaleFor(NSView* view)
{
    NSWindow* window = view.window;
    if (window)
        return window.backingScaleFactor;
    NSScreen* screen = NSScreen.mainScreen;
    return screen ? screen.backingScaleFactor : 1.0;
}

void SyncLayerGeometry(CAMetalLayer* layer, NSView* view)
{
    if (!layer || !view)
        return;

    const CGFloat scale = BackingScaleFor(view);
    const CGSize bounds = view.bounds.size;

    layer.contentsScale = scale;
    // drawableSize is in physical pixels. Leaving it at the layer's default
    // would present at logical resolution on a Retina display, i.e. exactly the
    // "internal resolution silently downscaled at present" failure the whole
    // high-resolution path exists to avoid.
    layer.drawableSize = CGSizeMake(
        MAX(bounds.width * scale, 1.0),
        MAX(bounds.height * scale, 1.0));
}

} // namespace


Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    QWidget* widget)
{
    Surface surface;
    surface.Backend = "VK_EXT_metal_surface";

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr || !widget)
    {
        surface.Failure = "internal error: no Vulkan instance or no target widget";
        return surface;
    }

    const auto create = reinterpret_cast<PFN_CreateMetalSurface>(
        getInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!create)
    {
        surface.Failure =
            "the Vulkan runtime does not provide vkCreateMetalSurfaceEXT "
            "(VK_EXT_metal_surface is missing; MoltenVK is required on macOS)";
        return surface;
    }

    // On the cocoa platform plugin winId() is the NSView backing the widget.
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    if (!view)
    {
        surface.Failure = "the Vulkan surface widget has no NSView";
        return surface;
    }

    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
    if (!metalDevice)
    {
        surface.Failure = "no Metal device is available; MoltenVK cannot present";
        return surface;
    }

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = metalDevice;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    // The panel repositions and resizes this view constantly (screen layout
    // changes, window resizes). Implicit CoreAnimation animations on those
    // property changes would make the presented image lag the widget by a
    // fraction of a second, so they are switched off rather than fought.
    layer.needsDisplayOnBoundsChange = YES;
    layer.presentsWithTransaction = NO;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;

    view.wantsLayer = YES;
    view.layer = layer;
    SyncLayerGeometry(layer, view);

    MetalSurfaceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = (__bridge const void*)layer;

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.Failure = "vkCreateMetalSurfaceEXT failed: " + melonDS::Vk::FormatResult(result);
        return surface;
    }

    // Unretained on purpose: the NSView owns the layer from the assignment
    // above and releases it with itself, so Destroy() has nothing to free. This
    // pointer exists only so UpdateGeometry() can reach the layer again.
    surface.PlatformLayer = (__bridge void*)layer;

    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] presentation surface created backend=%s metalDevice=%s\n",
        surface.Backend.c_str(),
        metalDevice.name.UTF8String);
    return surface;
}


Surface Create(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr getInstanceProcAddr,
    const NativeWindowSnapshot& snapshot)
{
    Surface surface;
    surface.Backend = "VK_EXT_metal_surface";

    if (instance == VK_NULL_HANDLE || !getInstanceProcAddr)
    {
        surface.Failure = "internal error: no Vulkan instance or instance procedure resolver";
        return surface;
    }
    if (!snapshot.IsValid() || snapshot.WindowId == 0)
    {
        surface.Failure = "the Vulkan native-surface snapshot has no NSView";
        return surface;
    }

    const auto create = reinterpret_cast<PFN_CreateMetalSurface>(
        getInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!create)
    {
        surface.Failure =
            "the Vulkan runtime does not provide vkCreateMetalSurfaceEXT "
            "(VK_EXT_metal_surface is missing; MoltenVK is required on macOS)";
        return surface;
    }

    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(snapshot.WindowId));
    if (!view)
    {
        surface.Failure = "the Vulkan native-surface snapshot has no NSView";
        return surface;
    }

    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
    if (!metalDevice)
    {
        surface.Failure = "no Metal device is available; MoltenVK cannot present";
        return surface;
    }

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = metalDevice;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.needsDisplayOnBoundsChange = YES;
    layer.presentsWithTransaction = NO;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    view.wantsLayer = YES;
    view.layer = layer;
    SyncLayerGeometry(layer, view);

    MetalSurfaceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = (__bridge const void*)layer;

    const VkResult result = create(instance, &info, nullptr, &surface.Handle);
    if (result != VK_SUCCESS)
    {
        surface.Handle = VK_NULL_HANDLE;
        surface.FailureResult = result;
        surface.Failure = "vkCreateMetalSurfaceEXT failed: " + melonDS::Vk::FormatResult(result);
        return surface;
    }

    surface.PlatformLayer = (__bridge void*)layer;
    Platform::Log(
        Platform::LogLevel::Info,
        "[Vulkan] presentation surface created from snapshot backend=%s generation=%llu\n",
        surface.Backend.c_str(),
        static_cast<unsigned long long>(snapshot.Generation));
    return surface;
}


void UpdateGeometry(const Surface& surface, QWidget* widget)
{
    if (!surface.PlatformLayer || !widget)
        return;

    CAMetalLayer* layer = (__bridge CAMetalLayer*)surface.PlatformLayer;
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
    SyncLayerGeometry(layer, view);
}


void RunFrameInPlatformScope(void (*body)(void* context), void* context)
{
    if (!body)
        return;

    // MoltenVK's acquire/present path autoreleases a CAMetalDrawable and the
    // Metal command buffers behind it. The emulation thread that drives this
    // has no run loop, so nothing would ever drain the thread's pool and the
    // process would leak one drawable per presented frame.
    @autoreleasepool
    {
        body(context);
    }
}

} // namespace MelonPrime::VulkanSurface

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __APPLE__
