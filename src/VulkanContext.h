#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdint>
#include <mutex>
#include <string>
#include <vulkan/vulkan.h>

#include "types.h"

// Ported from SapphireRhodonite/melonDS-android-lib.
// Source pin: d77944275fa61f9b79cfcead2c3e98993429a023.
// Mobile external-buffer support is intentionally excluded on desktop.

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

    VkInstance GetInstance() const { return Instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
    VkDevice GetDevice() const { return Device; }
    VkQueue GetQueue() const { return Queue; }
    u32 GetQueueFamilyIndex() const { return QueueFamilyIndex; }
    std::mutex& GetQueueLock() { return QueueLock; }
    bool SupportsTimestamps() const { return TimestampQueriesSupported && ResetQueryPool != nullptr; }
    float GetTimestampPeriod() const { return TimestampPeriod; }
    const VulkanDeviceProfile& GetDeviceProfile() const { return DeviceProfile; }

    PFN_vkWaitSemaphoresKHR GetWaitSemaphores() const { return WaitSemaphores; }
    PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValue() const { return GetSemaphoreCounterValueFn; }
    PFN_vkResetQueryPoolEXT GetResetQueryPool() const { return ResetQueryPool; }
    bool SupportsTimelineSemaphores() const { return TimelineSemaphoresSupported; }
    bool SupportsDynamicTextureIndexing() const { return DynamicTextureIndexingSupported; }
    bool SupportsNonUniformTextureIndexing() const { return NonUniformTextureIndexingSupported; }
    bool IsTimelineSemaphoreForcedOff() const { return ForceDisableTimelineSemaphores; }
    bool IsDynamicTextureIndexingForcedOff() const { return ForceDisableDynamicTextureIndexing; }

    u32 FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;
    static void SetCompatibilityOverrides(bool disableTimelineSemaphores, bool disableDynamicTextureIndexing);

private:
    VulkanContext() = default;
    ~VulkanContext() = default;

    bool initializeLocked();
    void shutdownLocked();

private:
    mutable std::mutex ContextLock;
    u32 ReferenceCount = 0;

    VkInstance Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    u32 QueueFamilyIndex = 0;
    std::mutex QueueLock;

    PFN_vkWaitSemaphoresKHR WaitSemaphores = nullptr;
    PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValueFn = nullptr;
    PFN_vkResetQueryPoolEXT ResetQueryPool = nullptr;
    float TimestampPeriod = 0.0f;
    bool TimestampQueriesSupported = false;
    bool TimelineSemaphoresSupported = false;
    bool DynamicTextureIndexingSupported = false;
    bool NonUniformTextureIndexingSupported = false;
    bool ForceDisableTimelineSemaphores = false;
    bool ForceDisableDynamicTextureIndexing = false;
    VulkanDeviceProfile DeviceProfile{};
};

}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
