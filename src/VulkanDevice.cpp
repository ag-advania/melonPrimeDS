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

#include "VulkanDevice.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace melonDS
{

struct VulkanDeviceState
{
    ~VulkanDeviceState()
    {
        if (Device != VK_NULL_HANDLE)
        {
            if (DeviceFns.DeviceWaitIdle)
            {
                const VkResult res = DeviceFns.DeviceWaitIdle(Device);
                if (res != VK_SUCCESS)
                {
                    Platform::Log(Platform::LogLevel::Warn,
                        "[Vulkan] vkDeviceWaitIdle during shared-device teardown: %s\n",
                        Vk::FormatResult(res).c_str());
                }
            }
            if (DeviceFns.DestroyDevice)
                DeviceFns.DestroyDevice(Device, nullptr);
        }
        if (OwnsContextReference && Context)
            Context->Release();
    }

    VulkanContext* Context = nullptr;
    bool OwnsContextReference = false;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    Vk::DeviceDispatch DeviceFns{};
    VkQueue MainQueue = VK_NULL_HANDLE;
    u32 MainQueueFamily = Vk::QueueFamilySelection::InvalidFamily;
    VkQueue PresentQueue = VK_NULL_HANDLE;
    u32 PresentQueueFamily = Vk::QueueFamilySelection::InvalidFamily;
    Vk::DeviceProbeResult Profile;
    std::vector<const char*> EnabledExtensions;
    VkPhysicalDeviceFeatures EnabledFeatures{};
    VulkanLowLatencyStatus NvLowLatency2;
    VulkanLowLatencyStatus AmdAntiLag;
    bool GenericPresentTimingRequested = false;
    VulkanPresentTimingDeviceFeatures PresentTimingFeatures{};
    bool DeviceFaultEnabled = false;
    std::atomic<bool> DeviceFaultReported{false};
    mutable std::mutex QueueMutex;
    mutable std::mutex MemoryMutex;
    Vk::VulkanMemoryTelemetry MemoryTelemetry{};
};

namespace
{

// Same reason as in VulkanContext.cpp: the macro for this name only exists
// behind VK_ENABLE_BETA_EXTENSIONS, which the build does not define.
constexpr const char* PortabilitySubsetExtensionName = "VK_KHR_portability_subset";

// Single priority value shared by every queue. The backend creates exactly one
// queue per family, so relative priorities have nothing to arbitrate; 1.0 is
// the documented "as important as anything else" value.
constexpr float QueuePriority = 1.0f;

std::mutex SharedDeviceMutex;
std::weak_ptr<VulkanDeviceState> SharedDevice;

#if defined(_WIN32)
// On Windows, backend switching can leave driver or injected-layer callbacks
// alive beyond the last frontend Vulkan view, so releasing that view during a
// live switch must not call vkDestroyDevice from inside the transition. Keep
// one deliberately retained reference. Normal shutdown lets Windows reclaim
// it, while a synchronous graphics-backend transition releases it explicitly
// only after both presenter and renderer destruction stacks have unwound. The
// pointer itself is intentionally never destroyed, so static-destruction order
// cannot re-enter the Vulkan loader after VulkanContext has already gone away.
std::shared_ptr<VulkanDeviceState>& ProcessLifetimeDevice() noexcept
{
    static auto* retained = new std::shared_ptr<VulkanDeviceState>();
    return *retained;
}
#endif

const Vk::DeviceDispatch EmptyDeviceDispatch{};
const Vk::DeviceProbeResult EmptyDeviceProfile{};
const std::vector<const char*> EmptyExtensions;
const VkPhysicalDeviceFeatures EmptyFeatures{};
const VulkanLowLatencyStatus EmptyLowLatencyStatus{};
const VkPhysicalDeviceMemoryProperties EmptyMemoryProperties{};
const VkPhysicalDeviceLimits EmptyLimits{};
const Vk::VulkanMemoryAdmissionSnapshot EmptyMemoryAdmission{};
std::mutex EmptyQueueMutex;

const char* DeviceTypeName(VkPhysicalDeviceType type) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
    default:                                     return "other";
    }
}

} // namespace


VulkanDevice::~VulkanDevice()
{
    Destroy();
}


const Vk::InstanceDispatch& VulkanDevice::InstanceFns() const noexcept
{
    return State->Context->Fns();
}

bool VulkanDevice::IsValid() const noexcept
{
    return State && State->Device != VK_NULL_HANDLE;
}

VkDevice VulkanDevice::GetHandle() const noexcept
{
    return State ? State->Device : VK_NULL_HANDLE;
}

VkPhysicalDevice VulkanDevice::GetPhysicalDevice() const noexcept
{
    return State ? State->PhysicalDevice : VK_NULL_HANDLE;
}

const Vk::DeviceDispatch& VulkanDevice::Fns() const noexcept
{
    return State ? State->DeviceFns : EmptyDeviceDispatch;
}

VkQueue VulkanDevice::GetMainQueue() const noexcept
{
    return State ? State->MainQueue : VK_NULL_HANDLE;
}

u32 VulkanDevice::GetMainQueueFamily() const noexcept
{
    return State ? State->MainQueueFamily : Vk::QueueFamilySelection::InvalidFamily;
}

VkQueue VulkanDevice::GetPresentQueue() const noexcept
{
    return State ? State->PresentQueue : VK_NULL_HANDLE;
}

u32 VulkanDevice::GetPresentQueueFamily() const noexcept
{
    return State ? State->PresentQueueFamily : Vk::QueueFamilySelection::InvalidFamily;
}

std::mutex& VulkanDevice::GetQueueMutex() const noexcept
{
    return State ? State->QueueMutex : EmptyQueueMutex;
}

const Vk::DeviceProbeResult& VulkanDevice::GetProfile() const noexcept
{
    return State ? State->Profile : EmptyDeviceProfile;
}

const std::vector<const char*>& VulkanDevice::GetEnabledExtensions() const noexcept
{
    return State ? State->EnabledExtensions : EmptyExtensions;
}

const VkPhysicalDeviceFeatures& VulkanDevice::GetEnabledFeatures() const noexcept
{
    return State ? State->EnabledFeatures : EmptyFeatures;
}

