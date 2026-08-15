/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef VULKAN_LOADER_H
#define VULKAN_LOADER_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>
#include <vector>

#include "VulkanCommon.h"

// <windows.h> defines CreateSemaphore as a macro (CreateSemaphoreW under
// UNICODE, CreateSemaphoreA otherwise), and DeviceDispatch below has a member
// of exactly that name. A translation unit that pulled in windows.h first
// therefore compiled the member -- and every use of it -- under the expanded
// name, so DeviceDispatch had two different definitions in one binary. GCC's
// LTO -Wodr reports it: "the first difference of corresponding definitions is
// field 'CreateSemaphoreW'".
//
// Undefining it here, before the struct, gives every translation unit the same
// member name. Nothing in melonPrimeDS calls the Win32 CreateSemaphore; code
// that ever needs it can name CreateSemaphoreW/A explicitly.
#if defined(_WIN32) && defined(CreateSemaphore)  // scatter-budget-exempt: windows.h macro collision with a Vulkan entry-point name; not platform input dispatch
#undef CreateSemaphore
#endif

namespace melonDS::Vk
{

// Runtime loading of the Vulkan entry points.
//
// melonPrimeDS ships a single binary that must start on machines with no Vulkan
// runtime at all, so the loader is never linked: the shared library is opened
// by name and every entry point is resolved through the two dispatch functions
// the Vulkan spec guarantees (vkGetInstanceProcAddr / vkGetDeviceProcAddr).
//
// The spec defines three dispatch levels and they are kept as three separate
// structs on purpose:
//
//  * Global   - resolvable with a VK_NULL_HANDLE instance. Only five commands
//               qualify; anything else must not be looked up this way.
//  * Instance - resolved from a VkInstance. These are valid for any physical
//               device the instance enumerated, and they go through the
//               loader's trampoline.
//  * Device   - resolved from a VkDevice with vkGetDeviceProcAddr. This skips
//               the loader trampoline entirely, which matters: every per-frame
//               vkCmd* call in the rasterizer goes through this table.
//
// A device-level command resolved from the *instance* would still work, but it
// would pay dispatch overhead on every call, so the split is not cosmetic.

struct GlobalDispatch
{
    PFN_vkGetInstanceProcAddr                   GetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance                        CreateInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties  EnumerateInstanceExtensionProperties = nullptr;
    PFN_vkEnumerateInstanceLayerProperties      EnumerateInstanceLayerProperties = nullptr;

    // Added in Vulkan 1.1. A 1.0-only loader legitimately returns null here,
    // which is itself the answer: the runtime supports 1.0 only, and the
    // feature probe rejects it against the 1.1 baseline.
    PFN_vkEnumerateInstanceVersion              EnumerateInstanceVersion = nullptr;
};

struct InstanceDispatch
{
    // --- core 1.0 ---
    PFN_vkDestroyInstance                               DestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices                      EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties                   GetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures                     GetPhysicalDeviceFeatures = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties             GetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties        GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties             GetPhysicalDeviceFormatProperties = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties        GetPhysicalDeviceImageFormatProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties            EnumerateDeviceExtensionProperties = nullptr;
    PFN_vkCreateDevice                                  CreateDevice = nullptr;
    PFN_vkGetDeviceProcAddr                             GetDeviceProcAddr = nullptr;

    // --- core 1.1 ---
    // Required by the 1.1 baseline, so these are hard failures if absent.
    PFN_vkGetPhysicalDeviceProperties2                  GetPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2                    GetPhysicalDeviceFeatures2 = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2            GetPhysicalDeviceMemoryProperties2 = nullptr;

    // --- VK_KHR_surface (optional: the settings-dialog probe runs headless) ---
    PFN_vkDestroySurfaceKHR                             DestroySurfaceKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR            GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR       GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR            GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR       GetPhysicalDeviceSurfacePresentModesKHR = nullptr;

    // --- modern optional WSI capability/timestamp queries ---
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR GetPhysicalDeviceSurfaceCapabilities2KHR = nullptr;

