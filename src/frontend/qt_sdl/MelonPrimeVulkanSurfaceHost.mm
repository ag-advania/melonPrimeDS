#include "MelonPrimeVulkanSurfaceHost.h"

#if !defined(__APPLE__)
#error "MelonPrimeVulkanSurfaceHost.mm is Apple-only"
#endif

#import <AppKit/NSView.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QWidget>

#include "Platform.h"
#include "VulkanContext.h"

namespace
{
std::atomic<melonDS::u64> gNextSurfaceGeneration{0};

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

NSView* NativeView(QWidget& widget)
{
    return (__bridge NSView*)reinterpret_cast<void*>(static_cast<quintptr>(widget.winId()));
}
} // namespace

bool MelonPrimeVulkanSurfaceHost::createForWidget(QWidget& newWidget, VkInstance instance)
{
    if (instance == VK_NULL_HANDLE || surfaceHandle != VK_NULL_HANDLE)
        return false;

    NSView* view = NativeView(newWidget);
    if (view == nil)
        return false;

    [view setWantsLayer:YES];
    CAMetalLayer* layer = [view.layer isKindOfClass:[CAMetalLayer class]]
        ? (CAMetalLayer*)view.layer
        : [CAMetalLayer layer];
    view.layer = layer;
    const QSize size = WidgetPixelSize(newWidget);
    layer.contentsScale = std::max<qreal>(newWidget.devicePixelRatioF(), 1.0);
    layer.drawableSize = CGSizeMake(size.width(), size.height());

    VkMetalSurfaceCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    createInfo.pLayer = layer;
    VkSurfaceKHR newSurface = VK_NULL_HANDLE;
    const VkResult result = vkCreateMetalSurfaceEXT(
        instance, &createInfo, nullptr, &newSurface);
    if (result != VK_SUCCESS || newSurface == VK_NULL_HANDLE)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan surface host: Metal surface creation failed (%d)\n",
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
    system = VulkanWindowSystem::Metal;
    nativeDisplayIdentity = 0;
    nativeWindowIdentity = reinterpret_cast<quintptr>((__bridge void*)view);
    platformLayer = (__bridge void*)layer;
    surfaceGeneration = NextSurfaceGeneration();
    return true;
}

void MelonPrimeVulkanSurfaceHost::destroy(VkInstance instance)
{
    if (surfaceHandle != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, surfaceHandle, nullptr);
    surfaceHandle = VK_NULL_HANDLE;
    platformLayer = nullptr;
    system = VulkanWindowSystem::None;
    nativeDisplayIdentity = 0;
    nativeWindowIdentity = 0;
    widget.clear();
}

bool MelonPrimeVulkanSurfaceHost::matchesWidget(QWidget& candidate) const
{
    NSView* view = NativeView(candidate);
    return surfaceHandle != VK_NULL_HANDLE
        && widget == &candidate
        && system == VulkanWindowSystem::Metal
        && view != nil
        && reinterpret_cast<quintptr>((__bridge void*)view) == nativeWindowIdentity
        && platformLayer != nullptr
        && (__bridge void*)view.layer == platformLayer;
}

QSize MelonPrimeVulkanSurfaceHost::pixelSize() const
{
    if (!widget)
        return {};
    const QSize size = WidgetPixelSize(*widget);
    if (platformLayer != nullptr)
    {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)platformLayer;
        layer.contentsScale = std::max<qreal>(widget->devicePixelRatioF(), 1.0);
        layer.drawableSize = CGSizeMake(size.width(), size.height());
    }
    return size;
}
