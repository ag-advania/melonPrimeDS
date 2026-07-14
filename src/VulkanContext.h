#pragma once
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include <atomic>
#include <mutex>
#include <string>
#include <volk.h>
#include "types.h"
namespace melonDS {
struct VulkanDeviceProfile {
 u32 VendorId=0, DeviceId=0; std::string DeviceName;
 bool IsQualcomm=false, IsAdreno=false, IsArmMali=false, IsPowerVR=false, IsMaliG52Class=false;
 bool IsNvidia=false, IsAmd=false, IsIntel=false;
};
class VulkanContext {
public:
 static VulkanContext& Get();
 VulkanContext(const VulkanContext&)=delete; VulkanContext& operator=(const VulkanContext&)=delete;
 bool Acquire(); void Release(); bool IsReady() const;
 VkInstance GetInstance() const{return Instance;} VkPhysicalDevice GetPhysicalDevice() const{return PhysicalDevice;}
 VkDevice GetDevice() const{return Device;} VkQueue GetQueue() const{return Queue;} u32 GetQueueFamilyIndex() const{return QueueFamilyIndex;}
 std::mutex& GetQueueLock(){return QueueLock;} bool SupportsTimestamps() const{return TimestampQueriesSupported && ResetQueryPool;}
 float GetTimestampPeriod() const{return TimestampPeriod;} const VulkanDeviceProfile& GetDeviceProfile() const{return DeviceProfile;}
 PFN_vkWaitSemaphoresKHR GetWaitSemaphores() const{return WaitSemaphores;} PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValue() const{return GetSemaphoreCounterValueFn;}
 PFN_vkResetQueryPoolEXT GetResetQueryPool() const{return ResetQueryPool;} bool SupportsTimelineSemaphores() const{return TimelineSemaphoresSupported;}
 bool SupportsDynamicTextureIndexing() const{return DynamicTextureIndexingSupported;} bool SupportsNonUniformTextureIndexing() const{return NonUniformTextureIndexingSupported;}
 bool IsTimelineSemaphoreForcedOff() const{return ForceDisableTimelineSemaphores;} bool IsDynamicTextureIndexingForcedOff() const{return ForceDisableDynamicTextureIndexing;}
 u32 FindMemoryType(u32 typeBits,VkMemoryPropertyFlags properties) const;
 static void SetCompatibilityOverrides(bool disableTimelineSemaphores,bool disableDynamicTextureIndexing);
private:
 VulkanContext()=default; ~VulkanContext()=default; bool initializeLocked(); void shutdownLocked();
 mutable std::mutex ContextLock; u32 ReferenceCount=0; VkInstance Instance=VK_NULL_HANDLE; VkDebugUtilsMessengerEXT DebugMessenger=VK_NULL_HANDLE;
 VkPhysicalDevice PhysicalDevice=VK_NULL_HANDLE; VkDevice Device=VK_NULL_HANDLE; VkQueue Queue=VK_NULL_HANDLE; u32 QueueFamilyIndex=0; std::mutex QueueLock;
 PFN_vkWaitSemaphoresKHR WaitSemaphores=nullptr; PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValueFn=nullptr; PFN_vkResetQueryPoolEXT ResetQueryPool=nullptr;
 float TimestampPeriod=0; bool TimestampQueriesSupported=false, TimelineSemaphoresSupported=false, DynamicTextureIndexingSupported=false, NonUniformTextureIndexingSupported=false;
 bool ForceDisableTimelineSemaphores=false, ForceDisableDynamicTextureIndexing=false; VulkanDeviceProfile DeviceProfile{};
}; }
