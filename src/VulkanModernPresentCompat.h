/*
    Copyright 2016-2026 melonDS team

    Compatibility declarations for optional Vulkan presentation extensions
    newer than the minimum SDK used by melonPrimeDS. Values and layouts are
    copied from Khronos Vulkan-Headers generated vulkan_core.h, header version
    359 (2026), under Apache-2.0 OR MIT. Every block disappears automatically
    when the build SDK already provides the corresponding extension.
*/

#ifndef VULKAN_MODERN_PRESENT_COMPAT_H
#define VULKAN_MODERN_PRESENT_COMPAT_H

#ifndef VK_KHR_present_id2
#define VK_KHR_present_id2 1
#define VK_KHR_PRESENT_ID_2_SPEC_VERSION 1
#define VK_KHR_PRESENT_ID_2_EXTENSION_NAME "VK_KHR_present_id2"
#define VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR ((VkStructureType)1000479000)
#define VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR ((VkStructureType)1000479001)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR ((VkStructureType)1000479002)
#define VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR ((VkSwapchainCreateFlagBitsKHR)0x00000040)

typedef struct VkSurfaceCapabilitiesPresentId2KHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentId2Supported;
} VkSurfaceCapabilitiesPresentId2KHR;

typedef struct VkPresentId2KHR {
    VkStructureType sType;
    const void* pNext;
    uint32_t swapchainCount;
    const uint64_t* pPresentIds;
} VkPresentId2KHR;

typedef struct VkPhysicalDevicePresentId2FeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentId2;
} VkPhysicalDevicePresentId2FeaturesKHR;
#endif

#ifndef VK_KHR_present_wait2
#define VK_KHR_present_wait2 1
#define VK_KHR_PRESENT_WAIT_2_SPEC_VERSION 1
#define VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME "VK_KHR_present_wait2"
#define VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR ((VkStructureType)1000480000)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR ((VkStructureType)1000480001)
#define VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR ((VkStructureType)1000480002)
#define VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR ((VkSwapchainCreateFlagBitsKHR)0x00000080)

typedef struct VkSurfaceCapabilitiesPresentWait2KHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentWait2Supported;
} VkSurfaceCapabilitiesPresentWait2KHR;

typedef struct VkPhysicalDevicePresentWait2FeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentWait2;
} VkPhysicalDevicePresentWait2FeaturesKHR;

typedef struct VkPresentWait2InfoKHR {
    VkStructureType sType;
    const void* pNext;
    uint64_t presentId;
    uint64_t timeout;
} VkPresentWait2InfoKHR;

typedef VkResult (VKAPI_PTR *PFN_vkWaitForPresent2KHR)(
    VkDevice device, VkSwapchainKHR swapchain, const VkPresentWait2InfoKHR* pPresentWait2Info);
#endif

#ifndef VK_KHR_present_mode_fifo_latest_ready
#define VK_KHR_present_mode_fifo_latest_ready 1
#define VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_SPEC_VERSION 1
#define VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME "VK_KHR_present_mode_fifo_latest_ready"
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR ((VkStructureType)1000361000)
#define VK_PRESENT_MODE_FIFO_LATEST_READY_KHR ((VkPresentModeKHR)1000361000)

typedef struct VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentModeFifoLatestReady;
} VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR;
#endif

#ifndef VK_EXT_present_timing
#define VK_EXT_present_timing 1
#define VK_EXT_PRESENT_TIMING_SPEC_VERSION 3
#define VK_EXT_PRESENT_TIMING_EXTENSION_NAME "VK_EXT_present_timing"
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT ((VkStructureType)1000208000)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT ((VkStructureType)1000208001)
#define VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT ((VkStructureType)1000208002)
#define VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT ((VkStructureType)1000208003)
#define VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT ((VkStructureType)1000208004)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT ((VkStructureType)1000208005)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT ((VkStructureType)1000208006)
#define VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT ((VkStructureType)1000208007)
#define VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT ((VkStructureType)1000208008)
#define VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT ((VkSwapchainCreateFlagBitsKHR)0x00000200)
#define VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT ((VkResult)-1000208000)

typedef VkFlags VkPresentStageFlagsEXT;
typedef VkFlags VkPastPresentationTimingFlagsEXT;
typedef VkFlags VkPresentTimingInfoFlagsEXT;

