#include "VulkanContext.h"
#include "Platform.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
namespace melonDS { namespace {
std::atomic<bool> gNoTimeline{false},gNoDynamic{false};
bool hasExt(const char* n,const std::vector<VkExtensionProperties>& e){for(auto&x:e)if(!std::strcmp(n,x.extensionName))return true;return false;}
int deviceScore(VkPhysicalDevice d){VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(d,&p);int s=0;if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)s+=1000;else if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)s+=500;s+=int(p.limits.maxImageDimension2D/1024);return s;}
}
VulkanContext& VulkanContext::Get(){static VulkanContext c;return c;}
void VulkanContext::SetCompatibilityOverrides(bool a,bool b){gNoTimeline.store(a);gNoDynamic.store(b);}
bool VulkanContext::Acquire(){std::lock_guard<std::mutex> l(ContextLock);if(ReferenceCount++){return Device!=VK_NULL_HANDLE;}if(!initializeLocked()){ReferenceCount=0;return false;}return true;}
void VulkanContext::Release(){std::lock_guard<std::mutex> l(ContextLock);if(!ReferenceCount)return;if(--ReferenceCount==0)shutdownLocked();}
bool VulkanContext::IsReady()const{std::lock_guard<std::mutex> l(ContextLock);return Device!=VK_NULL_HANDLE;}
bool VulkanContext::initializeLocked(){
 ForceDisableTimelineSemaphores=gNoTimeline.load(); ForceDisableDynamicTextureIndexing=gNoDynamic.load();
 if(volkInitialize()!=VK_SUCCESS){Platform::Log(Platform::LogLevel::Error,"[MelonPrime] VulkanContext: volkInitialize failed\n");return false;}
 std::vector<const char*> ie{VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef _WIN32
 ie.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
 VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};ai.pApplicationName="melonPrimeDS";ai.applicationVersion=1;ai.pEngineName="melonPrimeDS";ai.engineVersion=1;ai.apiVersion=VK_API_VERSION_1_1;
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&ai;ici.enabledExtensionCount=(u32)ie.size();ici.ppEnabledExtensionNames=ie.data();
 if(vkCreateInstance(&ici,nullptr,&Instance)!=VK_SUCCESS){Platform::Log(Platform::LogLevel::Error,"[MelonPrime] VulkanContext: vkCreateInstance failed\n");return false;}volkLoadInstance(Instance);
 u32 count=0;vkEnumeratePhysicalDevices(Instance,&count,nullptr);if(!count)return false;std::vector<VkPhysicalDevice> ds(count);vkEnumeratePhysicalDevices(Instance,&count,ds.data());
 PhysicalDevice=*std::max_element(ds.begin(),ds.end(),[](auto a,auto b){return deviceScore(a)<deviceScore(b);});
 VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(PhysicalDevice,&props);TimestampPeriod=props.limits.timestampPeriod;TimestampQueriesSupported=props.limits.timestampComputeAndGraphics;
 DeviceProfile.VendorId=props.vendorID;DeviceProfile.DeviceId=props.deviceID;DeviceProfile.DeviceName=props.deviceName;DeviceProfile.IsNvidia=props.vendorID==0x10DE;DeviceProfile.IsAmd=props.vendorID==0x1002||props.vendorID==0x1022;DeviceProfile.IsIntel=props.vendorID==0x8086;
 u32 qc=0;vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice,&qc,nullptr);std::vector<VkQueueFamilyProperties> q(qc);vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice,&qc,q.data());QueueFamilyIndex=UINT32_MAX;for(u32 i=0;i<qc;i++)if(q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){QueueFamilyIndex=i;break;}if(QueueFamilyIndex==UINT32_MAX)return false;
 u32 ec=0;vkEnumerateDeviceExtensionProperties(PhysicalDevice,nullptr,&ec,nullptr);std::vector<VkExtensionProperties> ex(ec);vkEnumerateDeviceExtensionProperties(PhysicalDevice,nullptr,&ec,ex.data());
 std::vector<const char*> de{VK_KHR_SWAPCHAIN_EXTENSION_NAME};bool timeline=hasExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,ex)&&!ForceDisableTimelineSemaphores;bool indexing=hasExt(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,ex)&&!ForceDisableDynamicTextureIndexing;
 if(timeline)de.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);if(indexing)de.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);if(hasExt(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,ex))de.push_back(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
 VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};VkPhysicalDeviceTimelineSemaphoreFeatures tf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};VkPhysicalDeviceDescriptorIndexingFeatures df{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
 void** next=&f2.pNext;if(timeline){*next=&tf;next=&tf.pNext;}if(indexing){*next=&df;next=&df.pNext;}vkGetPhysicalDeviceFeatures2(PhysicalDevice,&f2);
 if(timeline)tf.timelineSemaphore=VK_TRUE;if(indexing){df.shaderSampledImageArrayNonUniformIndexing=df.shaderSampledImageArrayNonUniformIndexing;df.runtimeDescriptorArray=df.runtimeDescriptorArray;df.descriptorBindingPartiallyBound=df.descriptorBindingPartiallyBound;}
 float priority=1.0f;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=QueueFamilyIndex;qi.queueCount=1;qi.pQueuePriorities=&priority;
 VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.pNext=f2.pNext;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.enabledExtensionCount=(u32)de.size();di.ppEnabledExtensionNames=de.data();di.pEnabledFeatures=&f2.features;
 if(vkCreateDevice(PhysicalDevice,&di,nullptr,&Device)!=VK_SUCCESS){Platform::Log(Platform::LogLevel::Error,"[MelonPrime] VulkanContext: vkCreateDevice failed\n");return false;}volkLoadDevice(Device);vkGetDeviceQueue(Device,QueueFamilyIndex,0,&Queue);
 WaitSemaphores=(PFN_vkWaitSemaphoresKHR)vkGetDeviceProcAddr(Device,"vkWaitSemaphoresKHR");GetSemaphoreCounterValueFn=(PFN_vkGetSemaphoreCounterValueKHR)vkGetDeviceProcAddr(Device,"vkGetSemaphoreCounterValueKHR");ResetQueryPool=(PFN_vkResetQueryPoolEXT)vkGetDeviceProcAddr(Device,"vkResetQueryPoolEXT");
 TimelineSemaphoresSupported=timeline&&WaitSemaphores&&GetSemaphoreCounterValueFn;DynamicTextureIndexingSupported=indexing&&df.runtimeDescriptorArray;NonUniformTextureIndexingSupported=indexing&&df.shaderSampledImageArrayNonUniformIndexing;
 Platform::Log(Platform::LogLevel::Info,"[MelonPrime] VulkanContext ready: device=%s vendor=%04x timeline=%d indexing=%d\n",props.deviceName,props.vendorID,TimelineSemaphoresSupported?1:0,DynamicTextureIndexingSupported?1:0);return true;
}
void VulkanContext::shutdownLocked(){if(Device){vkDeviceWaitIdle(Device);vkDestroyDevice(Device,nullptr);}Device=VK_NULL_HANDLE;Queue=VK_NULL_HANDLE;PhysicalDevice=VK_NULL_HANDLE;if(Instance)vkDestroyInstance(Instance,nullptr);Instance=VK_NULL_HANDLE;}
u32 VulkanContext::FindMemoryType(u32 bits,VkMemoryPropertyFlags flags)const{VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(PhysicalDevice,&p);for(u32 i=0;i<p.memoryTypeCount;i++)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;return UINT32_MAX;}
}
