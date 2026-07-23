#ifdef VK_USE_PLATFORM_WIN32_KHR
// MELONPRIME-PC-ADAPT: windows.h must precede vulkan_win32 types pulled in via VulkanDispatch.h
#include <windows.h>
#endif

#include "VulkanDispatch.h"

#include <cstring>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <mutex>
#include <utility>

#include "Platform.h"

#if MELONDS_HAS_ADRENOTOOLS && defined(__ANDROID__)
// MELONPRIME-PC-ADAPT: adrenotools custom-driver loading is Android-only
#include <adrenotools/driver.h>
#include <adrenotools/priv.h>
#endif

PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
PFN_vkAllocateMemory vkAllocateMemory = nullptr;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
PFN_vkBindImageMemory vkBindImageMemory = nullptr;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = nullptr;
PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = nullptr;
PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;
PFN_vkCmdClearColorImage vkCmdClearColorImage = nullptr;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer = nullptr;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
PFN_vkCmdCopyImage vkCmdCopyImage = nullptr;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
PFN_vkCmdDispatch vkCmdDispatch = nullptr;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect = nullptr;
PFN_vkCmdDraw vkCmdDraw = nullptr;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
PFN_vkCmdFillBuffer vkCmdFillBuffer = nullptr;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
PFN_vkCmdPushConstants vkCmdPushConstants = nullptr;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants = nullptr;
PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask = nullptr;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference = nullptr;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask = nullptr;
PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
#ifdef __ANDROID__
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR = nullptr; // MELONPRIME-PC-ADAPT: Android-only PFN type
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
// MELONPRIME-PC-ADAPT: Win32 surface loader mirrors the Android one
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = nullptr;
#endif
PFN_vkCreateBuffer vkCreateBuffer = nullptr;
PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
PFN_vkCreateDevice vkCreateDevice = nullptr;
PFN_vkCreateFence vkCreateFence = nullptr;
PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
PFN_vkCreateImage vkCreateImage = nullptr;
PFN_vkCreateImageView vkCreateImageView = nullptr;
PFN_vkCreateInstance vkCreateInstance = nullptr;
PFN_vkCreatePipelineCache vkCreatePipelineCache = nullptr;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
PFN_vkCreateSampler vkCreateSampler = nullptr;
PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
PFN_vkDestroyDevice vkDestroyDevice = nullptr;
PFN_vkDestroyFence vkDestroyFence = nullptr;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
PFN_vkDestroyImage vkDestroyImage = nullptr;
PFN_vkDestroyImageView vkDestroyImageView = nullptr;
PFN_vkDestroyInstance vkDestroyInstance = nullptr;
PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache = nullptr;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
PFN_vkDestroySampler vkDestroySampler = nullptr;
PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets = nullptr;
PFN_vkFreeMemory vkFreeMemory = nullptr;
#ifdef __ANDROID__
PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID = nullptr; // MELONPRIME-PC-ADAPT: Android-only PFN type
#endif
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
PFN_vkGetFenceStatus vkGetFenceStatus = nullptr;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR = nullptr;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData = nullptr;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
PFN_vkGetSemaphoreCounterValue vkGetSemaphoreCounterValue = nullptr;
PFN_vkGetSemaphoreCounterValueKHR vkGetSemaphoreCounterValueKHR = nullptr;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
PFN_vkMapMemory vkMapMemory = nullptr;
PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
PFN_vkQueueSubmit vkQueueSubmit = nullptr;
PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
PFN_vkResetFences vkResetFences = nullptr;
PFN_vkResetQueryPool vkResetQueryPool = nullptr;
PFN_vkResetQueryPoolEXT vkResetQueryPoolEXT = nullptr;
PFN_vkUnmapMemory vkUnmapMemory = nullptr;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
PFN_vkWaitForFences vkWaitForFences = nullptr;
PFN_vkWaitSemaphores vkWaitSemaphores = nullptr;
PFN_vkWaitSemaphoresKHR vkWaitSemaphoresKHR = nullptr;

namespace melonDS::VulkanDispatch
{
namespace
{
std::mutex gLock;
DriverConfiguration gConfiguration;
void* gVulkanHandle = nullptr;
bool gInitialized = false;
bool gUsingCustomDriver = false;

bool sameConfiguration(const DriverConfiguration& left, const DriverConfiguration& right)
{
    return left.UseCustomDriver == right.UseCustomDriver &&
        left.TmpLibDir == right.TmpLibDir &&
        left.HookLibDir == right.HookLibDir &&
        left.CustomDriverDir == right.CustomDriverDir &&
        left.CustomDriverName == right.CustomDriverName &&
        left.DisplayName == right.DisplayName;
}

void* loadSymbol(const char* name)
{
#ifdef _WIN32
    // MELONPRIME-PC-ADAPT: mirror dlsym() via GetProcAddress() on Windows
    return gVulkanHandle != nullptr
        ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gVulkanHandle), name))
        : nullptr;
#else
    return gVulkanHandle != nullptr ? dlsym(gVulkanHandle, name) : nullptr;
#endif
}

bool isNullInstanceGlobalProc(const char* name)
{
    return std::strcmp(name, "vkCreateInstance") == 0 ||
        std::strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0 ||
        std::strcmp(name, "vkEnumerateInstanceLayerProperties") == 0 ||
        std::strcmp(name, "vkGetInstanceProcAddr") == 0;
}

template <typename T>
void loadGlobal(T& target, const char* name)
{
    target = nullptr;
    if (vkGetInstanceProcAddr != nullptr && isNullInstanceGlobalProc(name))
        target = reinterpret_cast<T>(vkGetInstanceProcAddr(VK_NULL_HANDLE, name));
    if (target == nullptr)
        target = reinterpret_cast<T>(loadSymbol(name));
}

template <typename T>
void loadInstance(VkInstance instance, T& target, const char* name)
{
    target = nullptr;
    if (vkGetInstanceProcAddr != nullptr)
        target = reinterpret_cast<T>(vkGetInstanceProcAddr(instance, name));
    if (target == nullptr)
        target = reinterpret_cast<T>(loadSymbol(name));
}