    // --- platform WSI ---
    // Only the entry point for the platform block this translation unit was
    // compiled with exists; see the note in VulkanCommon.h about why the
    // Linux/macOS blocks are opted into by the frontend adapter rather than
    // forced on for core.
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    PFN_vkCreateWin32SurfaceKHR                         CreateWin32SurfaceKHR = nullptr;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    PFN_vkCreateXlibSurfaceKHR                          CreateXlibSurfaceKHR = nullptr;
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
    PFN_vkCreateXcbSurfaceKHR                           CreateXcbSurfaceKHR = nullptr;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    PFN_vkCreateWaylandSurfaceKHR                       CreateWaylandSurfaceKHR = nullptr;
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
    PFN_vkCreateMetalSurfaceEXT                         CreateMetalSurfaceEXT = nullptr;
#endif

    // --- VK_EXT_debug_utils (optional, developer builds) ---
    PFN_vkCreateDebugUtilsMessengerEXT                  CreateDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                 DestroyDebugUtilsMessengerEXT = nullptr;
};

struct DeviceDispatch
{
    // --- device / queue ---
    PFN_vkDestroyDevice                 DestroyDevice = nullptr;
    PFN_vkGetDeviceQueue                GetDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle                DeviceWaitIdle = nullptr;
    PFN_vkQueueSubmit                   QueueSubmit = nullptr;
    PFN_vkQueueWaitIdle                 QueueWaitIdle = nullptr;

    // --- memory ---
    PFN_vkAllocateMemory                AllocateMemory = nullptr;
    PFN_vkFreeMemory                    FreeMemory = nullptr;
    PFN_vkMapMemory                     MapMemory = nullptr;
    PFN_vkUnmapMemory                   UnmapMemory = nullptr;
    PFN_vkFlushMappedMemoryRanges       FlushMappedMemoryRanges = nullptr;
    PFN_vkInvalidateMappedMemoryRanges  InvalidateMappedMemoryRanges = nullptr;

    // --- buffers ---
    PFN_vkCreateBuffer                  CreateBuffer = nullptr;
    PFN_vkDestroyBuffer                 DestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements   GetBufferMemoryRequirements = nullptr;
    PFN_vkBindBufferMemory              BindBufferMemory = nullptr;
    PFN_vkCreateBufferView              CreateBufferView = nullptr;
    PFN_vkDestroyBufferView             DestroyBufferView = nullptr;

    // --- images ---
    PFN_vkCreateImage                   CreateImage = nullptr;
    PFN_vkDestroyImage                  DestroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements    GetImageMemoryRequirements = nullptr;
    PFN_vkBindImageMemory               BindImageMemory = nullptr;
    PFN_vkGetImageSubresourceLayout     GetImageSubresourceLayout = nullptr;
    PFN_vkCreateImageView               CreateImageView = nullptr;
    PFN_vkDestroyImageView              DestroyImageView = nullptr;
    PFN_vkCreateSampler                 CreateSampler = nullptr;
    PFN_vkDestroySampler                DestroySampler = nullptr;

    // --- descriptors ---
    PFN_vkCreateDescriptorSetLayout     CreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout    DestroyDescriptorSetLayout = nullptr;
    PFN_vkCreateDescriptorPool          CreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool         DestroyDescriptorPool = nullptr;
    PFN_vkResetDescriptorPool           ResetDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets        AllocateDescriptorSets = nullptr;
    PFN_vkFreeDescriptorSets            FreeDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets          UpdateDescriptorSets = nullptr;

    // --- pipelines ---
    PFN_vkCreatePipelineLayout          CreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout         DestroyPipelineLayout = nullptr;
    PFN_vkCreateShaderModule            CreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule           DestroyShaderModule = nullptr;
    PFN_vkCreatePipelineCache           CreatePipelineCache = nullptr;
    PFN_vkDestroyPipelineCache          DestroyPipelineCache = nullptr;
    PFN_vkGetPipelineCacheData          GetPipelineCacheData = nullptr;
    PFN_vkCreateComputePipelines        CreateComputePipelines = nullptr;
    PFN_vkCreateGraphicsPipelines       CreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline               DestroyPipeline = nullptr;

    // --- render passes (present/compositor path only) ---
    PFN_vkCreateRenderPass              CreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass             DestroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer             CreateFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer            DestroyFramebuffer = nullptr;

    // --- command buffers ---
    PFN_vkCreateCommandPool             CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool            DestroyCommandPool = nullptr;
    PFN_vkResetCommandPool              ResetCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers        AllocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers            FreeCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer            BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer              EndCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer            ResetCommandBuffer = nullptr;

