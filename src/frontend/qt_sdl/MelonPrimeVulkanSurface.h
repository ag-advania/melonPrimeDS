#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>
#include <vulkan/vulkan.h>

namespace MelonPrime
{

enum class VulkanNativeWindowType
{
    Unknown,
    Win32,
    Xlib,
    Wayland,
    Metal,
};

struct VulkanNativeWindowInfo
{
    VulkanNativeWindowType type = VulkanNativeWindowType::Unknown;
    void* display = nullptr;
    // Win32: HWND. Xlib: Window. Wayland: wl_surface*.
    // Metal (macOS): the CAMetalLayer the presenter owns, created and attached
    // to the panel's NSView by MelonPrimeVulkanSurfaceMacOS.
    void* window = nullptr;
};

VkSurfaceKHR CreateVulkanSurface(
    VkInstance instance,
    const VulkanNativeWindowInfo& nativeWindow,
    std::string& reason);

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
