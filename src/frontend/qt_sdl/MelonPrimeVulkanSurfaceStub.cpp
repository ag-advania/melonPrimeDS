#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__APPLE__) // scatter-budget-exempt: unsupported Vulkan surface stub, not input dispatch

#include "MelonPrimeVulkanSurface.h"

namespace MelonPrime
{

VkSurfaceKHR CreateVulkanSurface(VkInstance, const VulkanNativeWindowInfo&, std::string& reason)
{
    reason = "A desktop Vulkan surface adapter is not available for this platform";
    return VK_NULL_HANDLE;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && __APPLE__; scatter-budget-exempt: unsupported Vulkan surface stub, not input dispatch