    // --- synchronization (legacy only: no Synchronization2 path exists) ---
    PFN_vkCreateFence                   CreateFence = nullptr;
    PFN_vkDestroyFence                  DestroyFence = nullptr;
    PFN_vkResetFences                   ResetFences = nullptr;
    PFN_vkGetFenceStatus                GetFenceStatus = nullptr;
    PFN_vkWaitForFences                 WaitForFences = nullptr;
    PFN_vkCreateSemaphore               CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore              DestroySemaphore = nullptr;

    // --- queries (GPU timing for the perf overlay) ---
    PFN_vkCreateQueryPool               CreateQueryPool = nullptr;
    PFN_vkDestroyQueryPool              DestroyQueryPool = nullptr;
    PFN_vkGetQueryPoolResults           GetQueryPoolResults = nullptr;

    // --- commands ---
    PFN_vkCmdBindPipeline               CmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets         CmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants              CmdPushConstants = nullptr;
    PFN_vkCmdDispatch                   CmdDispatch = nullptr;
    PFN_vkCmdDispatchIndirect           CmdDispatchIndirect = nullptr;
    PFN_vkCmdPipelineBarrier            CmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBuffer                 CmdCopyBuffer = nullptr;
    PFN_vkCmdCopyBufferToImage          CmdCopyBufferToImage = nullptr;
    PFN_vkCmdCopyImageToBuffer          CmdCopyImageToBuffer = nullptr;
    PFN_vkCmdCopyImage                  CmdCopyImage = nullptr;
    PFN_vkCmdBlitImage                  CmdBlitImage = nullptr;
    PFN_vkCmdFillBuffer                 CmdFillBuffer = nullptr;
    PFN_vkCmdUpdateBuffer               CmdUpdateBuffer = nullptr;
    PFN_vkCmdClearColorImage            CmdClearColorImage = nullptr;
    PFN_vkCmdResetQueryPool             CmdResetQueryPool = nullptr;
    PFN_vkCmdWriteTimestamp             CmdWriteTimestamp = nullptr;
    PFN_vkCmdBeginRenderPass            CmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass              CmdEndRenderPass = nullptr;
    PFN_vkCmdSetViewport                CmdSetViewport = nullptr;
    PFN_vkCmdSetScissor                 CmdSetScissor = nullptr;
    PFN_vkCmdBindVertexBuffers          CmdBindVertexBuffers = nullptr;
    PFN_vkCmdBindIndexBuffer            CmdBindIndexBuffer = nullptr;
    PFN_vkCmdDraw                       CmdDraw = nullptr;
    PFN_vkCmdDrawIndexed                CmdDrawIndexed = nullptr;

    // --- VK_KHR_swapchain (optional: present only) ---
    PFN_vkCreateSwapchainKHR            CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR           DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR         GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR           AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR               QueuePresentKHR = nullptr;

    // --- vendor-neutral present pacing / telemetry (optional) ---
    PFN_vkWaitForPresentKHR                     WaitForPresentKHR = nullptr;
    PFN_vkWaitForPresent2KHR                    WaitForPresent2KHR = nullptr;
    PFN_vkGetCalibratedTimestampsKHR            GetCalibratedTimestampsKHR = nullptr;
    PFN_vkSetSwapchainPresentTimingQueueSizeEXT SetSwapchainPresentTimingQueueSizeEXT = nullptr;
    PFN_vkGetSwapchainTimingPropertiesEXT       GetSwapchainTimingPropertiesEXT = nullptr;
    PFN_vkGetSwapchainTimeDomainPropertiesEXT   GetSwapchainTimeDomainPropertiesEXT = nullptr;
    PFN_vkGetPastPresentationTimingEXT          GetPastPresentationTimingEXT = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE       GetPastPresentationTimingGOOGLE = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE         GetRefreshCycleDurationGOOGLE = nullptr;

    // --- VK_KHR_timeline_semaphore (optional: VK_NV_low_latency2 dependency) ---
    //
    // The instance is created with apiVersion 1.1, so the 1.2-core spellings of
    // these are not available even on a 1.3 device: the KHR extension is the
    // only way to get a timeline semaphore here, and VkLatencySleepInfoNV
    // requires one.
    // The timeline semaphore itself is created through the core CreateSemaphore
    // above with a VkSemaphoreTypeCreateInfoKHR in its pNext chain; only the
    // wait/query commands are extension entry points.
    PFN_vkWaitSemaphoresKHR             WaitSemaphoresKHR = nullptr;
    PFN_vkGetSemaphoreCounterValueKHR   GetSemaphoreCounterValueKHR = nullptr;