const VulkanLowLatencyStatus& VulkanDevice::GetNvLowLatency2Status() const noexcept
{
    return State ? State->NvLowLatency2 : EmptyLowLatencyStatus;
}

const VulkanLowLatencyStatus& VulkanDevice::GetAmdAntiLagStatus() const noexcept
{
    return State ? State->AmdAntiLag : EmptyLowLatencyStatus;
}

const VulkanPresentTimingDeviceFeatures&
    VulkanDevice::GetPresentTimingFeatures() const noexcept
{
    static const VulkanPresentTimingDeviceFeatures EmptyPresentTimingFeatures{};
    return State ? State->PresentTimingFeatures : EmptyPresentTimingFeatures;
}

int VulkanDevice::GetMaxScaleFactor() const noexcept
{
    return State ? State->Profile.MaxScaleFactor : 0;
}

const VkPhysicalDeviceMemoryProperties& VulkanDevice::GetMemoryProperties() const noexcept
{
    return State ? State->Profile.MemoryProperties : EmptyMemoryProperties;
}

const VkPhysicalDeviceLimits& VulkanDevice::GetLimits() const noexcept
{
    return State ? State->Profile.Properties.limits : EmptyLimits;
}

bool VulkanDevice::RefreshMemoryAdmission()
{
    if (!State || !State->Context || State->PhysicalDevice == VK_NULL_HANDLE)
        return false;

    Vk::VulkanMemoryAdmissionSnapshot snapshot = Vk::FeatureProbe::QueryMemoryAdmission(
        State->Context->Fns(),
        State->PhysicalDevice,
        State->Profile.Properties,
        State->Profile.MemoryProperties);
    {
        std::lock_guard<std::mutex> lock(State->MemoryMutex);
        // Preserve the minimal process-local reservation state across a fresh
        // driver budget snapshot. Detailed telemetry is optional diagnostics
        // and is never the authority for admission.
        snapshot.CurrentAllocationCount =
            State->Profile.MemoryAdmission.CurrentAllocationCount;
        snapshot.CurrentReservedBytes =
            State->Profile.MemoryAdmission.CurrentReservedBytes;
        State->Profile.MemoryAdmission = snapshot;
    }

    Platform::Log(
        Platform::LogLevel::Debug,
        "[Vulkan] memory admission refreshed: budget=%s allocation-size=%s "
        "allocation-count=%u current=%u\n",
        snapshot.HasLiveBudget ? "live" : "75%-heuristic",
        snapshot.MaxMemoryAllocationSize == 0 ? "unavailable" : "available",
        snapshot.MaxMemoryAllocationCount,
        snapshot.CurrentAllocationCount);
    LogMemoryTelemetry("memory admission refresh");
    return true;
}

Vk::VulkanMemoryAdmissionSnapshot VulkanDevice::GetMemoryAdmissionSnapshot() const
{
    if (!State)
        return EmptyMemoryAdmission;

    std::lock_guard<std::mutex> lock(State->MemoryMutex);
    return State->Profile.MemoryAdmission;
}

Vk::VulkanMemoryTelemetrySnapshot VulkanDevice::GetMemoryTelemetry() const
{
    if (!State)
        return {};

    std::lock_guard<std::mutex> lock(State->MemoryMutex);
    return State->MemoryTelemetry.GetSnapshot();
}

void VulkanDevice::LogMemoryTelemetry(const char* boundary) const
{
#if defined(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
    const Vk::VulkanMemoryTelemetrySnapshot telemetry = GetMemoryTelemetry();
    VkDeviceSize currentBytes = 0;
    VkDeviceSize peakBytes = 0;
    for (u32 heap = 0; heap < VK_MAX_MEMORY_HEAPS; ++heap)
    {
        currentBytes += telemetry.CurrentBytes[heap];
        peakBytes += telemetry.PeakBytes[heap];
    }
    Platform::Log(
        Platform::LogLevel::Debug,
        "[Vulkan] memory telemetry boundary=%s current-count=%u peak-count=%u "
        "allocations=%llu frees=%llu current-bytes=%.1f MiB peak-bytes=%.1f MiB "
        "largest=%.1f MiB "
        "buckets=1M:%llu,4M:%llu,16M:%llu,64M:%llu,256M:%llu,1G:%llu,4G:%llu,large:%llu\n",
        boundary ? boundary : "unspecified",
        telemetry.CurrentAllocationCount,
        telemetry.PeakAllocationCount,
        static_cast<unsigned long long>(telemetry.TotalAllocationCount),
        static_cast<unsigned long long>(telemetry.TotalFreeCount),
        static_cast<double>(currentBytes) / Vk::MemoryMiB,
        static_cast<double>(peakBytes) / Vk::MemoryMiB,
        static_cast<double>(telemetry.LargestAllocation) / Vk::MemoryMiB,
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[0]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[1]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[2]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[3]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[4]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[5]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[6]),
        static_cast<unsigned long long>(telemetry.AllocationSizeBuckets[7]));

    for (u32 heap = 0; heap < VK_MAX_MEMORY_HEAPS; ++heap)
    {
        if (telemetry.CurrentBytes[heap] == 0 && telemetry.PeakBytes[heap] == 0)
            continue;
        Platform::Log(
            Platform::LogLevel::Debug,
            "[Vulkan] memory telemetry heap=%u current=%.1f MiB peak=%.1f MiB\n",
            heap,
            static_cast<double>(telemetry.CurrentBytes[heap]) / Vk::MemoryMiB,
            static_cast<double>(telemetry.PeakBytes[heap]) / Vk::MemoryMiB);
    }
#else
    (void)boundary;
#endif
}

