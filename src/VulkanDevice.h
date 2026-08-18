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

#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "VulkanCommon.h"
#include "VulkanContext.h"
#include "VulkanFeatureProbe.h"
#include "VulkanLoader.h"

namespace melonDS
{

// Optional vendor low-latency extensions the caller wants enabled.
//
// These MUST be decided at vkCreateDevice time -- a device extension cannot be
// added to a live VkDevice -- so the presenter passes its configuration down to
// VulkanDevice::Create() rather than trying to turn Reflex on later. Requesting
// one the hardware does not expose is not an error: the extension is skipped,
// the reason is recorded, and device creation proceeds unchanged. That is the
// whole "never fail renderer creation over a vendor feature" rule.
//
// Declared at namespace scope rather than nested in VulkanDevice because
// Create() takes one as a defaulted argument, and a default argument may not
// name a nested class whose member initializers are not yet complete.
struct VulkanLowLatencyRequest
{
    bool NvLowLatency2 = false;
    bool AmdAntiLag = false;
    // Vendor-neutral WSI stack. This only enables optional device capabilities;
    // the presenter policy decides whether it is telemetry-only or may wait.
    bool GenericPresentTiming = false;
};

// Which VK_EXT_present_timing scheduling modes the logical device actually
// enabled.
//
// A VkDevice cannot be asked what it was created with, and requesting a target
// presentation time through a mode the feature struct did not enable is invalid
// usage. The device records the answer once so the pacer can gate absolute
// target-time scheduling without re-probing the physical device every frame.
struct VulkanPresentTimingDeviceFeatures
{
    bool PresentTiming = false;
    bool PresentAtAbsoluteTime = false;
    bool PresentAtRelativeTime = false;
    bool GoogleDisplayTiming = false;
};

// Outcome of one VulkanLowLatencyRequest member, kept so the caller can log
// Requested / Supported / Enabled / Actual / Reason without re-querying the
// driver.
struct VulkanLowLatencyStatus
{
    bool Requested = false;
    bool Supported = false;
    bool Enabled = false;
    std::string Reason;
};

// The logical device, its queues and its dispatch table.
//
// VulkanDevice is a copyable, ref-counted view of one process-wide logical
// device. On Windows the backing device is deliberately retained so a renderer
// switch cannot destroy it from inside the transition it is unwinding; other
// platforms release it when the final view goes away.
struct VulkanDeviceState;

class VulkanDevice
{
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = default;
    VulkanDevice& operator=(const VulkanDevice&) = default;
    VulkanDevice(VulkanDevice&&) noexcept = default;
    VulkanDevice& operator=(VulkanDevice&&) noexcept = default;

    // Creates the logical device on the physical device the context already
    // selected. `requestedRendererName` only feeds the startup log line, which
    // must state what the user asked for next to what was actually created.
    //
    // Returns false and fills GetFailureReason() on any failure; nothing is
    // left half-constructed.
    bool Create(
        VulkanContext& context,
        const char* requestedRendererName = "Vulkan",
        const VulkanLowLatencyRequest& lowLatency = VulkanLowLatencyRequest{});

    // True when this process already owns a logical device for `context`.
    // The presenter uses this to avoid re-selecting the physical device after
    // the renderer has already created child objects from it.
    [[nodiscard]] static bool HasSharedDevice(const VulkanContext& context) noexcept;

    // Windows retains one shared-device reference so destroying the final
    // renderer/presenter view cannot re-enter the driver from its destructor.
    // A synchronous backend transition calls this only after both views have
    // been destroyed and their stacks have unwound, allowing DX12 to acquire
    // the hardware adapter instead of falling through to a software device.
    static void ReleaseRetainedDeviceForBackendTransition() noexcept;

    // Resolves presentation on a logical device that was created after a
    // headless physical-device probe. This succeeds when the already-created
    // main queue can present to `surface`; a VkDevice cannot grow a new queue
    // family after creation, so a different present-only family is rejected.
    bool ResolvePresentSupport(VkSurfaceKHR surface);