    // --- VK_NV_low_latency2 (optional: NVIDIA Reflex) ---
    PFN_vkSetLatencySleepModeNV         SetLatencySleepModeNV = nullptr;
    PFN_vkLatencySleepNV                LatencySleepNV = nullptr;
    PFN_vkSetLatencyMarkerNV            SetLatencyMarkerNV = nullptr;
    PFN_vkGetLatencyTimingsNV           GetLatencyTimingsNV = nullptr;
    PFN_vkQueueNotifyOutOfBandNV        QueueNotifyOutOfBandNV = nullptr;

    // --- VK_AMD_anti_lag (optional: AMD Radeon Anti-Lag 2) ---
    PFN_vkAntiLagUpdateAMD              AntiLagUpdateAMD = nullptr;

    // --- VK_EXT_debug_utils (optional) ---
    // Consumed through Vk::SetDebugName(); null in a release build, which is
    // the entire opt-out path.
    PFN_vkSetDebugUtilsObjectNameEXT    SetDebugUtilsObjectNameEXT = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT    CmdBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT      CmdEndDebugUtilsLabelEXT = nullptr;
    PFN_vkQueueBeginDebugUtilsLabelEXT  QueueBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkQueueEndDebugUtilsLabelEXT    QueueEndDebugUtilsLabelEXT = nullptr;
};

// Owns the dlopen()/LoadLibrary() handle on the Vulkan runtime and the four or
// five global commands. Not thread-safe by itself; VulkanContext serializes
// Open()/Close() behind its own mutex.
class Library
{
public:
    Library() = default;
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // Opens the platform Vulkan runtime and resolves the global dispatch
    // level. Idempotent: a second call on an open Library succeeds without
    // reopening. Returns false and fills GetFailureReason() otherwise.
    bool Open();

    // Unloads the runtime. The caller is responsible for having destroyed
    // every VkInstance first -- unloading with a live instance leaves dangling
    // function pointers inside the driver, which is undefined behaviour.
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept { return Handle != nullptr; }
    [[nodiscard]] const GlobalDispatch& Global() const noexcept { return GlobalFns; }
    [[nodiscard]] const std::string& GetFailureReason() const noexcept { return FailureReason; }
    [[nodiscard]] const std::string& GetLibraryName() const noexcept { return LibraryName; }

    // Instance version reported by the runtime, as a packed VK_MAKE_API_VERSION
    // value. A 1.0 loader has no vkEnumerateInstanceVersion, and the spec says
    // that absence means 1.0, so this returns VK_API_VERSION_1_0 in that case
    // instead of failing.
    [[nodiscard]] u32 GetInstanceVersion() const noexcept { return InstanceVersion; }

private:
    // void* rather than HMODULE so the header stays free of platform types;
    // the .cpp does the one reinterpret_cast.
    void* Handle = nullptr;
    GlobalDispatch GlobalFns{};
    std::string FailureReason;
    std::string LibraryName;
    u32 InstanceVersion = 0;
};

// True when `name` appears in `enabled` (case-sensitive, as the Vulkan spec
// requires for extension names).
[[nodiscard]] bool ExtensionEnabled(const std::vector<const char*>& enabled, const char* name) noexcept;

// Resolves the instance dispatch level for `instance`.
//
// `enabledExtensions` must be exactly the list handed to vkCreateInstance:
// entry points belonging to an extension that was *not* enabled are left null
// (looking them up would be undefined behaviour on some loaders), while a
// missing entry point for an extension that *was* enabled is a hard failure --
// that combination means the runtime lied about supporting it.
bool LoadInstanceDispatch(
    const GlobalDispatch& global,
    VkInstance instance,
    const std::vector<const char*>& enabledExtensions,
    InstanceDispatch& out,
    std::string& outFailureReason);

// Same contract at the device level, resolved through vkGetDeviceProcAddr so
// the hot vkCmd* path does not go through the loader trampoline.
bool LoadDeviceDispatch(
    const InstanceDispatch& instanceFns,
    VkDevice device,
    const std::vector<const char*>& enabledExtensions,
    DeviceDispatch& out,
    std::string& outFailureReason);

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_LOADER_H