bool VulkanDevice::AdmitScaleDependentResources(
    const Vk::ResolutionBudget& budget, const char* reason) const
{
    if (!State)
        return false;

    std::lock_guard<std::mutex> lock(State->MemoryMutex);
    const Vk::VulkanMemoryAdmissionSnapshot& snapshot = State->Profile.MemoryAdmission;
    const u32 heapIndex = snapshot.PreferredDeviceLocalMemoryType
        < snapshot.MemoryTypeCount
        ? snapshot.MemoryTypeHeapIndex[snapshot.PreferredDeviceLocalMemoryType]
        : Vk::InvalidMemoryHeap;
    const Vk::VulkanMemoryAdmissionRequest request{
        budget.ProjectedDeviceLocalBytes,
        heapIndex < snapshot.HeapCount ? snapshot.CurrentReservedBytes[heapIndex] : 0,
        budget.LargestDeviceAllocation,
        budget.ProjectedAllocationCount,
        snapshot.PreferredDeviceLocalMemoryType,
    };
    const Vk::VulkanMemoryAdmissionResult result =
        Vk::EvaluateVulkanMemoryAdmission(snapshot, request);
    if (result.Accepted)
        return true;

    Platform::Log(
        Platform::LogLevel::Error,
        "[Vulkan] scale admission refused reason=%s scale=%d requested=%.1f MiB "
        "heap=%u type=%u budget=%.1f MiB usage=%.1f MiB available=%.1f MiB "
        "reserve=%.1f MiB largest=%.1f MiB max-allocation=%.1f MiB "
        "current-count=%u additional-count=%u max-count=%u boundary=%s\n",
        Vk::VulkanMemoryAdmissionReasonText(result.Reason),
        budget.ScaleFactor,
        static_cast<double>(budget.ProjectedDeviceLocalBytes) / Vk::MemoryMiB,
        result.HeapIndex,
        request.MemoryTypeIndex,
        static_cast<double>(result.HeapBudget) / Vk::MemoryMiB,
        static_cast<double>(result.HeapUsage) / Vk::MemoryMiB,
        static_cast<double>(result.AvailableBytes) / Vk::MemoryMiB,
        static_cast<double>(result.SafetyReserve) / Vk::MemoryMiB,
        static_cast<double>(budget.LargestDeviceAllocation) / Vk::MemoryMiB,
        static_cast<double>(snapshot.MaxMemoryAllocationSize) / Vk::MemoryMiB,
        snapshot.CurrentAllocationCount,
        budget.ProjectedAllocationCount,
        snapshot.MaxMemoryAllocationCount,
        reason ? reason : "scale resource recreation");
    return false;
}

bool VulkanDevice::ReserveMemoryAllocation(
    u32 memoryTypeIndex, VkDeviceSize size, const char* debugName) const
{
    if (!State)
        return false;

    std::lock_guard<std::mutex> lock(State->MemoryMutex);
    const Vk::VulkanMemoryAdmissionSnapshot& snapshot = State->Profile.MemoryAdmission;
    const u32 heapIndex = memoryTypeIndex < snapshot.MemoryTypeCount
        ? snapshot.MemoryTypeHeapIndex[memoryTypeIndex] : Vk::InvalidMemoryHeap;
    const Vk::VulkanMemoryAdmissionRequest request{
        size,
        heapIndex < snapshot.HeapCount ? snapshot.CurrentReservedBytes[heapIndex] : 0,
        size,
        1,
        memoryTypeIndex };
    const Vk::VulkanMemoryAdmissionResult result =
        Vk::EvaluateVulkanMemoryAdmission(snapshot, request);
    if (!result.Accepted)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] allocation admission refused name=%s requested=%.1f MiB "
            "type=%u heap=%u budget=%.1f MiB usage=%.1f MiB available=%.1f MiB "
            "reserve=%.1f MiB max-allocation=%.1f MiB current-count=%u max-count=%u "
            "reason=%s\n",
            debugName ? debugName : "<unnamed allocation>",
            static_cast<double>(size) / Vk::MemoryMiB,
            memoryTypeIndex,
            result.HeapIndex,
            static_cast<double>(result.HeapBudget) / Vk::MemoryMiB,
            static_cast<double>(result.HeapUsage) / Vk::MemoryMiB,
            static_cast<double>(result.AvailableBytes) / Vk::MemoryMiB,
            static_cast<double>(result.SafetyReserve) / Vk::MemoryMiB,
            static_cast<double>(snapshot.MaxMemoryAllocationSize) / Vk::MemoryMiB,
            snapshot.CurrentAllocationCount,
            snapshot.MaxMemoryAllocationCount,
            Vk::VulkanMemoryAdmissionReasonText(result.Reason));
        return false;
    }

    auto& admission = State->Profile.MemoryAdmission;
    ++admission.CurrentAllocationCount;
    admission.CurrentReservedBytes[result.HeapIndex] += size;
#if defined(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
    State->MemoryTelemetry.RecordAllocation(result.HeapIndex, size);
#endif
    return true;
}

void VulkanDevice::ReleaseMemoryAllocation(u32 memoryTypeIndex, VkDeviceSize size) const noexcept
{
    if (!State)
        return;

    std::lock_guard<std::mutex> lock(State->MemoryMutex);
    auto& admission = State->Profile.MemoryAdmission;
    u32 heapIndex = Vk::InvalidMemoryHeap;
    if (memoryTypeIndex < admission.MemoryTypeCount)
        heapIndex = admission.MemoryTypeHeapIndex[memoryTypeIndex];
    if (admission.CurrentAllocationCount != 0)
        --admission.CurrentAllocationCount;
    if (heapIndex < admission.HeapCount)
    {
        const VkDeviceSize current = admission.CurrentReservedBytes[heapIndex];
        admission.CurrentReservedBytes[heapIndex] = current > size ? current - size : 0;
    }
#if defined(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
    State->MemoryTelemetry.RecordFree(heapIndex, size);
#endif
}

