#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__APPLE__) // scatter-budget-exempt: native Vulkan surface adapter, not input dispatch

#include "MelonPrimeVulkanSurface.h"
#include "MelonPrimeVulkanSurfaceMacOS.h"

#import <AppKit/NSView.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include <algorithm>
#include <cmath>
#include <functional>

#include "Platform.h"
#include "VulkanDispatch.h"

namespace MelonPrime
{
namespace
{
// VK_EXT_metal_surface, declared locally so VK_USE_PLATFORM_METAL_EXT never
// leaks into the shared core compilation units (same policy as the Win32 and
// Linux adapters).
constexpr VkStructureType kMetalSurfaceCreateInfoType =
    static_cast<VkStructureType>(1000217000); // VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT

struct MetalSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    const void* pLayer; // CAMetalLayer*
};

using CreateMetalSurfaceFn = VkResult (VKAPI_PTR *)(
    VkInstance,
    const MetalSurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);
} // namespace

VkSurfaceKHR CreateVulkanSurface(
    VkInstance instance,
    const VulkanNativeWindowInfo& nativeWindow,
    std::string& reason)
{
    if (instance == VK_NULL_HANDLE
        || nativeWindow.type != VulkanNativeWindowType::Metal
        || nativeWindow.window == nullptr)
    {
        reason = "macOS Vulkan surface requires a valid instance and CAMetalLayer";
        return VK_NULL_HANDLE;
    }

    auto createSurface = reinterpret_cast<CreateMetalSurfaceFn>(
        vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (createSurface == nullptr)
    {
        reason = "vkCreateMetalSurfaceEXT is unavailable; MoltenVK 1.1 or newer is required";
        return VK_NULL_HANDLE;
    }

    MetalSurfaceCreateInfo createInfo{};
    createInfo.sType = kMetalSurfaceCreateInfoType;
    createInfo.pLayer = nativeWindow.window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = createSurface(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS)
    {
        reason = "vkCreateMetalSurfaceEXT failed with VkResult "
            + std::to_string(static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return surface;
}

namespace VulkanMacOS
{

void* CreateOrAttachLayer(void* existingLayer, void* nativeViewHandle)
{
    NSView* view = (__bridge NSView*)nativeViewHandle;
    if (view == nil)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "VulkanPresenter: winId() produced no NSView; cannot host a CAMetalLayer");
        return existingLayer;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)existingLayer;
    const bool created = layer == nil;
    if (created)
    {
        layer = [CAMetalLayer layer];
        // MoltenVK selects the MTLDevice and the drawable pixel format from
        // the swapchain it creates on this layer, so only the presentation
        // policy is configured here. framebufferOnly stays YES: the presenter
        // renders into its own images and blits into the drawable.
        layer.framebufferOnly = YES;
        layer.opaque = YES;
        layer.presentsWithTransaction = NO;
        // Sublayers are laid out explicitly in UpdateLayerGeometry(); no
        // autoresizing, and no implicit animation on frame changes (a resize
        // must take effect on the same frame the swapchain is recreated).
        layer.anchorPoint = CGPointMake(0.0, 0.0);
        layer.actions = @{
            @"bounds": [NSNull null],
            @"position": [NSNull null],
            @"contents": [NSNull null],
            @"hidden": [NSNull null],
        };
        // The emulator's own frame limiter and the swapchain present mode own
        // pacing. Leaving CoreAnimation's display sync enabled would clamp
        // MAILBOX/IMMEDIATE back to the refresh rate.
        if ([layer respondsToSelector:@selector(setDisplaySyncEnabled:)])
            layer.displaySyncEnabled = NO;
        // Revealed only once a frame has reached the swapchain.
        layer.hidden = YES;
    }

    // Qt can rebuild the NSView (fullscreen, screen changes) and can replace
    // its backing layer, so re-parent unconditionally rather than assuming the
    // previous attachment survived.
    view.wantsLayer = YES;
    CALayer* hostLayer = view.layer;
    if (hostLayer == nil)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "VulkanPresenter: NSView has no backing layer to host the Vulkan surface");
        return created ? nullptr : existingLayer;
    }
    if (layer.superlayer != hostLayer)
    {
        [layer removeFromSuperlayer];
        [hostLayer addSublayer:layer];
    }

    if (created)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "VulkanPresenter: attached CAMetalLayer sublayer to NSView viewFlipped=%d",
            [view isFlipped] ? 1 : 0);
        return (void*)CFBridgingRetain(layer);
    }
    return existingLayer;
}

void UpdateLayerGeometry(void* layer, double contentsScale, int widthPoints, int heightPoints)
{
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
    if (metalLayer == nil)
        return;

    const CGFloat scale = contentsScale > 0.0 ? static_cast<CGFloat>(contentsScale) : 1.0;
    const CGFloat width = static_cast<CGFloat>(std::max(1, widthPoints));
    const CGFloat height = static_cast<CGFloat>(std::max(1, heightPoints));

    // Layer geometry is in points; the drawable is in device pixels. Keeping
    // them consistent is what makes the MoltenVK surface extent match the
    // presenter's own idea of the surface size.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    metalLayer.frame = CGRectMake(0.0, 0.0, width, height);
    metalLayer.contentsScale = scale;
    metalLayer.drawableSize = CGSizeMake(
        std::ceil(width * scale),
        std::ceil(height * scale));
    [CATransaction commit];
}

void SetLayerHidden(void* layer, bool hidden)
{
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
    if (metalLayer == nil || metalLayer.hidden == (hidden ? YES : NO))
        return;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    metalLayer.hidden = hidden ? YES : NO;
    [CATransaction commit];
}

void DestroyLayer(void* layer)
{
    if (layer == nullptr)
        return;
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
    [metalLayer removeFromSuperlayer];
    CFBridgingRelease(layer);
}

void RunInAutoreleasePool(const std::function<void()>& body)
{
    @autoreleasepool
    {
        body();
    }
}

} // namespace VulkanMacOS
} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __APPLE__; scatter-budget-exempt: native Vulkan surface adapter, not input dispatch
