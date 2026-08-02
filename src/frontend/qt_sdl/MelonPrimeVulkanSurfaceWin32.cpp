#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(_WIN32)

#include "MelonPrimeVulkanSurface.h"

#include <windows.h>

#include "VulkanDispatch.h"

namespace MelonPrime
{
namespace
{
// Keep Win32 surface declarations local so VK_USE_PLATFORM_WIN32_KHR does
// not leak into shared core compilation units.
struct Win32SurfaceCreateInfo
{
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
};

using CreateWin32SurfaceFn = VkResult (VKAPI_PTR *)(
    VkInstance,
    const Win32SurfaceCreateInfo*,
    const VkAllocationCallbacks*,
    VkSurfaceKHR*);
}

VkSurfaceKHR CreateVulkanSurface(
    VkInstance instance,
    const VulkanNativeWindowInfo& nativeWindow,
    std::string& reason)
{
    if (instance == VK_NULL_HANDLE
        || nativeWindow.type != VulkanNativeWindowType::Win32
        || nativeWindow.window == nullptr)
    {
        reason = "Win32 Vulkan surface requires a valid instance and HWND";
        return VK_NULL_HANDLE;
    }

    auto createSurface = reinterpret_cast<CreateWin32SurfaceFn>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (createSurface == nullptr)
    {
        reason = "vkCreateWin32SurfaceKHR is unavailable";
        return VK_NULL_HANDLE;
    }

    Win32SurfaceCreateInfo createInfo{};
    createInfo.sType = static_cast<VkStructureType>(1000009000); // VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = static_cast<HWND>(nativeWindow.window);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = createSurface(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS)
    {
        reason = "vkCreateWin32SurfaceKHR failed with VkResult " + std::to_string(static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return surface;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && _WIN32
