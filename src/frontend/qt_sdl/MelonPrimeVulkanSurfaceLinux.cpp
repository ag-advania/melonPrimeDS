#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__) // scatter-budget-exempt: native Vulkan surface adapter, not input dispatch

#include "MelonPrimeVulkanSurface.h"

#include <cstdint>

#include "VulkanDispatch.h"

namespace MelonPrime
{
namespace
{
struct XlibSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* display;
    unsigned long window;
};

struct WaylandSurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* display;
    void* surface;
};

using CreateXlibSurfaceFn = VkResult (VKAPI_PTR *)(
    VkInstance,
    const XlibSurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);
using CreateWaylandSurfaceFn = VkResult (VKAPI_PTR *)(
    VkInstance,
    const WaylandSurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);
}

VkSurfaceKHR CreateVulkanSurface(
    VkInstance instance,
    const VulkanNativeWindowInfo& nativeWindow,
    std::string& reason)
{
    if (instance == VK_NULL_HANDLE || nativeWindow.display == nullptr || nativeWindow.window == nullptr)
    {
        reason = "Linux Vulkan surface requires valid display and window handles";
        return VK_NULL_HANDLE;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    if (nativeWindow.type == VulkanNativeWindowType::Xlib)
    {
        auto createSurface = reinterpret_cast<CreateXlibSurfaceFn>(
            vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
        if (createSurface == nullptr)
        {
            reason = "vkCreateXlibSurfaceKHR is unavailable";
            return VK_NULL_HANDLE;
        }
        XlibSurfaceCreateInfo createInfo{};
        createInfo.sType = static_cast<VkStructureType>(1000004000);
        createInfo.display = nativeWindow.display;
        createInfo.window = static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(nativeWindow.window));
        result = createSurface(instance, &createInfo, nullptr, &surface);
    }
    else if (nativeWindow.type == VulkanNativeWindowType::Wayland)
    {
        auto createSurface = reinterpret_cast<CreateWaylandSurfaceFn>(
            vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
        if (createSurface == nullptr)
        {
            reason = "vkCreateWaylandSurfaceKHR is unavailable";
            return VK_NULL_HANDLE;
        }
        WaylandSurfaceCreateInfo createInfo{};
        createInfo.sType = static_cast<VkStructureType>(1000006000);
        createInfo.display = nativeWindow.display;
        createInfo.surface = nativeWindow.window;
        result = createSurface(instance, &createInfo, nullptr, &surface);
    }
    else
    {
        reason = "Unsupported Linux Qt platform for Vulkan presentation";
        return VK_NULL_HANDLE;
    }

    if (result != VK_SUCCESS)
    {
        reason = "Linux Vulkan surface creation failed with VkResult "
            + std::to_string(static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return surface;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __linux__; scatter-budget-exempt: native Vulkan surface adapter, not input dispatch