    // Waits for the device to go idle and destroys everything. Safe to call
    // more than once.
    //
    // The vkDeviceWaitIdle here is one of the three places the backend is
    // allowed to use it (shutdown, renderer-switch teardown, swapchain
    // recreation): destroying a VkDevice with work in flight is undefined
    // behaviour and there is no cheaper correct alternative at teardown.
    void Destroy();

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] VkDevice GetHandle() const noexcept;
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept;
    [[nodiscard]] const Vk::DeviceDispatch& Fns() const noexcept;
    [[nodiscard]] const Vk::InstanceDispatch& InstanceFns() const noexcept;

    // The queue every submission goes to. When a universal family exists this
    // is also the present queue.
    [[nodiscard]] VkQueue GetMainQueue() const noexcept;
    [[nodiscard]] u32 GetMainQueueFamily() const noexcept;

    [[nodiscard]] VkQueue GetPresentQueue() const noexcept;
    [[nodiscard]] u32 GetPresentQueueFamily() const noexcept;

    // VkQueue host access is externally synchronized by the Vulkan spec. The
    // renderer and presenter share the same queue, so every QueueSubmit and
    // QueuePresentKHR call takes this shared mutex.
    [[nodiscard]] std::mutex& GetQueueMutex() const noexcept;

    // True when the main and present queues come from different families, in
    // which case swapchain images need real ownership transfer barriers.
    [[nodiscard]] bool RequiresPresentOwnershipTransfer() const noexcept
    {
        return GetPresentQueue() != VK_NULL_HANDLE
            && GetPresentQueueFamily() != GetMainQueueFamily();
    }

    [[nodiscard]] const Vk::DeviceProbeResult& GetProfile() const noexcept;
    [[nodiscard]] const std::vector<const char*>& GetEnabledExtensions() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceFeatures& GetEnabledFeatures() const noexcept;
    [[nodiscard]] const std::string& GetFailureReason() const noexcept { return FailureReason; }

    // What actually happened to each requested low-latency extension.
    [[nodiscard]] const VulkanLowLatencyStatus& GetNvLowLatency2Status() const noexcept;
    [[nodiscard]] const VulkanLowLatencyStatus& GetAmdAntiLagStatus() const noexcept;

    // Which present-timing scheduling modes vkCreateDevice actually enabled.
    // All false when the extension was skipped or dropped by the fail-soft
    // retry, which keeps target-time presentation off rather than invalid.
    [[nodiscard]] const VulkanPresentTimingDeviceFeatures&
        GetPresentTimingFeatures() const noexcept;

    // Highest internal resolution the selected device can actually run.
    [[nodiscard]] int GetMaxScaleFactor() const noexcept;

    // Memory properties, cached because VulkanMemory reads them on every
    // allocation and the query is not free on some drivers.
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceLimits& GetLimits() const noexcept;

    // Re-queries optional live budget data at device init and resource
    // recreation boundaries. This is deliberately not a per-frame operation.
    bool RefreshMemoryAdmission();
    [[nodiscard]] Vk::VulkanMemoryAdmissionSnapshot GetMemoryAdmissionSnapshot() const;
    [[nodiscard]] bool AdmitScaleDependentResources(
        const Vk::ResolutionBudget& budget, const char* reason) const;

    // Dedicated allocations reserve the process-local allocation count before
    // vkAllocateMemory. The reservation closes the check/register race between
    // concurrent resource creators; failed Vulkan calls release it again.
    [[nodiscard]] bool ReserveMemoryAllocation(
        u32 memoryTypeIndex, VkDeviceSize size, const char* debugName) const;
    void ReleaseMemoryAllocation(u32 memoryTypeIndex, VkDeviceSize size) const noexcept;

    // Captures compact VK_EXT_device_fault metadata once after a device-loss
    // boundary. Vendor binary data is intentionally not dumped into the log.
    void ReportDeviceLost(const char* operation) const;

    // Attaches a debug name, no-op without VK_EXT_debug_utils.
    template <typename HandleT>
    void SetDebugName(VkObjectType type, HandleT handle, const char* name) const noexcept
    {
        const Vk::DeviceDispatch& fns = Fns();
        Vk::SetDebugName(fns.SetDebugUtilsObjectNameEXT, GetHandle(), type, handle, name);
    }

private:
    void LogStartupSummary(const char* requestedRendererName) const;
    void LogLowLatencySummary() const;

    std::shared_ptr<VulkanDeviceState> State;
    std::string FailureReason;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_DEVICE_H