void loadGlobalSymbols()
{
    loadGlobal(vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
    loadGlobal(vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
    loadGlobal(vkAllocateDescriptorSets, "vkAllocateDescriptorSets");
    loadGlobal(vkAllocateMemory, "vkAllocateMemory");
    loadGlobal(vkBeginCommandBuffer, "vkBeginCommandBuffer");
    loadGlobal(vkBindBufferMemory, "vkBindBufferMemory");
    loadGlobal(vkBindImageMemory, "vkBindImageMemory");
    loadGlobal(vkCmdBeginRenderPass, "vkCmdBeginRenderPass");
    loadGlobal(vkCmdBindDescriptorSets, "vkCmdBindDescriptorSets");
    loadGlobal(vkCmdBindIndexBuffer, "vkCmdBindIndexBuffer");
    loadGlobal(vkCmdBindPipeline, "vkCmdBindPipeline");
    loadGlobal(vkCmdBindVertexBuffers, "vkCmdBindVertexBuffers");
    loadGlobal(vkCmdBlitImage, "vkCmdBlitImage");
    loadGlobal(vkCmdClearColorImage, "vkCmdClearColorImage");
    loadGlobal(vkCmdCopyBuffer, "vkCmdCopyBuffer");
    loadGlobal(vkCmdCopyBufferToImage, "vkCmdCopyBufferToImage");
    loadGlobal(vkCmdCopyImage, "vkCmdCopyImage");
    loadGlobal(vkCmdCopyImageToBuffer, "vkCmdCopyImageToBuffer");
    loadGlobal(vkCmdDispatch, "vkCmdDispatch");
    loadGlobal(vkCmdDispatchIndirect, "vkCmdDispatchIndirect");
    loadGlobal(vkCmdDraw, "vkCmdDraw");
    loadGlobal(vkCmdDrawIndexed, "vkCmdDrawIndexed");
    loadGlobal(vkCmdEndRenderPass, "vkCmdEndRenderPass");
    loadGlobal(vkCmdFillBuffer, "vkCmdFillBuffer");
    loadGlobal(vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
    loadGlobal(vkCmdPushConstants, "vkCmdPushConstants");
    loadGlobal(vkCmdSetBlendConstants, "vkCmdSetBlendConstants");
    loadGlobal(vkCmdSetScissor, "vkCmdSetScissor");
    loadGlobal(vkCmdSetStencilCompareMask, "vkCmdSetStencilCompareMask");
    loadGlobal(vkCmdSetStencilReference, "vkCmdSetStencilReference");
    loadGlobal(vkCmdSetStencilWriteMask, "vkCmdSetStencilWriteMask");
    loadGlobal(vkCmdSetViewport, "vkCmdSetViewport");
    loadGlobal(vkCmdWriteTimestamp, "vkCmdWriteTimestamp");
#ifdef __ANDROID__
    loadGlobal(vkCreateAndroidSurfaceKHR, "vkCreateAndroidSurfaceKHR"); // MELONPRIME-PC-ADAPT
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    loadGlobal(vkCreateWin32SurfaceKHR, "vkCreateWin32SurfaceKHR"); // MELONPRIME-PC-ADAPT
#endif
    loadGlobal(vkCreateBuffer, "vkCreateBuffer");
    loadGlobal(vkCreateCommandPool, "vkCreateCommandPool");
    loadGlobal(vkCreateComputePipelines, "vkCreateComputePipelines");
    loadGlobal(vkCreateDebugUtilsMessengerEXT, "vkCreateDebugUtilsMessengerEXT");
    loadGlobal(vkCreateDescriptorPool, "vkCreateDescriptorPool");
    loadGlobal(vkCreateDescriptorSetLayout, "vkCreateDescriptorSetLayout");
    loadGlobal(vkCreateDevice, "vkCreateDevice");
    loadGlobal(vkCreateFence, "vkCreateFence");
    loadGlobal(vkCreateFramebuffer, "vkCreateFramebuffer");
    loadGlobal(vkCreateGraphicsPipelines, "vkCreateGraphicsPipelines");
    loadGlobal(vkCreateImage, "vkCreateImage");
    loadGlobal(vkCreateImageView, "vkCreateImageView");
    loadGlobal(vkCreateInstance, "vkCreateInstance");
    loadGlobal(vkCreatePipelineCache, "vkCreatePipelineCache");
    loadGlobal(vkCreatePipelineLayout, "vkCreatePipelineLayout");
    loadGlobal(vkCreateQueryPool, "vkCreateQueryPool");
    loadGlobal(vkCreateRenderPass, "vkCreateRenderPass");
    loadGlobal(vkCreateSampler, "vkCreateSampler");
    loadGlobal(vkCreateSemaphore, "vkCreateSemaphore");
    loadGlobal(vkCreateShaderModule, "vkCreateShaderModule");
    loadGlobal(vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
    loadGlobal(vkDestroyBuffer, "vkDestroyBuffer");
    loadGlobal(vkDestroyCommandPool, "vkDestroyCommandPool");
    loadGlobal(vkDestroyDebugUtilsMessengerEXT, "vkDestroyDebugUtilsMessengerEXT");
    loadGlobal(vkDestroyDescriptorPool, "vkDestroyDescriptorPool");
    loadGlobal(vkDestroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
    loadGlobal(vkDestroyDevice, "vkDestroyDevice");
    loadGlobal(vkDestroyFence, "vkDestroyFence");
    loadGlobal(vkDestroyFramebuffer, "vkDestroyFramebuffer");
    loadGlobal(vkDestroyImage, "vkDestroyImage");
    loadGlobal(vkDestroyImageView, "vkDestroyImageView");
    loadGlobal(vkDestroyInstance, "vkDestroyInstance");
    loadGlobal(vkDestroyPipeline, "vkDestroyPipeline");
    loadGlobal(vkDestroyPipelineCache, "vkDestroyPipelineCache");
    loadGlobal(vkDestroyPipelineLayout, "vkDestroyPipelineLayout");
    loadGlobal(vkDestroyQueryPool, "vkDestroyQueryPool");
    loadGlobal(vkDestroyRenderPass, "vkDestroyRenderPass");
    loadGlobal(vkDestroySampler, "vkDestroySampler");
    loadGlobal(vkDestroySemaphore, "vkDestroySemaphore");
    loadGlobal(vkDestroyShaderModule, "vkDestroyShaderModule");
    loadGlobal(vkDestroySurfaceKHR, "vkDestroySurfaceKHR");
    loadGlobal(vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
    loadGlobal(vkDeviceWaitIdle, "vkDeviceWaitIdle");
    loadGlobal(vkEndCommandBuffer, "vkEndCommandBuffer");
    loadGlobal(vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
    loadGlobal(vkEnumerateInstanceExtensionProperties, "vkEnumerateInstanceExtensionProperties");
    loadGlobal(vkEnumerateInstanceLayerProperties, "vkEnumerateInstanceLayerProperties");
    loadGlobal(vkEnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    loadGlobal(vkFreeCommandBuffers, "vkFreeCommandBuffers");
    loadGlobal(vkFreeDescriptorSets, "vkFreeDescriptorSets");
    loadGlobal(vkFreeMemory, "vkFreeMemory");
#ifdef __ANDROID__
    loadGlobal(vkGetAndroidHardwareBufferPropertiesANDROID, "vkGetAndroidHardwareBufferPropertiesANDROID"); // MELONPRIME-PC-ADAPT
#endif
    loadGlobal(vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    loadGlobal(vkGetDeviceProcAddr, "vkGetDeviceProcAddr");
    loadGlobal(vkGetDeviceQueue, "vkGetDeviceQueue");
    loadGlobal(vkGetFenceStatus, "vkGetFenceStatus");
    loadGlobal(vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
    loadGlobal(vkGetPhysicalDeviceFeatures, "vkGetPhysicalDeviceFeatures");
    loadGlobal(vkGetPhysicalDeviceFeatures2, "vkGetPhysicalDeviceFeatures2");
    loadGlobal(vkGetPhysicalDeviceFeatures2KHR, "vkGetPhysicalDeviceFeatures2KHR");
    loadGlobal(vkGetPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
    loadGlobal(vkGetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    loadGlobal(vkGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    loadGlobal(vkGetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    loadGlobal(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    loadGlobal(vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    loadGlobal(vkGetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    loadGlobal(vkGetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
    loadGlobal(vkGetPipelineCacheData, "vkGetPipelineCacheData");
    loadGlobal(vkGetQueryPoolResults, "vkGetQueryPoolResults");
    loadGlobal(vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValue");
    loadGlobal(vkGetSemaphoreCounterValueKHR, "vkGetSemaphoreCounterValueKHR");
    loadGlobal(vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
    loadGlobal(vkMapMemory, "vkMapMemory");
    loadGlobal(vkQueuePresentKHR, "vkQueuePresentKHR");
    loadGlobal(vkQueueSubmit, "vkQueueSubmit");
    loadGlobal(vkQueueWaitIdle, "vkQueueWaitIdle");
    loadGlobal(vkResetCommandBuffer, "vkResetCommandBuffer");
    loadGlobal(vkResetFences, "vkResetFences");
    loadGlobal(vkResetQueryPool, "vkResetQueryPool");
    loadGlobal(vkResetQueryPoolEXT, "vkResetQueryPoolEXT");
    loadGlobal(vkUnmapMemory, "vkUnmapMemory");
    loadGlobal(vkUpdateDescriptorSets, "vkUpdateDescriptorSets");
    loadGlobal(vkWaitForFences, "vkWaitForFences");
    loadGlobal(vkWaitSemaphores, "vkWaitSemaphores");
    loadGlobal(vkWaitSemaphoresKHR, "vkWaitSemaphoresKHR");
}

void loadInstanceSymbols(VkInstance instance)
{
    loadInstance(instance, vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
    loadInstance(instance, vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
    loadInstance(instance, vkAllocateDescriptorSets, "vkAllocateDescriptorSets");
    loadInstance(instance, vkAllocateMemory, "vkAllocateMemory");
    loadInstance(instance, vkBeginCommandBuffer, "vkBeginCommandBuffer");
    loadInstance(instance, vkBindBufferMemory, "vkBindBufferMemory");
    loadInstance(instance, vkBindImageMemory, "vkBindImageMemory");
    loadInstance(instance, vkCmdBeginRenderPass, "vkCmdBeginRenderPass");
    loadInstance(instance, vkCmdBindDescriptorSets, "vkCmdBindDescriptorSets");
    loadInstance(instance, vkCmdBindIndexBuffer, "vkCmdBindIndexBuffer");
    loadInstance(instance, vkCmdBindPipeline, "vkCmdBindPipeline");
    loadInstance(instance, vkCmdBindVertexBuffers, "vkCmdBindVertexBuffers");
    loadInstance(instance, vkCmdBlitImage, "vkCmdBlitImage");
    loadInstance(instance, vkCmdClearColorImage, "vkCmdClearColorImage");
    loadInstance(instance, vkCmdCopyBuffer, "vkCmdCopyBuffer");
    loadInstance(instance, vkCmdCopyBufferToImage, "vkCmdCopyBufferToImage");
    loadInstance(instance, vkCmdCopyImage, "vkCmdCopyImage");
    loadInstance(instance, vkCmdCopyImageToBuffer, "vkCmdCopyImageToBuffer");
    loadInstance(instance, vkCmdDispatch, "vkCmdDispatch");
    loadInstance(instance, vkCmdDispatchIndirect, "vkCmdDispatchIndirect");
    loadInstance(instance, vkCmdDraw, "vkCmdDraw");
    loadInstance(instance, vkCmdDrawIndexed, "vkCmdDrawIndexed");
    loadInstance(instance, vkCmdEndRenderPass, "vkCmdEndRenderPass");
    loadInstance(instance, vkCmdFillBuffer, "vkCmdFillBuffer");
    loadInstance(instance, vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
    loadInstance(instance, vkCmdPushConstants, "vkCmdPushConstants");
    loadInstance(instance, vkCmdSetBlendConstants, "vkCmdSetBlendConstants");
    loadInstance(instance, vkCmdSetScissor, "vkCmdSetScissor");
    loadInstance(instance, vkCmdSetStencilCompareMask, "vkCmdSetStencilCompareMask");
    loadInstance(instance, vkCmdSetStencilReference, "vkCmdSetStencilReference");
    loadInstance(instance, vkCmdSetStencilWriteMask, "vkCmdSetStencilWriteMask");
    loadInstance(instance, vkCmdSetViewport, "vkCmdSetViewport");
    loadInstance(instance, vkCmdWriteTimestamp, "vkCmdWriteTimestamp");
#ifdef __ANDROID__
    loadInstance(instance, vkCreateAndroidSurfaceKHR, "vkCreateAndroidSurfaceKHR"); // MELONPRIME-PC-ADAPT
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    loadInstance(instance, vkCreateWin32SurfaceKHR, "vkCreateWin32SurfaceKHR"); // MELONPRIME-PC-ADAPT
#endif
    loadInstance(instance, vkCreateBuffer, "vkCreateBuffer");
    loadInstance(instance, vkCreateCommandPool, "vkCreateCommandPool");
    loadInstance(instance, vkCreateComputePipelines, "vkCreateComputePipelines");
    loadInstance(instance, vkCreateDebugUtilsMessengerEXT, "vkCreateDebugUtilsMessengerEXT");
    loadInstance(instance, vkCreateDescriptorPool, "vkCreateDescriptorPool");
    loadInstance(instance, vkCreateDescriptorSetLayout, "vkCreateDescriptorSetLayout");
    loadInstance(instance, vkCreateDevice, "vkCreateDevice");
    loadInstance(instance, vkCreateFence, "vkCreateFence");
    loadInstance(instance, vkCreateFramebuffer, "vkCreateFramebuffer");
    loadInstance(instance, vkCreateGraphicsPipelines, "vkCreateGraphicsPipelines");
    loadInstance(instance, vkCreateImage, "vkCreateImage");
    loadInstance(instance, vkCreateImageView, "vkCreateImageView");
    loadInstance(instance, vkCreateInstance, "vkCreateInstance");
    loadInstance(instance, vkCreatePipelineCache, "vkCreatePipelineCache");
    loadInstance(instance, vkCreatePipelineLayout, "vkCreatePipelineLayout");
    loadInstance(instance, vkCreateQueryPool, "vkCreateQueryPool");
    loadInstance(instance, vkCreateRenderPass, "vkCreateRenderPass");
    loadInstance(instance, vkCreateSampler, "vkCreateSampler");
    loadInstance(instance, vkCreateSemaphore, "vkCreateSemaphore");
    loadInstance(instance, vkCreateShaderModule, "vkCreateShaderModule");
    loadInstance(instance, vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
    loadInstance(instance, vkDestroyBuffer, "vkDestroyBuffer");
    loadInstance(instance, vkDestroyCommandPool, "vkDestroyCommandPool");
    loadInstance(instance, vkDestroyDebugUtilsMessengerEXT, "vkDestroyDebugUtilsMessengerEXT");
    loadInstance(instance, vkDestroyDescriptorPool, "vkDestroyDescriptorPool");
    loadInstance(instance, vkDestroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
    loadInstance(instance, vkDestroyDevice, "vkDestroyDevice");
    loadInstance(instance, vkDestroyFence, "vkDestroyFence");
    loadInstance(instance, vkDestroyFramebuffer, "vkDestroyFramebuffer");
    loadInstance(instance, vkDestroyImage, "vkDestroyImage");
    loadInstance(instance, vkDestroyImageView, "vkDestroyImageView");
    loadInstance(instance, vkDestroyInstance, "vkDestroyInstance");
    loadInstance(instance, vkDestroyPipeline, "vkDestroyPipeline");
    loadInstance(instance, vkDestroyPipelineCache, "vkDestroyPipelineCache");
    loadInstance(instance, vkDestroyPipelineLayout, "vkDestroyPipelineLayout");
    loadInstance(instance, vkDestroyQueryPool, "vkDestroyQueryPool");
    loadInstance(instance, vkDestroyRenderPass, "vkDestroyRenderPass");
    loadInstance(instance, vkDestroySampler, "vkDestroySampler");
    loadInstance(instance, vkDestroySemaphore, "vkDestroySemaphore");
    loadInstance(instance, vkDestroyShaderModule, "vkDestroyShaderModule");
    loadInstance(instance, vkDestroySurfaceKHR, "vkDestroySurfaceKHR");
    loadInstance(instance, vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
    loadInstance(instance, vkDeviceWaitIdle, "vkDeviceWaitIdle");
    loadInstance(instance, vkEndCommandBuffer, "vkEndCommandBuffer");
    loadInstance(instance, vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
    loadInstance(instance, vkEnumerateInstanceExtensionProperties, "vkEnumerateInstanceExtensionProperties");
    loadInstance(instance, vkEnumerateInstanceLayerProperties, "vkEnumerateInstanceLayerProperties");
    loadInstance(instance, vkEnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    loadInstance(instance, vkFreeCommandBuffers, "vkFreeCommandBuffers");
    loadInstance(instance, vkFreeDescriptorSets, "vkFreeDescriptorSets");
    loadInstance(instance, vkFreeMemory, "vkFreeMemory");
#ifdef __ANDROID__
    loadInstance(instance, vkGetAndroidHardwareBufferPropertiesANDROID, "vkGetAndroidHardwareBufferPropertiesANDROID"); // MELONPRIME-PC-ADAPT
#endif
    loadInstance(instance, vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    loadInstance(instance, vkGetDeviceProcAddr, "vkGetDeviceProcAddr");
    loadInstance(instance, vkGetDeviceQueue, "vkGetDeviceQueue");
    loadInstance(instance, vkGetFenceStatus, "vkGetFenceStatus");
    loadInstance(instance, vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
    loadInstance(instance, vkGetPhysicalDeviceFeatures, "vkGetPhysicalDeviceFeatures");
    loadInstance(instance, vkGetPhysicalDeviceFeatures2, "vkGetPhysicalDeviceFeatures2");
    loadInstance(instance, vkGetPhysicalDeviceFeatures2KHR, "vkGetPhysicalDeviceFeatures2KHR");
    loadInstance(instance, vkGetPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
    loadInstance(instance, vkGetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    loadInstance(instance, vkGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    loadInstance(instance, vkGetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    loadInstance(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    loadInstance(instance, vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    loadInstance(instance, vkGetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    loadInstance(instance, vkGetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
    loadInstance(instance, vkGetPipelineCacheData, "vkGetPipelineCacheData");
    loadInstance(instance, vkGetQueryPoolResults, "vkGetQueryPoolResults");
    loadInstance(instance, vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValue");
    loadInstance(instance, vkGetSemaphoreCounterValueKHR, "vkGetSemaphoreCounterValueKHR");
    loadInstance(instance, vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
    loadInstance(instance, vkMapMemory, "vkMapMemory");
    loadInstance(instance, vkQueuePresentKHR, "vkQueuePresentKHR");
    loadInstance(instance, vkQueueSubmit, "vkQueueSubmit");
    loadInstance(instance, vkQueueWaitIdle, "vkQueueWaitIdle");
    loadInstance(instance, vkResetCommandBuffer, "vkResetCommandBuffer");
    loadInstance(instance, vkResetFences, "vkResetFences");
    loadInstance(instance, vkResetQueryPool, "vkResetQueryPool");
    loadInstance(instance, vkResetQueryPoolEXT, "vkResetQueryPoolEXT");
    loadInstance(instance, vkUnmapMemory, "vkUnmapMemory");
    loadInstance(instance, vkUpdateDescriptorSets, "vkUpdateDescriptorSets");
    loadInstance(instance, vkWaitForFences, "vkWaitForFences");
    loadInstance(instance, vkWaitSemaphores, "vkWaitSemaphores");
    loadInstance(instance, vkWaitSemaphoresKHR, "vkWaitSemaphoresKHR");
}

void loadDeviceSymbols(VkDevice device)
{
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
        if (resolved != nullptr)
            vkAcquireNextImageKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkAllocateCommandBuffers>(vkGetDeviceProcAddr(device, "vkAllocateCommandBuffers"));
        if (resolved != nullptr)
            vkAllocateCommandBuffers = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkAllocateDescriptorSets>(vkGetDeviceProcAddr(device, "vkAllocateDescriptorSets"));
        if (resolved != nullptr)
            vkAllocateDescriptorSets = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkAllocateMemory>(vkGetDeviceProcAddr(device, "vkAllocateMemory"));
        if (resolved != nullptr)
            vkAllocateMemory = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkBeginCommandBuffer>(vkGetDeviceProcAddr(device, "vkBeginCommandBuffer"));
        if (resolved != nullptr)
            vkBeginCommandBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkBindBufferMemory>(vkGetDeviceProcAddr(device, "vkBindBufferMemory"));
        if (resolved != nullptr)
            vkBindBufferMemory = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkBindImageMemory>(vkGetDeviceProcAddr(device, "vkBindImageMemory"));
        if (resolved != nullptr)
            vkBindImageMemory = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBeginRenderPass>(vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass"));
        if (resolved != nullptr)
            vkCmdBeginRenderPass = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(device, "vkCmdBindDescriptorSets"));
        if (resolved != nullptr)
            vkCmdBindDescriptorSets = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBindIndexBuffer>(vkGetDeviceProcAddr(device, "vkCmdBindIndexBuffer"));
        if (resolved != nullptr)
            vkCmdBindIndexBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(device, "vkCmdBindPipeline"));
        if (resolved != nullptr)
            vkCmdBindPipeline = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBindVertexBuffers>(vkGetDeviceProcAddr(device, "vkCmdBindVertexBuffers"));
        if (resolved != nullptr)
            vkCmdBindVertexBuffers = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdBlitImage>(vkGetDeviceProcAddr(device, "vkCmdBlitImage"));
        if (resolved != nullptr)
            vkCmdBlitImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdClearColorImage>(vkGetDeviceProcAddr(device, "vkCmdClearColorImage"));
        if (resolved != nullptr)
            vkCmdClearColorImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdCopyBuffer>(vkGetDeviceProcAddr(device, "vkCmdCopyBuffer"));
        if (resolved != nullptr)
            vkCmdCopyBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(vkGetDeviceProcAddr(device, "vkCmdCopyBufferToImage"));
        if (resolved != nullptr)
            vkCmdCopyBufferToImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdCopyImage>(vkGetDeviceProcAddr(device, "vkCmdCopyImage"));
        if (resolved != nullptr)
            vkCmdCopyImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(vkGetDeviceProcAddr(device, "vkCmdCopyImageToBuffer"));
        if (resolved != nullptr)
            vkCmdCopyImageToBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdDispatch>(vkGetDeviceProcAddr(device, "vkCmdDispatch"));
        if (resolved != nullptr)
            vkCmdDispatch = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdDispatchIndirect>(vkGetDeviceProcAddr(device, "vkCmdDispatchIndirect"));
        if (resolved != nullptr)
            vkCmdDispatchIndirect = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdDraw>(vkGetDeviceProcAddr(device, "vkCmdDraw"));
        if (resolved != nullptr)
            vkCmdDraw = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdDrawIndexed>(vkGetDeviceProcAddr(device, "vkCmdDrawIndexed"));
        if (resolved != nullptr)
            vkCmdDrawIndexed = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdEndRenderPass>(vkGetDeviceProcAddr(device, "vkCmdEndRenderPass"));
        if (resolved != nullptr)
            vkCmdEndRenderPass = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdFillBuffer>(vkGetDeviceProcAddr(device, "vkCmdFillBuffer"));
        if (resolved != nullptr)
            vkCmdFillBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier"));
        if (resolved != nullptr)
            vkCmdPipelineBarrier = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdPushConstants>(vkGetDeviceProcAddr(device, "vkCmdPushConstants"));
        if (resolved != nullptr)
            vkCmdPushConstants = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetBlendConstants>(vkGetDeviceProcAddr(device, "vkCmdSetBlendConstants"));
        if (resolved != nullptr)
            vkCmdSetBlendConstants = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetScissor>(vkGetDeviceProcAddr(device, "vkCmdSetScissor"));
        if (resolved != nullptr)
            vkCmdSetScissor = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetStencilCompareMask>(vkGetDeviceProcAddr(device, "vkCmdSetStencilCompareMask"));
        if (resolved != nullptr)
            vkCmdSetStencilCompareMask = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetStencilReference>(vkGetDeviceProcAddr(device, "vkCmdSetStencilReference"));
        if (resolved != nullptr)
            vkCmdSetStencilReference = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetStencilWriteMask>(vkGetDeviceProcAddr(device, "vkCmdSetStencilWriteMask"));
        if (resolved != nullptr)
            vkCmdSetStencilWriteMask = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdSetViewport>(vkGetDeviceProcAddr(device, "vkCmdSetViewport"));
        if (resolved != nullptr)
            vkCmdSetViewport = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCmdWriteTimestamp>(vkGetDeviceProcAddr(device, "vkCmdWriteTimestamp"));
        if (resolved != nullptr)
            vkCmdWriteTimestamp = resolved;
    }
#ifdef __ANDROID__
    // MELONPRIME-PC-ADAPT
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(vkGetDeviceProcAddr(device, "vkCreateAndroidSurfaceKHR"));
        if (resolved != nullptr)
            vkCreateAndroidSurfaceKHR = resolved;
    }
#endif
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateBuffer>(vkGetDeviceProcAddr(device, "vkCreateBuffer"));
        if (resolved != nullptr)
            vkCreateBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateCommandPool>(vkGetDeviceProcAddr(device, "vkCreateCommandPool"));
        if (resolved != nullptr)
            vkCreateCommandPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateComputePipelines>(vkGetDeviceProcAddr(device, "vkCreateComputePipelines"));
        if (resolved != nullptr)
            vkCreateComputePipelines = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetDeviceProcAddr(device, "vkCreateDebugUtilsMessengerEXT"));
        if (resolved != nullptr)
            vkCreateDebugUtilsMessengerEXT = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateDescriptorPool>(vkGetDeviceProcAddr(device, "vkCreateDescriptorPool"));
        if (resolved != nullptr)
            vkCreateDescriptorPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(vkGetDeviceProcAddr(device, "vkCreateDescriptorSetLayout"));
        if (resolved != nullptr)
            vkCreateDescriptorSetLayout = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateDevice>(vkGetDeviceProcAddr(device, "vkCreateDevice"));
        if (resolved != nullptr)
            vkCreateDevice = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateFence>(vkGetDeviceProcAddr(device, "vkCreateFence"));
        if (resolved != nullptr)
            vkCreateFence = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateFramebuffer>(vkGetDeviceProcAddr(device, "vkCreateFramebuffer"));
        if (resolved != nullptr)
            vkCreateFramebuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(vkGetDeviceProcAddr(device, "vkCreateGraphicsPipelines"));
        if (resolved != nullptr)
            vkCreateGraphicsPipelines = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateImage>(vkGetDeviceProcAddr(device, "vkCreateImage"));
        if (resolved != nullptr)
            vkCreateImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateImageView>(vkGetDeviceProcAddr(device, "vkCreateImageView"));
        if (resolved != nullptr)
            vkCreateImageView = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateInstance>(vkGetDeviceProcAddr(device, "vkCreateInstance"));
        if (resolved != nullptr)
            vkCreateInstance = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreatePipelineCache>(vkGetDeviceProcAddr(device, "vkCreatePipelineCache"));
        if (resolved != nullptr)
            vkCreatePipelineCache = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreatePipelineLayout>(vkGetDeviceProcAddr(device, "vkCreatePipelineLayout"));
        if (resolved != nullptr)
            vkCreatePipelineLayout = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateQueryPool>(vkGetDeviceProcAddr(device, "vkCreateQueryPool"));
        if (resolved != nullptr)
            vkCreateQueryPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateRenderPass>(vkGetDeviceProcAddr(device, "vkCreateRenderPass"));
        if (resolved != nullptr)
            vkCreateRenderPass = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateSampler>(vkGetDeviceProcAddr(device, "vkCreateSampler"));
        if (resolved != nullptr)
            vkCreateSampler = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateSemaphore>(vkGetDeviceProcAddr(device, "vkCreateSemaphore"));
        if (resolved != nullptr)
            vkCreateSemaphore = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateShaderModule>(vkGetDeviceProcAddr(device, "vkCreateShaderModule"));
        if (resolved != nullptr)
            vkCreateShaderModule = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
        if (resolved != nullptr)
            vkCreateSwapchainKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyBuffer>(vkGetDeviceProcAddr(device, "vkDestroyBuffer"));
        if (resolved != nullptr)
            vkDestroyBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyCommandPool>(vkGetDeviceProcAddr(device, "vkDestroyCommandPool"));
        if (resolved != nullptr)
            vkDestroyCommandPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetDeviceProcAddr(device, "vkDestroyDebugUtilsMessengerEXT"));
        if (resolved != nullptr)
            vkDestroyDebugUtilsMessengerEXT = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyDescriptorPool>(vkGetDeviceProcAddr(device, "vkDestroyDescriptorPool"));
        if (resolved != nullptr)
            vkDestroyDescriptorPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(vkGetDeviceProcAddr(device, "vkDestroyDescriptorSetLayout"));
        if (resolved != nullptr)
            vkDestroyDescriptorSetLayout = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(device, "vkDestroyDevice"));
        if (resolved != nullptr)
            vkDestroyDevice = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyFence>(vkGetDeviceProcAddr(device, "vkDestroyFence"));
        if (resolved != nullptr)
            vkDestroyFence = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyFramebuffer>(vkGetDeviceProcAddr(device, "vkDestroyFramebuffer"));
        if (resolved != nullptr)
            vkDestroyFramebuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyImage>(vkGetDeviceProcAddr(device, "vkDestroyImage"));
        if (resolved != nullptr)
            vkDestroyImage = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyImageView>(vkGetDeviceProcAddr(device, "vkDestroyImageView"));
        if (resolved != nullptr)
            vkDestroyImageView = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyInstance>(vkGetDeviceProcAddr(device, "vkDestroyInstance"));
        if (resolved != nullptr)
            vkDestroyInstance = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyPipeline>(vkGetDeviceProcAddr(device, "vkDestroyPipeline"));
        if (resolved != nullptr)
            vkDestroyPipeline = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyPipelineCache>(vkGetDeviceProcAddr(device, "vkDestroyPipelineCache"));
        if (resolved != nullptr)
            vkDestroyPipelineCache = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyPipelineLayout>(vkGetDeviceProcAddr(device, "vkDestroyPipelineLayout"));
        if (resolved != nullptr)
            vkDestroyPipelineLayout = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyQueryPool>(vkGetDeviceProcAddr(device, "vkDestroyQueryPool"));
        if (resolved != nullptr)
            vkDestroyQueryPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyRenderPass>(vkGetDeviceProcAddr(device, "vkDestroyRenderPass"));
        if (resolved != nullptr)
            vkDestroyRenderPass = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroySampler>(vkGetDeviceProcAddr(device, "vkDestroySampler"));
        if (resolved != nullptr)
            vkDestroySampler = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroySemaphore>(vkGetDeviceProcAddr(device, "vkDestroySemaphore"));
        if (resolved != nullptr)
            vkDestroySemaphore = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroyShaderModule>(vkGetDeviceProcAddr(device, "vkDestroyShaderModule"));
        if (resolved != nullptr)
            vkDestroyShaderModule = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroySurfaceKHR>(vkGetDeviceProcAddr(device, "vkDestroySurfaceKHR"));
        if (resolved != nullptr)
            vkDestroySurfaceKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
        if (resolved != nullptr)
            vkDestroySwapchainKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkDeviceWaitIdle>(vkGetDeviceProcAddr(device, "vkDeviceWaitIdle"));
        if (resolved != nullptr)
            vkDeviceWaitIdle = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkEndCommandBuffer>(vkGetDeviceProcAddr(device, "vkEndCommandBuffer"));
        if (resolved != nullptr)
            vkEndCommandBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(vkGetDeviceProcAddr(device, "vkEnumerateDeviceExtensionProperties"));
        if (resolved != nullptr)
            vkEnumerateDeviceExtensionProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(vkGetDeviceProcAddr(device, "vkEnumerateInstanceExtensionProperties"));
        if (resolved != nullptr)
            vkEnumerateInstanceExtensionProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(vkGetDeviceProcAddr(device, "vkEnumerateInstanceLayerProperties"));
        if (resolved != nullptr)
            vkEnumerateInstanceLayerProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(vkGetDeviceProcAddr(device, "vkEnumeratePhysicalDevices"));
        if (resolved != nullptr)
            vkEnumeratePhysicalDevices = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkFreeCommandBuffers>(vkGetDeviceProcAddr(device, "vkFreeCommandBuffers"));
        if (resolved != nullptr)
            vkFreeCommandBuffers = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkFreeDescriptorSets>(vkGetDeviceProcAddr(device, "vkFreeDescriptorSets"));
        if (resolved != nullptr)
            vkFreeDescriptorSets = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkFreeMemory>(vkGetDeviceProcAddr(device, "vkFreeMemory"));
        if (resolved != nullptr)
            vkFreeMemory = resolved;
    }
#ifdef __ANDROID__
    // MELONPRIME-PC-ADAPT
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID"));
        if (resolved != nullptr)
            vkGetAndroidHardwareBufferPropertiesANDROID = resolved;
    }
#endif
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(vkGetDeviceProcAddr(device, "vkGetBufferMemoryRequirements"));
        if (resolved != nullptr)
            vkGetBufferMemoryRequirements = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr(device, "vkGetDeviceQueue"));
        if (resolved != nullptr)
            vkGetDeviceQueue = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetFenceStatus>(vkGetDeviceProcAddr(device, "vkGetFenceStatus"));
        if (resolved != nullptr)
            vkGetFenceStatus = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(vkGetDeviceProcAddr(device, "vkGetImageMemoryRequirements"));
        if (resolved != nullptr)
            vkGetImageMemoryRequirements = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceFeatures"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceFeatures = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceFeatures2"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceFeatures2 = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceFeatures2KHR"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceFeatures2KHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceFormatProperties"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceFormatProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceMemoryProperties"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceMemoryProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceProperties"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceQueueFamilyProperties"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceQueueFamilyProperties = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceSurfaceFormatsKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceSurfacePresentModesKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(vkGetDeviceProcAddr(device, "vkGetPhysicalDeviceSurfaceSupportKHR"));
        if (resolved != nullptr)
            vkGetPhysicalDeviceSurfaceSupportKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetPipelineCacheData>(vkGetDeviceProcAddr(device, "vkGetPipelineCacheData"));
        if (resolved != nullptr)
            vkGetPipelineCacheData = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetQueryPoolResults>(vkGetDeviceProcAddr(device, "vkGetQueryPoolResults"));
        if (resolved != nullptr)
            vkGetQueryPoolResults = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValue"));
        if (resolved != nullptr)
            vkGetSemaphoreCounterValue = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValueKHR"));
        if (resolved != nullptr)
            vkGetSemaphoreCounterValueKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
        if (resolved != nullptr)
            vkGetSwapchainImagesKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkMapMemory>(vkGetDeviceProcAddr(device, "vkMapMemory"));
        if (resolved != nullptr)
            vkMapMemory = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkQueuePresentKHR>(vkGetDeviceProcAddr(device, "vkQueuePresentKHR"));
        if (resolved != nullptr)
            vkQueuePresentKHR = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkQueueSubmit>(vkGetDeviceProcAddr(device, "vkQueueSubmit"));
        if (resolved != nullptr)
            vkQueueSubmit = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkQueueWaitIdle>(vkGetDeviceProcAddr(device, "vkQueueWaitIdle"));
        if (resolved != nullptr)
            vkQueueWaitIdle = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkResetCommandBuffer>(vkGetDeviceProcAddr(device, "vkResetCommandBuffer"));
        if (resolved != nullptr)
            vkResetCommandBuffer = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkResetFences>(vkGetDeviceProcAddr(device, "vkResetFences"));
        if (resolved != nullptr)
            vkResetFences = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkResetQueryPool>(vkGetDeviceProcAddr(device, "vkResetQueryPool"));
        if (resolved != nullptr)
            vkResetQueryPool = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkResetQueryPoolEXT>(vkGetDeviceProcAddr(device, "vkResetQueryPoolEXT"));
        if (resolved != nullptr)
            vkResetQueryPoolEXT = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkUnmapMemory>(vkGetDeviceProcAddr(device, "vkUnmapMemory"));
        if (resolved != nullptr)
            vkUnmapMemory = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkUpdateDescriptorSets>(vkGetDeviceProcAddr(device, "vkUpdateDescriptorSets"));
        if (resolved != nullptr)
            vkUpdateDescriptorSets = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkWaitForFences>(vkGetDeviceProcAddr(device, "vkWaitForFences"));
        if (resolved != nullptr)
            vkWaitForFences = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkWaitSemaphores>(vkGetDeviceProcAddr(device, "vkWaitSemaphores"));
        if (resolved != nullptr)
            vkWaitSemaphores = resolved;
    }
    if (vkGetDeviceProcAddr != nullptr) {
        auto resolved = reinterpret_cast<PFN_vkWaitSemaphoresKHR>(vkGetDeviceProcAddr(device, "vkWaitSemaphoresKHR"));
        if (resolved != nullptr)
            vkWaitSemaphoresKHR = resolved;
    }
}

}

void ConfigureDriver(const DriverConfiguration& configuration)
{
    std::scoped_lock guard(gLock);
    if (gInitialized && !sameConfiguration(gConfiguration, configuration))
    {
        if (gVulkanHandle != nullptr)
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(gVulkanHandle)); // MELONPRIME-PC-ADAPT: mirror dlclose() via FreeLibrary()
#else
            dlclose(gVulkanHandle);
#endif
        gVulkanHandle = nullptr;
        gInitialized = false;
        gUsingCustomDriver = false;
        vkGetInstanceProcAddr = nullptr;
        vkGetDeviceProcAddr = nullptr;
        Platform::Log(
            Platform::LogLevel::Warn,
            "VulkanDriver: configuration changed; Vulkan loader will reopen on next use"
        );
    }
    gConfiguration = configuration;
}

bool Initialize()
{
    std::scoped_lock guard(gLock);
    if (gInitialized)
        return vkGetInstanceProcAddr != nullptr;

    const bool requestedCustomDriver = gConfiguration.UseCustomDriver
        && !gConfiguration.CustomDriverDir.empty()
        && !gConfiguration.CustomDriverName.empty()
        && !gConfiguration.HookLibDir.empty();

#if MELONDS_HAS_ADRENOTOOLS && defined(__ANDROID__)
    // MELONPRIME-PC-ADAPT: adrenotools custom-driver path is Android-only
    if (requestedCustomDriver)
    {
        gVulkanHandle = adrenotools_open_libvulkan(
            RTLD_NOW | RTLD_LOCAL,
            ADRENOTOOLS_DRIVER_CUSTOM,
            gConfiguration.TmpLibDir.empty() ? nullptr : gConfiguration.TmpLibDir.c_str(),
            gConfiguration.HookLibDir.c_str(),
            gConfiguration.CustomDriverDir.c_str(),
            gConfiguration.CustomDriverName.c_str(),
            nullptr,
            nullptr
        );
        if (gVulkanHandle != nullptr)
        {
            gUsingCustomDriver = true;
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanDriver: source=custom driver=%s result=opened",
                gConfiguration.DisplayName.empty() ? gConfiguration.CustomDriverName.c_str() : gConfiguration.DisplayName.c_str()
            );
        }
        else
        {
            const char* error = dlerror();
            Platform::Log(
                Platform::LogLevel::Error,
                "VulkanDriver: custom driver load failed driver=%s error=%s; falling back to system",
                gConfiguration.CustomDriverName.c_str(),
                error != nullptr ? error : "unknown"
            );
        }
    }
#else
    if (requestedCustomDriver)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "VulkanDriver: custom driver requested but adrenotools is not available in this build/ABI; falling back to system"
        );
    }
#endif

    if (gVulkanHandle == nullptr)
    {
#ifdef _WIN32
        // MELONPRIME-PC-ADAPT: mirror dlopen("libvulkan.so") via LoadLibraryA("vulkan-1.dll")
        gVulkanHandle = reinterpret_cast<void*>(LoadLibraryA("vulkan-1.dll"));
#else
        gVulkanHandle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
        gUsingCustomDriver = false;
        if (gVulkanHandle != nullptr)
            Platform::Log(Platform::LogLevel::Warn, "VulkanDriver: source=system");
    }

    if (gVulkanHandle == nullptr)
    {
#ifdef _WIN32
        // MELONPRIME-PC-ADAPT: dlerror() has no Windows equivalent; report GetLastError() instead
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanDriver: failed to open vulkan-1.dll error=%lu",
            static_cast<unsigned long>(GetLastError())
        );
#else
        const char* error = dlerror();
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanDriver: failed to open libvulkan.so error=%s",
            error != nullptr ? error : "unknown"
        );
#endif
        return false;
    }

    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(loadSymbol("vkGetInstanceProcAddr"));
    if (vkGetInstanceProcAddr == nullptr)
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanDriver: vkGetInstanceProcAddr not found");
        return false;
    }

    loadGlobalSymbols();
    gInitialized = true;
    Platform::Log(
        Platform::LogLevel::Warn,
        "VulkanDriver: active=%s",
        gUsingCustomDriver ? "custom" : "system"
    );
    return true;
}

void LoadInstance(VkInstance instance)
{
    std::scoped_lock guard(gLock);
    if (!gInitialized || instance == VK_NULL_HANDLE)
        return;
    loadInstanceSymbols(instance);
}

void LoadDevice(VkDevice device)
{
    std::scoped_lock guard(gLock);
    if (!gInitialized || device == VK_NULL_HANDLE)
        return;
    loadDeviceSymbols(device);
}

}
