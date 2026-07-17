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
};

struct VulkanNativeWindowInfo
{
    VulkanNativeWindowType type = VulkanNativeWindowType::Unknown;
    void* display = nullptr;
    void* window = nullptr;
};

VkSurfaceKHR CreateVulkanSurface(
    VkInstance instance,
    const VulkanNativeWindowInfo& nativeWindow,
    std::string& reason);

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