void VulkanDevice::ReportDeviceLost(const char* operation) const
{
    if (!State || !State->DeviceFaultEnabled
        || State->DeviceFaultReported.exchange(true, std::memory_order_acq_rel))
        return;

    const PFN_vkGetDeviceFaultInfoEXT getFaultInfo = State->DeviceFns.GetDeviceFaultInfoEXT;
    if (!getFaultInfo)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] device lost during %s; VK_EXT_device_fault entry point unavailable\n",
            operation ? operation : "unknown operation");
        return;
    }

    VkDeviceFaultCountsEXT counts{};
    counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    VkResult res = getFaultInfo(State->Device, &counts, nullptr);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] device lost during %s; vkGetDeviceFaultInfoEXT counts failed: %s\n",
            operation ? operation : "unknown operation",
            Vk::FormatResult(res).c_str());
        return;
    }

    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
    VkDeviceFaultInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
    info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
    info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
    // Do not allocate or print vendorBinarySize here. The binary can be large;
    // release logs keep only availability metadata and a developer artifact
    // collector can opt into copying it later.
    info.pVendorBinaryData = nullptr;
    res = getFaultInfo(State->Device, &counts, &info);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] device lost during %s; vkGetDeviceFaultInfoEXT info failed: %s\n",
            operation ? operation : "unknown operation",
            Vk::FormatResult(res).c_str());
        return;
    }

    Platform::Log(
        Platform::LogLevel::Error,
        "[Vulkan] device fault diagnostics operation=%s description=\"%s\" "
        "address-info=%u vendor-info=%u vendor-binary=%llu bytes\n",
        operation ? operation : "unknown operation",
        info.description[0] ? info.description : "<none>",
        counts.addressInfoCount,
        counts.vendorInfoCount,
        static_cast<unsigned long long>(counts.vendorBinarySize));
    for (const VkDeviceFaultVendorInfoEXT& vendor : vendors)
    {
        Platform::Log(
            Platform::LogLevel::Debug,
            "[Vulkan] device fault vendor-info description=\"%s\" code=%llu data=%llu\n",
            vendor.description[0] ? vendor.description : "<none>",
            static_cast<unsigned long long>(vendor.vendorFaultCode),
            static_cast<unsigned long long>(vendor.vendorFaultData));
    }
}

bool VulkanDevice::HasSharedDevice(const VulkanContext& context) noexcept
{
    std::lock_guard<std::mutex> lock(SharedDeviceMutex);
    const std::shared_ptr<VulkanDeviceState> shared = SharedDevice.lock();
    return shared && shared->Context == &context && shared->Device != VK_NULL_HANDLE;
}

void VulkanDevice::ReleaseRetainedDeviceForBackendTransition() noexcept
{
#if defined(_WIN32)
    std::shared_ptr<VulkanDeviceState> retired;
    {
        std::lock_guard<std::mutex> lock(SharedDeviceMutex);
        retired.swap(ProcessLifetimeDevice());
        SharedDevice.reset();
    }

    if (retired)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] releasing retained device at quiesced backend-transition boundary\n");
        // Destroy outside SharedDeviceMutex: the state releases its retained
        // VulkanContext reference after vkDestroyDevice completes.
        retired.reset();
    }
#endif
}


