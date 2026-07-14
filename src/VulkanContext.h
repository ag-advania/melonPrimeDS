#pragma once

// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <volk.h>

#include "types.h"

namespace melonDS
{

struct VulkanDeviceProfile
{
    u32 VendorId = 0;
    u32 DeviceId = 0;
    std::string DeviceName;
    bool IsQualcomm = false;
    bool IsAdreno = false;
    bool IsArmMali = false;
    bool IsPowerVR = false;
    bool IsMaliG52Class = false;
    bool IsNvidia = false;
    bool IsAmd = false;
    bool IsIntel = false;
};

enum class VulkanDesktopSurfaceBackend : u8
{
    None = 0,
    Win32,
    Xcb,
    Xlib,
    Wayland,
    Metal,
};

struct VulkanPlatformRequirements
{
    std::vector<std::string> InstanceExtensions;
    VulkanDesktopSurfaceBackend PreferredSurfaceBackend =
        VulkanDesktopSurfaceBackend::None;
    VkInstanceCreateFlags InstanceFlags = 0;
    bool SurfaceExtensionsReady = false;
    bool PortabilityEnumerationEnabled = false;
};

class VulkanContext
{
public:
    static VulkanContext& Get();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool Acquire();
    void Release();
    bool IsReady() const;

    // Surface creation is intentionally deferred to the frontend. The device
    // creates one queue from every non-empty family so a surface-specific
    // present family can be resolved later without creating a second device.
    bool ResolvePresentQueue(VkSurfaceKHR surface);
    [[nodiscard]] bool IsPresentQueueResolved() const { return PresentQueueResolved; }
    [[nodiscard]] bool RequiresSeparatePresentQueue() const
    {
        return PresentQueueResolved
            && PresentQueueFamilyIndex != GraphicsQueueFamilyIndex;
    }

    VkInstance GetInstance() const { return Instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
    VkDevice GetDevice() const { return Device; }
    VkQueue GetGraphicsQueue() const { return GraphicsQueue; }
    VkQueue GetPresentQueue() const
    {
        return PresentQueueResolved ? PresentQueue : GraphicsQueue;
    }
    u32 GetGraphicsQueueFamily() const { return GraphicsQueueFamilyIndex; }
    u32 GetPresentQueueFamily() const
    {
        return PresentQueueResolved ? PresentQueueFamilyIndex : GraphicsQueueFamilyIndex;
    }

    // Compatibility aliases retained until downstream call sites migrate.
    VkQueue GetQueue() const { return GetGraphicsQueue(); }
    u32 GetQueueFamilyIndex() const { return GetGraphicsQueueFamily(); }

    std::mutex& GetQueueLock() { return QueueLock; }
    bool SupportsTimestamps() const
    {
        return TimestampQueriesSupported && ResetQueryPool;
    }
    float GetTimestampPeriod() const { return TimestampPeriod; }
    const VulkanDeviceProfile& GetDeviceProfile() const { return DeviceProfile; }
    const VulkanPlatformRequirements& GetPlatformRequirements() const
    {
        return PlatformRequirements;
    }

    PFN_vkWaitSemaphoresKHR GetWaitSemaphores() const { return WaitSemaphores; }
    PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValue() const
    {
        return GetSemaphoreCounterValueFn;
    }
    PFN_vkResetQueryPoolEXT GetResetQueryPool() const { return ResetQueryPool; }
    bool SupportsTimelineSemaphores() const { return TimelineSemaphoresSupported; }
    bool SupportsDynamicTextureIndexing() const { return DynamicTextureIndexingSupported; }
    bool SupportsNonUniformTextureIndexing() const { return NonUniformTextureIndexingSupported; }
    bool IsTimelineSemaphoreForcedOff() const { return ForceDisableTimelineSemaphores; }
    bool IsDynamicTextureIndexingForcedOff() const { return ForceDisableDynamicTextureIndexing; }

    u32 FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;
    static void SetCompatibilityOverrides(
        bool disableTimelineSemaphores,
        bool disableDynamicTextureIndexing);

private:
    VulkanContext() = default;
    ~VulkanContext() = default;

    bool initializeLocked();
    void shutdownLocked();

    mutable std::mutex ContextLock;
    u32 ReferenceCount = 0;
    VkInstance Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    VkQueue PresentQueue = VK_NULL_HANDLE;
    u32 GraphicsQueueFamilyIndex = UINT32_MAX;
    u32 PresentQueueFamilyIndex = UINT32_MAX;
    bool PresentQueueResolved = false;
    std::vector<u32> CreatedQueueFamilies;
    std::mutex QueueLock;

    PFN_vkWaitSemaphoresKHR WaitSemaphores = nullptr;
    PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValueFn = nullptr;
    PFN_vkResetQueryPoolEXT ResetQueryPool = nullptr;
    float TimestampPeriod = 0;
    bool TimestampQueriesSupported = false;
    bool TimelineSemaphoresSupported = false;
    bool DynamicTextureIndexingSupported = false;
    bool NonUniformTextureIndexingSupported = false;
    bool ForceDisableTimelineSemaphores = false;
    bool ForceDisableDynamicTextureIndexing = false;
    VulkanDeviceProfile DeviceProfile{};
    VulkanPlatformRequirements PlatformRequirements{};
};

} // namespace melonDS