#define VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT ((VkPresentStageFlagsEXT)0x00000001)
#define VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT ((VkPresentStageFlagsEXT)0x00000002)
#define VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT ((VkPresentStageFlagsEXT)0x00000004)
#define VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT ((VkPresentStageFlagsEXT)0x00000008)
#define VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT ((VkPastPresentationTimingFlagsEXT)0x00000001)
#define VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT ((VkPresentTimingInfoFlagsEXT)0x00000001)
#define VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT ((VkPresentTimingInfoFlagsEXT)0x00000002)

typedef struct VkPhysicalDevicePresentTimingFeaturesEXT {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentTiming;
    VkBool32 presentAtAbsoluteTime;
    VkBool32 presentAtRelativeTime;
} VkPhysicalDevicePresentTimingFeaturesEXT;

typedef struct VkPresentTimingSurfaceCapabilitiesEXT {
    VkStructureType sType;
    void* pNext;
    VkBool32 presentTimingSupported;
    VkBool32 presentAtAbsoluteTimeSupported;
    VkBool32 presentAtRelativeTimeSupported;
    VkPresentStageFlagsEXT presentStageQueries;
} VkPresentTimingSurfaceCapabilitiesEXT;

typedef struct VkSwapchainTimingPropertiesEXT {
    VkStructureType sType;
    void* pNext;
    uint64_t refreshDuration;
    uint64_t refreshInterval;
} VkSwapchainTimingPropertiesEXT;

typedef struct VkSwapchainTimeDomainPropertiesEXT {
    VkStructureType sType;
    void* pNext;
    uint32_t timeDomainCount;
    VkTimeDomainKHR* pTimeDomains;
    uint64_t* pTimeDomainIds;
} VkSwapchainTimeDomainPropertiesEXT;

typedef struct VkPastPresentationTimingInfoEXT {
    VkStructureType sType;
    const void* pNext;
    VkPastPresentationTimingFlagsEXT flags;
    VkSwapchainKHR swapchain;
} VkPastPresentationTimingInfoEXT;

typedef struct VkPresentStageTimeEXT {
    VkPresentStageFlagsEXT stage;
    uint64_t time;
} VkPresentStageTimeEXT;

typedef struct VkPastPresentationTimingEXT {
    VkStructureType sType;
    void* pNext;
    uint64_t presentId;
    uint64_t targetTime;
    uint32_t presentStageCount;
    VkPresentStageTimeEXT* pPresentStages;
    VkTimeDomainKHR timeDomain;
    uint64_t timeDomainId;
    VkBool32 reportComplete;
} VkPastPresentationTimingEXT;

typedef struct VkPastPresentationTimingPropertiesEXT {
    VkStructureType sType;
    void* pNext;
    uint64_t timingPropertiesCounter;
    uint64_t timeDomainsCounter;
    uint32_t presentationTimingCount;
    VkPastPresentationTimingEXT* pPresentationTimings;
} VkPastPresentationTimingPropertiesEXT;

typedef struct VkPresentTimingInfoEXT {
    VkStructureType sType;
    const void* pNext;
    VkPresentTimingInfoFlagsEXT flags;
    uint64_t targetTime;
    uint64_t timeDomainId;
    VkPresentStageFlagsEXT presentStageQueries;
    VkPresentStageFlagsEXT targetTimeDomainPresentStage;
} VkPresentTimingInfoEXT;

typedef struct VkPresentTimingsInfoEXT {
    VkStructureType sType;
    const void* pNext;
    uint32_t swapchainCount;
    const VkPresentTimingInfoEXT* pTimingInfos;
} VkPresentTimingsInfoEXT;

typedef VkResult (VKAPI_PTR *PFN_vkSetSwapchainPresentTimingQueueSizeEXT)(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t size);
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainTimingPropertiesEXT)(
    VkDevice device, VkSwapchainKHR swapchain,
    VkSwapchainTimingPropertiesEXT* pSwapchainTimingProperties,
    uint64_t* pSwapchainTimingPropertiesCounter);
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainTimeDomainPropertiesEXT)(
    VkDevice device, VkSwapchainKHR swapchain,
    VkSwapchainTimeDomainPropertiesEXT* pSwapchainTimeDomainProperties,
    uint64_t* pTimeDomainsCounter);
typedef VkResult (VKAPI_PTR *PFN_vkGetPastPresentationTimingEXT)(
    VkDevice device, const VkPastPresentationTimingInfoEXT* pPastPresentationTimingInfo,
    VkPastPresentationTimingPropertiesEXT* pPastPresentationTimingProperties);
#endif

#endif // VULKAN_MODERN_PRESENT_COMPAT_H