bool VulkanDevice::Create(
    VulkanContext& context,
    const char* requestedRendererName,
    const VulkanLowLatencyRequest& lowLatency)
{
    Destroy();

    FailureReason.clear();
    std::lock_guard<std::mutex> sharedLock(SharedDeviceMutex);

    if (std::shared_ptr<VulkanDeviceState> shared = SharedDevice.lock())
    {
        if (shared->Context != &context)
        {
            FailureReason = "a Vulkan logical device from a different instance is still alive";
            return false;
        }
        if ((lowLatency.NvLowLatency2 && !shared->NvLowLatency2.Requested)
            || (lowLatency.AmdAntiLag && !shared->AmdAntiLag.Requested)
            || (lowLatency.GenericPresentTiming && !shared->GenericPresentTimingRequested))
        {
            FailureReason =
                "the shared Vulkan device was created before presentation low-latency "
                "extensions were requested";
            return false;
        }
        State = std::move(shared);
        RefreshMemoryAdmission();
        LogStartupSummary(requestedRendererName);
        return true;
    }

    State = std::make_shared<VulkanDeviceState>();
    State->Context = &context;
    if (!context.Acquire(false))
    {
        FailureReason = "could not retain the Vulkan instance for the shared logical device";
        State.reset();
        return false;
    }
    State->OwnsContextReference = true;
    VulkanContext*& Context = State->Context;
    VkPhysicalDevice& PhysicalDevice = State->PhysicalDevice;
    VkDevice& Device = State->Device;
    Vk::DeviceDispatch& DeviceFns = State->DeviceFns;
    VkQueue& MainQueue = State->MainQueue;
    u32& MainQueueFamily = State->MainQueueFamily;
    VkQueue& PresentQueue = State->PresentQueue;
    u32& PresentQueueFamily = State->PresentQueueFamily;
    Vk::DeviceProbeResult& Profile = State->Profile;
    std::vector<const char*>& EnabledExtensions = State->EnabledExtensions;
    VkPhysicalDeviceFeatures& EnabledFeatures = State->EnabledFeatures;
    VulkanLowLatencyStatus& NvLowLatency2 = State->NvLowLatency2;
    VulkanLowLatencyStatus& AmdAntiLag = State->AmdAntiLag;

    NvLowLatency2 = VulkanLowLatencyStatus{};
    AmdAntiLag = VulkanLowLatencyStatus{};
    NvLowLatency2.Requested = lowLatency.NvLowLatency2;
    AmdAntiLag.Requested = lowLatency.AmdAntiLag;
    State->GenericPresentTimingRequested = lowLatency.GenericPresentTiming;

    if (!context.IsReady())
    {
        FailureReason = "the Vulkan instance is not ready";
        return false;
    }
    if (!context.HasSelectedDevice())
    {
        FailureReason = "no physical device has been selected";
        return false;
    }

    Profile = context.GetSelectedDevice();
    PhysicalDevice = Profile.Handle;

    const Vk::QueueFamilySelection& queues = Profile.Queues;

    // --- queue families -----------------------------------------------------
    if (queues.HasUniversalFamily())
    {
        MainQueueFamily = queues.UniversalFamily;
        // Present comes from the same family when one was found *with* a
        // surface. In the headless case PresentUnknown is true and the present
        // queue stays null until the presenter re-selects with a real surface.
        PresentQueueFamily = queues.PresentUnknown
            ? Vk::QueueFamilySelection::InvalidFamily
            : queues.UniversalFamily;
    }
    else
    {
        // Split configuration. Graphics and compute must both exist -- the
        // probe fails the device otherwise -- and the renderer records that
        // ownership transfers are required.
        if (queues.GraphicsFamily == Vk::QueueFamilySelection::InvalidFamily
            || queues.ComputeFamily == Vk::QueueFamilySelection::InvalidFamily)
        {
            FailureReason = "the selected device has no usable graphics+compute queue family";
            return false;
        }

        // The compute family carries the rasterizer, which is the bulk of the
        // frame; graphics work in this backend is limited to the present-time
        // blit, so the compute family is the "main" one.
        MainQueueFamily = queues.ComputeFamily;
        PresentQueueFamily = queues.PresentFamily;

        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] no universal queue family on %s; using compute family %u "
            "and present family %u, resources will need ownership transfers\n",
            Profile.DeviceName.c_str(), MainQueueFamily, PresentQueueFamily);
    }

    std::vector<u32> distinctFamilies;
    distinctFamilies.push_back(MainQueueFamily);
    if (PresentQueueFamily != Vk::QueueFamilySelection::InvalidFamily
        && PresentQueueFamily != MainQueueFamily)
    {
        distinctFamilies.push_back(PresentQueueFamily);
    }

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(distinctFamilies.size());
    for (u32 family : distinctFamilies)
    {
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &QueuePriority;
        queueInfos.push_back(info);
    }

    // --- extensions ---------------------------------------------------------
    EnabledExtensions.clear();

    std::vector<VkExtensionProperties> available;
    if (!Vk::FeatureProbe::EnumerateDeviceExtensions(context.Fns(), PhysicalDevice, available))
    {
        FailureReason = "vkEnumerateDeviceExtensionProperties failed on the selected device";
        return false;
    }

    // The swapchain extension is only requested when the context was created
    // for presentation; the headless settings-dialog device does not need it
    // and asking for it there would fail on a compute-only ICD.
    const bool needPresent = context.GetEnabledInstanceExtensions().size() > 0
        && Vk::ExtensionEnabled(context.GetEnabledInstanceExtensions(), "VK_KHR_surface");

    for (const char* name : Vk::FeatureProbe::GetRequiredDeviceExtensions(needPresent))
    {
        if (!Vk::FeatureProbe::HasExtension(available, name))
        {
            FailureReason = std::string("required device extension ") + name + " is missing";
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
            return false;
        }
        EnabledExtensions.push_back(name);
    }

    // Mandatory, not optional: the portability subset specification states that
    // an implementation exposing VK_KHR_portability_subset must have it enabled
    // by any device created from it. Skipping it is a spec violation that
    // validation flags immediately.
    if (Profile.RequiresPortabilitySubset)
        EnabledExtensions.push_back(PortabilitySubsetExtensionName);

    // VK_EXT_device_fault is optional diagnostics. It is enabled only when the
    // physical-device feature probe confirmed deviceFault, so a driver that
    // merely advertises the extension cannot make device creation invalid.
    if (Profile.HasDeviceFault
        && Vk::FeatureProbe::HasExtension(available, "VK_EXT_device_fault"))
        EnabledExtensions.push_back("VK_EXT_device_fault");

    // --- optional vendor low-latency extensions -----------------------------
    //
    // Everything below is strictly additive: a device that cannot do any of it
    // creates exactly the same VkDevice it would have created before. Nothing
    // here can make Create() return false.
    //
    // The feature structs must outlive vkCreateDevice, so they are declared in
    // this scope rather than inside the blocks that fill them.
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineFeatures{};
    VkPhysicalDevicePresentIdFeaturesKHR presentIdFeatures{};
    VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeatures{};
    VkPhysicalDeviceAntiLagFeaturesAMD antiLagFeatures{};
    VkPhysicalDevicePresentId2FeaturesKHR presentId2Features{};
    VkPhysicalDevicePresentWait2FeaturesKHR presentWait2Features{};
    VkPhysicalDevicePresentTimingFeaturesEXT presentTimingFeatures{};
    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR latestReadyFeatures{};
    VkPhysicalDeviceFaultFeaturesEXT deviceFaultFeatures{};
    const void* featureChain = nullptr;

    const auto chain = [&featureChain](auto& feature) {
        feature.pNext = const_cast<void*>(featureChain);
        featureChain = &feature;
    };

    if (Profile.HasDeviceFault
        && Vk::FeatureProbe::HasExtension(available, "VK_EXT_device_fault"))
    {
        deviceFaultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        deviceFaultFeatures.deviceFault = VK_TRUE;
        deviceFaultFeatures.deviceFaultVendorBinary = VK_FALSE;
        chain(deviceFaultFeatures);
        State->DeviceFaultEnabled = true;
    }

    if (lowLatency.NvLowLatency2 || lowLatency.AmdAntiLag
        || lowLatency.GenericPresentTiming)
    {
        // One vkGetPhysicalDeviceFeatures2 for everything asked for. Enabling a
        // feature bit the device reports as unsupported is invalid usage, so the
        // request is always confirmed against the driver rather than inferred
        // from the extension being present.
        void* probeChain = nullptr;
        const auto chainProbe = [&probeChain](auto& feature) {
            feature.pNext = probeChain;
            probeChain = &feature;
        };

        timelineFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
        presentIdFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        presentWaitFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        antiLagFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD;
        presentId2Features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
        presentWait2Features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR;
        presentTimingFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT;
        latestReadyFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR;

        if (Vk::FeatureProbe::HasExtension(available, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
            chainProbe(timelineFeatures);
        if (Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_ID_EXTENSION_NAME))
            chainProbe(presentIdFeatures);
        if (Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
            chainProbe(presentWaitFeatures);
        if (Vk::FeatureProbe::HasExtension(available, VK_AMD_ANTI_LAG_EXTENSION_NAME))
            chainProbe(antiLagFeatures);
        if (Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_ID_2_EXTENSION_NAME))
            chainProbe(presentId2Features);
        if (Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME))
            chainProbe(presentWait2Features);
        if (Vk::FeatureProbe::HasExtension(available, VK_EXT_PRESENT_TIMING_EXTENSION_NAME))
            chainProbe(presentTimingFeatures);
        if (Vk::FeatureProbe::HasExtension(
                available, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME))
            chainProbe(latestReadyFeatures);

        VkPhysicalDeviceFeatures2 probe{};
        probe.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        probe.pNext = probeChain;
        context.Fns().GetPhysicalDeviceFeatures2(PhysicalDevice, &probe);

        // The pNext links above were only scaffolding for the query. They are
        // rebuilt below for the structs that are actually enabled, because a
        // struct whose feature is unsupported must not reach vkCreateDevice.
        timelineFeatures.pNext = nullptr;
        presentIdFeatures.pNext = nullptr;
        presentWaitFeatures.pNext = nullptr;
        antiLagFeatures.pNext = nullptr;
        presentId2Features.pNext = nullptr;
        presentWait2Features.pNext = nullptr;
        presentTimingFeatures.pNext = nullptr;
        latestReadyFeatures.pNext = nullptr;
    }

    if (lowLatency.NvLowLatency2)
    {
        // VK_NV_low_latency2 depends on (Vulkan 1.2 or VK_KHR_timeline_semaphore)
        // AND VK_KHR_present_id. The instance is created with apiVersion 1.1, so
        // the 1.2-core route is unavailable by construction and both KHR
        // extensions have to be enabled explicitly alongside it.
        const bool hasExtension =
            Vk::FeatureProbe::HasExtension(available, VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
        const bool hasTimeline =
            Vk::FeatureProbe::HasExtension(available, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)
            && timelineFeatures.timelineSemaphore == VK_TRUE;
        const bool hasPresentId =
            needPresent
            && Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_ID_EXTENSION_NAME)
            && presentIdFeatures.presentId == VK_TRUE;

        NvLowLatency2.Supported = hasExtension && hasTimeline && hasPresentId;
        if (NvLowLatency2.Supported)
        {
            EnabledExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
            EnabledExtensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            EnabledExtensions.push_back(VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
            chain(timelineFeatures);
            chain(presentIdFeatures);
            NvLowLatency2.Enabled = true;
            NvLowLatency2.Reason = "enabled at device creation";
        }
        else if (!hasExtension)
        {
            NvLowLatency2.Reason =
                "this GPU does not expose VK_NV_low_latency2 (NVIDIA Reflex is NVIDIA-only)";
        }
        else if (!hasTimeline)
        {
            NvLowLatency2.Reason =
                "VK_KHR_timeline_semaphore is unavailable, and VkLatencySleepInfoNV requires a "
                "timeline semaphore";
        }
        else
        {
            NvLowLatency2.Reason = needPresent
                ? std::string("VK_KHR_present_id is unavailable, and VK_NV_low_latency2 depends on it")
                : std::string("this device was created without presentation support");
        }
    }
    else
    {
        NvLowLatency2.Reason = "not requested";
    }

    bool genericPresentExtensionsEnabled = false;
    if (lowLatency.GenericPresentTiming && needPresent)
    {
        const bool hasCaps2 = Vk::ExtensionEnabled(
            context.GetEnabledInstanceExtensions(),
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        const bool hasPresentId2 = hasCaps2
            && Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_ID_2_EXTENSION_NAME)
            && presentId2Features.presentId2 == VK_TRUE;

        const bool hasLegacyPresentId = needPresent
            && Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_ID_EXTENSION_NAME)
            && presentIdFeatures.presentId == VK_TRUE;
        if (hasLegacyPresentId
            && !Vk::ExtensionEnabled(EnabledExtensions, VK_KHR_PRESENT_ID_EXTENSION_NAME))
        {
            EnabledExtensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            chain(presentIdFeatures);
        }

        const bool hasLegacyPresentWait = hasLegacyPresentId
            && Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_WAIT_EXTENSION_NAME)
            && presentWaitFeatures.presentWait == VK_TRUE;
        if (hasLegacyPresentWait)
        {
            EnabledExtensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
            chain(presentWaitFeatures);
            genericPresentExtensionsEnabled = true;
        }

        if (hasPresentId2)
        {
            EnabledExtensions.push_back(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
            chain(presentId2Features);
            genericPresentExtensionsEnabled = true;
        }

        const bool hasPresentWait2 = hasPresentId2
            && Vk::FeatureProbe::HasExtension(available, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME)
            && presentWait2Features.presentWait2 == VK_TRUE;
        if (hasPresentWait2)
        {
            EnabledExtensions.push_back(VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME);
            chain(presentWait2Features);
        }

        const bool hasCalibratedTimestamps = Vk::FeatureProbe::HasExtension(
            available, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
        const bool hasPresentTiming = hasCaps2 && hasPresentId2 && hasCalibratedTimestamps
            && Vk::FeatureProbe::HasExtension(available, VK_EXT_PRESENT_TIMING_EXTENSION_NAME)
            && presentTimingFeatures.presentTiming == VK_TRUE;
        if (hasPresentTiming)
        {
            EnabledExtensions.push_back(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            EnabledExtensions.push_back(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
            chain(presentTimingFeatures);
            // Remember which scheduling modes the feature struct actually
            // enabled. Target-time presentation is only legal for the ones
            // enabled here, and the pacer cannot re-query a live device.
            State->PresentTimingFeatures.PresentTiming = true;
            State->PresentTimingFeatures.PresentAtAbsoluteTime =
                presentTimingFeatures.presentAtAbsoluteTime == VK_TRUE;
            State->PresentTimingFeatures.PresentAtRelativeTime =
                presentTimingFeatures.presentAtRelativeTime == VK_TRUE;
        }

        // GOOGLE display timing is an independent target-time backend. Unlike
        // EXT it needs neither present_id2 nor the EXT surface capability
        // chain, so discover it before deciding whether FIFO_LATEST_READY can
        // be enabled.
        const bool hasGoogleDisplayTiming = Vk::FeatureProbe::HasExtension(
            available, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
        if (hasGoogleDisplayTiming)
        {
            EnabledExtensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
            State->PresentTimingFeatures.GoogleDisplayTiming = true;
            genericPresentExtensionsEnabled = true;
        }

        // VK_KHR_present_mode_fifo_latest_ready depends on VK_KHR_swapchain,
        // not VK_EXT_present_timing. It is valid with either the EXT or GOOGLE
        // time-based target backend.
        const bool hasTimeBasedPresentBackend = hasPresentTiming || hasGoogleDisplayTiming;
        const bool hasLatestReady = hasTimeBasedPresentBackend
            && Vk::FeatureProbe::HasExtension(
                available, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME)
            && latestReadyFeatures.presentModeFifoLatestReady == VK_TRUE;
        if (hasLatestReady)
        {
            EnabledExtensions.push_back(
                VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
            chain(latestReadyFeatures);
        }

        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] generic present device capabilities: caps2=%s present-id2=%s "
            "present-wait2=%s legacy-present-wait=%s calibrated-timestamps=%s present-timing=%s "
            "present-at-absolute-time=%s present-at-relative-time=%s "
            "fifo-latest-ready=%s google-display-timing=%s\n",
            hasCaps2 ? "yes" : "no",
            hasPresentId2 ? "yes" : "no",
            hasPresentWait2 ? "yes" : "no",
            hasLegacyPresentWait ? "yes" : "no",
            hasCalibratedTimestamps ? "yes" : "no",
            hasPresentTiming ? "yes" : "no",
            State->PresentTimingFeatures.PresentAtAbsoluteTime ? "yes" : "no",
            State->PresentTimingFeatures.PresentAtRelativeTime ? "yes" : "no",
            hasLatestReady ? "yes" : "no",
            hasGoogleDisplayTiming ? "yes" : "no");
    }

    if (lowLatency.AmdAntiLag)
    {
        const bool hasExtension =
            Vk::FeatureProbe::HasExtension(available, VK_AMD_ANTI_LAG_EXTENSION_NAME);
        AmdAntiLag.Supported = hasExtension && antiLagFeatures.antiLag == VK_TRUE;
        if (AmdAntiLag.Supported)
        {
            EnabledExtensions.push_back(VK_AMD_ANTI_LAG_EXTENSION_NAME);
            chain(antiLagFeatures);
            AmdAntiLag.Enabled = true;
            AmdAntiLag.Reason = "enabled at device creation";
        }
        else if (!hasExtension)
        {
            AmdAntiLag.Reason =
                "this GPU does not expose VK_AMD_anti_lag (Radeon Anti-Lag 2 is AMD-only)";
        }
        else
        {
            AmdAntiLag.Reason =
                "the driver exposes VK_AMD_anti_lag but reports VkPhysicalDeviceAntiLagFeaturesAMD"
                "::antiLag as unsupported";
        }
    }
    else
    {
        AmdAntiLag.Reason = "not requested";
    }

    // --- features -----------------------------------------------------------
    // Deliberately empty.
    //
    // The compute rasterizer was ported from GPU3D_Compute, which uses only
    // core-guaranteed functionality: storage images are declared with an
    // explicit format (so shaderStorageImageWriteWithoutFormat is not needed),
    // the 64-bit depth interpolation is done with umulExtended on two 32-bit
    // halves rather than Int64 arithmetic (so shaderInt64 is not needed), and
    // no shader stage other than compute writes to a buffer or image (so
    // fragment/vertexPipelineStoresAndAtomics are not needed). Enabling
    // features the backend does not use would narrow the set of devices that
    // can create the device for no benefit.
    EnabledFeatures = VkPhysicalDeviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    // Individual feature structs only. A VkPhysicalDeviceFeatures2 here would
    // force pEnabledFeatures to be null, and the core feature set is still
    // supplied the 1.0 way below.
    createInfo.pNext = featureChain;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<u32>(EnabledExtensions.size());
    createInfo.ppEnabledExtensionNames =
        EnabledExtensions.empty() ? nullptr : EnabledExtensions.data();
    createInfo.pEnabledFeatures = &EnabledFeatures;
    // ppEnabledLayerNames is ignored for devices since Vulkan 1.0.13 -- device
    // layers are deprecated and the instance layer list already covers
    // validation -- so it is left null on purpose.

    VkResult res =
        context.Fns().CreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);

    if (res != VK_SUCCESS
        && (NvLowLatency2.Enabled || AmdAntiLag.Enabled || genericPresentExtensionsEnabled
            || State->DeviceFaultEnabled))
    {
        // A vendor latency extension must never be the reason the renderer
        // fails to start. The driver accepted every extension name and every
        // feature bit when queried, so reaching here means it contradicted
        // itself; the device is rebuilt without any of the optional additions
        // and the loss is reported instead of propagated.
        const std::string firstAttempt = Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkCreateDevice rejected optional extensions (%s); "
            "retrying without them\n",
            firstAttempt.c_str());

        EnabledExtensions.erase(
            std::remove_if(
                EnabledExtensions.begin(),
                EnabledExtensions.end(),
                [](const char* name) {
                    return std::strcmp(name, VK_NV_LOW_LATENCY_2_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_PRESENT_ID_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_AMD_ANTI_LAG_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_PRESENT_ID_2_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_EXT_PRESENT_TIMING_EXTENSION_NAME) == 0
                        || std::strcmp(name, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0
                        || std::strcmp(
                            name, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME) == 0
                        || std::strcmp(name, "VK_EXT_device_fault") == 0;
                }),
            EnabledExtensions.end());

        const std::string retryReason =
            "vkCreateDevice rejected the extension: " + firstAttempt;
        if (NvLowLatency2.Enabled)
        {
            NvLowLatency2.Enabled = false;
            NvLowLatency2.Supported = false;
            NvLowLatency2.Reason = retryReason;
        }
        if (AmdAntiLag.Enabled)
        {
            AmdAntiLag.Enabled = false;
            AmdAntiLag.Supported = false;
            AmdAntiLag.Reason = retryReason;
        }
        // The retry device has no VK_EXT_present_timing, so no scheduling mode
        // survives it either. Leaving these set would let the pacer request a
        // target time through entry points this device never enabled.
        State->PresentTimingFeatures = VulkanPresentTimingDeviceFeatures{};
        State->DeviceFaultEnabled = false;

        createInfo.pNext = nullptr;
        createInfo.enabledExtensionCount = static_cast<u32>(EnabledExtensions.size());
        createInfo.ppEnabledExtensionNames =
            EnabledExtensions.empty() ? nullptr : EnabledExtensions.data();
        res = context.Fns().CreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
    }

    if (res != VK_SUCCESS)
    {
        Device = VK_NULL_HANDLE;
        FailureReason = "vkCreateDevice failed: " + Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    std::string dispatchFailure;
    if (!Vk::LoadDeviceDispatch(context.Fns(), Device, EnabledExtensions, DeviceFns, dispatchFailure))
    {
        FailureReason = dispatchFailure;
        // The device exists but is unusable. Tear it down through the normal
        // path so the failure leaves nothing behind.
        Destroy();
        return false;
    }

    DeviceFns.GetDeviceQueue(Device, MainQueueFamily, 0, &MainQueue);
    if (MainQueue == VK_NULL_HANDLE)
    {
        FailureReason = "vkGetDeviceQueue returned no queue for the main family";
        Destroy();
        return false;
    }

    if (PresentQueueFamily != Vk::QueueFamilySelection::InvalidFamily)
    {
        if (PresentQueueFamily == MainQueueFamily)
        {
            // Same family, same queue index: the spec guarantees this is the
            // identical VkQueue, so no second retrieval is needed and, more
            // importantly, submissions to it are ordered against each other
            // with no cross-queue synchronization.
            PresentQueue = MainQueue;
        }
        else
        {
            DeviceFns.GetDeviceQueue(Device, PresentQueueFamily, 0, &PresentQueue);
            if (PresentQueue == VK_NULL_HANDLE)
            {
                FailureReason = "vkGetDeviceQueue returned no queue for the present family";
                Destroy();
                return false;
            }
        }
    }

    // Device-local usage can include allocations made by the loader or other
    // shared-device clients after the physical-device probe. Refresh once at
    // logical-device initialization so the first scale admission sees current
    // live budget and allocation-count state.
    RefreshMemoryAdmission();

    SetDebugName(VK_OBJECT_TYPE_DEVICE, Device, "melonPrimeDS device");
    SetDebugName(VK_OBJECT_TYPE_QUEUE, MainQueue, "melonPrimeDS main queue");

    LogStartupSummary(requestedRendererName);
    SharedDevice = State;
#if defined(_WIN32)
    ProcessLifetimeDevice() = State;
#endif
    return true;
}


void VulkanDevice::Destroy()
{
    State.reset();
}

bool VulkanDevice::ResolvePresentSupport(VkSurfaceKHR surface)
{
    if (!State || surface == VK_NULL_HANDLE || !State->Context)
    {
        FailureReason = "cannot resolve presentation without a shared device and surface";
        return false;
    }

    VkBool32 supported = VK_FALSE;
    const VkResult res = State->Context->Fns().GetPhysicalDeviceSurfaceSupportKHR(
        State->PhysicalDevice, State->MainQueueFamily, surface, &supported);
    if (res != VK_SUCCESS)
    {
        FailureReason = "vkGetPhysicalDeviceSurfaceSupportKHR failed: " + Vk::FormatResult(res);
        return false;
    }
    if (supported != VK_TRUE)
    {
        FailureReason =
            "the existing Vulkan device's main queue cannot present to this surface; "
            "the presenter must initialize before the renderer on this driver";
        return false;
    }

    State->PresentQueueFamily = State->MainQueueFamily;
    State->PresentQueue = State->MainQueue;
    return true;
}


void VulkanDevice::LogStartupSummary(const char* requestedRendererName) const
{
    if (!State)
        return;
    VulkanContext* Context = State->Context;
    const Vk::DeviceProbeResult& Profile = State->Profile;
    const auto& EnabledExtensions = State->EnabledExtensions;
    const u32 PresentQueueFamily = State->PresentQueueFamily;
    const u32 MainQueueFamily = State->MainQueueFamily;
    const char* requested = requestedRendererName ? requestedRendererName : "Vulkan";

    // requested/actual are logged together and in that order so a log excerpt
    // can never be read as a successful Vulkan start when a fallback happened.
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] renderer requested=%s actual=Vulkan\n", requested);

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] instance API %s, device API %s\n",
        Vk::FormatApiVersion(Context->GetInstanceVersion()).c_str(),
        Profile.ApiVersionText.c_str());

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] GPU %s (%s, %s) driver %s\n",
        Profile.DeviceName.c_str(),
        Profile.VendorName.c_str(),
        DeviceTypeName(Profile.Properties.deviceType),
        Profile.DriverVersionText.c_str());

    if (PresentQueueFamily == Vk::QueueFamilySelection::InvalidFamily)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: main family %u (present family not resolved yet)\n",
            MainQueueFamily);
    }
    else if (PresentQueueFamily == MainQueueFamily)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: family %u handles graphics, compute and present\n",
            MainQueueFamily);
    }
    else
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: main family %u, present family %u (ownership transfers active)\n",
            MainQueueFamily, PresentQueueFamily);
    }

    if (EnabledExtensions.empty())
    {
        Platform::Log(Platform::LogLevel::Info, "[Vulkan] device extensions: none\n");
    }
    else
    {
        for (const char* name : EnabledExtensions)
            Platform::Log(Platform::LogLevel::Info, "[Vulkan] device extension: %s\n", name);
    }

    // The enabled feature set is empty by design; saying so explicitly stops
    // an empty line from reading as "the log is broken".
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] device features: none required (core 1.1 functionality only)\n");

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] device-local memory %.1f MiB, internal resolution up to %dx\n",
        static_cast<double>(Profile.DeviceLocalMemory) / (1024.0 * 1024.0),
        Profile.MaxScaleFactor);

    if (Profile.RequiresPortabilitySubset)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] portability implementation: VK_KHR_portability_subset enabled\n");
    }

    LogLowLatencySummary();
}


void VulkanDevice::LogLowLatencySummary() const
{
    if (!State)
        return;
    const VulkanLowLatencyStatus& NvLowLatency2 = State->NvLowLatency2;
    const VulkanLowLatencyStatus& AmdAntiLag = State->AmdAntiLag;
    // Device-creation diagnostics only: a VkDevice can report whether an
    // extension was requested and enabled, but it cannot know the user's live
    // mode or whether the presenter is actively issuing markers. The presenter
    // emits the separate requested/supported/device-enabled/actual/reason line.
    const auto describe = [](const char* name, const VulkanLowLatencyStatus& status) {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] %s device creation: extension-requested=%s supported=%s "
            "device-extension-enabled=%s reason=%s\n",
            name,
            status.Requested ? "yes" : "no",
            status.Supported ? "yes" : "no",
            status.Enabled ? "yes" : "no",
            status.Reason.empty() ? "not evaluated" : status.Reason.c_str());
    };

    describe("NVIDIA Reflex (VK_NV_low_latency2)", NvLowLatency2);
    describe("AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag)", AmdAntiLag);
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
