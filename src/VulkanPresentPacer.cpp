/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
*/

#include "VulkanPresentPacer.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <vector>

namespace melonDS
{

namespace
{

constexpr u64 MaxPresentWaitNs = 2'000'000; // A driver stall must never hang input.
constexpr u32 TimingLogPeriodFrames = 600;

bool HasEnabledExtension(const VulkanDevice& device, const char* name) noexcept
{
    return Vk::ExtensionEnabled(device.GetEnabledExtensions(), name);
}

} // namespace

const char* VulkanPresentPacingPolicyName(VulkanPresentPacingPolicy policy) noexcept
{
    switch (policy)
    {
    case VulkanPresentPacingPolicy::TelemetryOnly: return "TelemetryOnly";
    case VulkanPresentPacingPolicy::PresentWait: return "PresentWait";
    case VulkanPresentPacingPolicy::JustInTime: return "JustInTime";
    case VulkanPresentPacingPolicy::JustInTimeFifoLatestReady: return "JustInTimeFifoLatestReady";
    }
    return "TelemetryOnly";
}

const char* VulkanPacingAuthorityName(VulkanPacingAuthority authority) noexcept
{
    switch (authority)
    {
    case VulkanPacingAuthority::GenericHost: return "GenericHost";
    case VulkanPacingAuthority::NvidiaReflex: return "NvidiaReflex";
    case VulkanPacingAuthority::AmdAntiLag2: return "AmdAntiLag2";
    case VulkanPacingAuthority::GenericPresentTiming: return "GenericPresentTiming";
    }
    return "GenericHost";
}

bool VulkanPresentPacer::Initialize(const VulkanDevice& device, VkSurfaceKHR surface)
{
    Shutdown();
    Device = &device;
    Surface = surface;
    Caps2Available = device.InstanceFns().GetPhysicalDeviceSurfaceCapabilities2KHR != nullptr;
    PresentId2Device = HasEnabledExtension(device, VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
    PresentWait2Device = HasEnabledExtension(device, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME)
        && device.Fns().WaitForPresent2KHR;
    PresentTimingDevice = HasEnabledExtension(device, VK_EXT_PRESENT_TIMING_EXTENSION_NAME)
        && device.Fns().GetSwapchainTimingPropertiesEXT
        && device.Fns().GetPastPresentationTimingEXT;
    LatestReadyDevice = HasEnabledExtension(
        device, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
    WaitRuntimeEnabled = PresentWait2Device;
    return Device && Surface != VK_NULL_HANDLE;
}

void VulkanPresentPacer::Shutdown() noexcept
{
    OnSwapchainDestroyed();
    Device = nullptr;
    Surface = VK_NULL_HANDLE;
    Caps2Available = false;
    PresentId2Device = false;
    PresentWait2Device = false;
    PresentTimingDevice = false;
    LatestReadyDevice = false;
    PresentId2Surface = false;
    PresentWait2Surface = false;
    PresentTimingSurface = false;
    PresentTimingRuntimeEnabled = false;
    PresentTimingRelative = false;
    PresentStageQueries = 0;
    WaitRuntimeEnabled = false;
    WaitDisabledReason.clear();
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
}

void VulkanPresentPacer::SetPolicy(int value) noexcept
{
    const int first = static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly);
    const int last = static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady);
    Policy.store(std::clamp(value, first, last), std::memory_order_release);
}

VulkanPresentPacingPolicy VulkanPresentPacer::GetPolicy() const noexcept
{
    return static_cast<VulkanPresentPacingPolicy>(Policy.load(std::memory_order_acquire));
}

bool VulkanPresentPacer::QuerySurfaceCapabilities(VkSurfaceCapabilitiesKHR& capabilities)
{
    if (!Device || Surface == VK_NULL_HANDLE)
        return false;

    PresentId2Surface = false;
    PresentWait2Surface = false;
    PresentTimingSurface = false;
    PresentTimingRuntimeEnabled = false;
    PresentTimingRelative = false;
    PresentStageQueries = 0;

    const Vk::InstanceDispatch& fns = Device->InstanceFns();
    if (Caps2Available)
    {
        VkSurfaceCapabilitiesPresentId2KHR id2{};
        id2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
        VkSurfaceCapabilitiesPresentWait2KHR wait2{};
        wait2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR;
        VkPresentTimingSurfaceCapabilitiesEXT timing{};
        timing.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT;

        void* chain = nullptr;
        if (PresentTimingDevice)
        {
            timing.pNext = chain;
            chain = &timing;
        }
        if (PresentWait2Device)
        {
            wait2.pNext = chain;
            chain = &wait2;
        }
        if (PresentId2Device)
        {
            id2.pNext = chain;
            chain = &id2;
        }

        VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        surfaceInfo.surface = Surface;
        VkSurfaceCapabilities2KHR caps2{};
        caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
        caps2.pNext = chain;
        const VkResult result = fns.GetPhysicalDeviceSurfaceCapabilities2KHR(
            Device->GetPhysicalDevice(), &surfaceInfo, &caps2);
        if (result == VK_SUCCESS)
        {
            capabilities = caps2.surfaceCapabilities;
            PresentId2Surface = PresentId2Device && id2.presentId2Supported == VK_TRUE;
            PresentWait2Surface = PresentWait2Device && PresentId2Surface
                && wait2.presentWait2Supported == VK_TRUE;
            PresentTimingSurface = PresentTimingDevice && PresentId2Surface
                && timing.presentTimingSupported == VK_TRUE;
            PresentTimingRuntimeEnabled = PresentTimingSurface;
            PresentTimingRelative = PresentTimingSurface
                && timing.presentAtRelativeTimeSupported == VK_TRUE;
            PresentStageQueries = PresentTimingSurface ? timing.presentStageQueries : 0;
            WaitRuntimeEnabled = PresentWait2Surface;
            return true;
        }

        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkGetPhysicalDeviceSurfaceCapabilities2KHR failed (%s); "
            "modern present pacing disabled for this surface\n",
            Vk::FormatResult(result).c_str());
    }

    const VkResult legacy = fns.GetPhysicalDeviceSurfaceCapabilitiesKHR(
        Device->GetPhysicalDevice(), Surface, &capabilities);
    return legacy == VK_SUCCESS;
}

VkSwapchainCreateFlagsKHR VulkanPresentPacer::GetSwapchainCreateFlags() const noexcept
{
    VkSwapchainCreateFlagsKHR flags = 0;
    if (PresentId2Surface)
        flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR;
    if (PresentWait2Surface)
        flags |= VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
    if (PresentTimingSurface)
        flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
    return flags;
}

bool VulkanPresentPacer::ShouldUseFifoLatestReady() const noexcept
{
    return GetPolicy() == VulkanPresentPacingPolicy::JustInTimeFifoLatestReady
        && PresentTimingRuntimeEnabled && PresentId2Surface && LatestReadyDevice;
}

void VulkanPresentPacer::OnSwapchainCreated(
    VkSwapchainKHR swapchain, VkPresentModeKHR presentMode)
{
    Swapchain = swapchain;
    PresentMode = presentMode;
    LastSubmittedId = 0;
    LastPresentedId = 0;
    LastWaitedId = 0;
    TimingReportCountdown = 0;
    WaitTimeouts = 0;
    WaitRuntimeEnabled = PresentWait2Surface;
    PresentTimingRuntimeEnabled = PresentTimingSurface;
    WaitDisabledReason.clear();

    if (PresentTimingSurface && Device->Fns().SetSwapchainPresentTimingQueueSizeEXT)
    {
        const VkResult sizeResult = Device->Fns().SetSwapchainPresentTimingQueueSizeEXT(
            Device->GetHandle(), Swapchain, 16);
        if (sizeResult != VK_SUCCESS)
        {
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] present timing queue sizing failed: %s; telemetry remains optional\n",
                Vk::FormatResult(sizeResult).c_str());
        }
        RefreshTimingProperties();
    }

    LogState("swapchain ready:");
}

void VulkanPresentPacer::OnSwapchainDestroyed() noexcept
{
    Swapchain = VK_NULL_HANDLE;
    RefreshDurationNs = 0;
    RefreshIntervalNs = 0;
    LastSubmittedId = 0;
    LastPresentedId = 0;
    LastWaitedId = 0;
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
}

void VulkanPresentPacer::SelectAuthority(
    bool reflexActive, bool antiLagActive, bool normalSpeed) noexcept
{
    VulkanPacingAuthority selected = VulkanPacingAuthority::GenericHost;
    if (reflexActive)
        selected = VulkanPacingAuthority::NvidiaReflex;
    else if (antiLagActive)
        selected = VulkanPacingAuthority::AmdAntiLag2;
    else if (normalSpeed
        && GetPolicy() != VulkanPresentPacingPolicy::TelemetryOnly
        && WaitRuntimeEnabled && PresentId2Surface && Swapchain != VK_NULL_HANDLE)
    {
        selected = VulkanPacingAuthority::GenericPresentTiming;
    }
    Authority.store(static_cast<int>(selected), std::memory_order_release);
}

bool VulkanPresentPacer::BeginFrame(
    bool reflexActive, bool antiLagActive, bool normalSpeed)
{
    SelectAuthority(reflexActive, antiLagActive, normalSpeed);
    // Telemetry-only is the safe default: collect periodic timing reports even
    // though no behavioural wait owns pacing.
    ReportPastTiming();
    if (GetAuthority() != VulkanPacingAuthority::GenericPresentTiming)
        return false;

    // A skipped frame must not wait for the same present twice. Only a present
    // that QueuePresentKHR actually accepted advances LastPresentedId.
    if (LastPresentedId == 0 || LastPresentedId == LastWaitedId)
        return false;

    VkPresentWait2InfoKHR wait{};
    wait.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
    wait.presentId = LastPresentedId;
    wait.timeout = GetPolicy() == VulkanPresentPacingPolicy::PresentWait
        ? MaxPresentWaitNs
        : (RefreshDurationNs > 0
            ? std::min(MaxPresentWaitNs, std::max<u64>(250'000, RefreshDurationNs / 4))
            : MaxPresentWaitNs);

    const VkResult result = Device->Fns().WaitForPresent2KHR(
        Device->GetHandle(), Swapchain, &wait);
    LastWaitedId = LastPresentedId;
    if (result == VK_SUCCESS)
        return false;
    if (result == VK_TIMEOUT)
    {
        ++WaitTimeouts;
        return false;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return true;

    DisableWait(Vk::FormatResult(result).c_str());
    return false;
}

u64 VulkanPresentPacer::PreparePresent(
    VkPresentInfoKHR& present, u64 preferredId, PresentMetadata& metadata)
{
    metadata = PresentMetadata{};
    if (!PresentId2Surface || Swapchain == VK_NULL_HANDLE)
        return 0;

    metadata.LogicalId = preferredId != 0 ? preferredId : LastSubmittedId + 1;
    LastSubmittedId = metadata.LogicalId;

    if (PresentTimingRuntimeEnabled)
    {
        metadata.Timing.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
        metadata.Timing.presentStageQueries = PresentStageQueries;
        // flags=0 means telemetry-only metadata: no absolute/relative target is
        // requested, so targetTime and targetTimeDomainPresentStage stay zero.
        metadata.Timings.sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT;
        metadata.Timings.swapchainCount = 1;
        metadata.Timings.pTimingInfos = &metadata.Timing;
        metadata.Timings.pNext = present.pNext;
        present.pNext = &metadata.Timings;
        metadata.TimingAttached = true;
    }

    // Keep ID2 outermost. If timing metadata makes vkQueuePresentKHR reject
    // the operation, retry preparation can splice only that node out while
    // preserving Reflex's outer VkPresentIdKHR and any pre-existing chain.
    metadata.Id2.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
    metadata.Id2.swapchainCount = 1;
    metadata.Id2.pPresentIds = &metadata.LogicalId;
    metadata.Id2.pNext = present.pNext;
    present.pNext = &metadata.Id2;
    return metadata.LogicalId;
}

bool VulkanPresentPacer::PrepareRetryWithoutTiming(
    VkResult result, PresentMetadata& metadata)
{
    if (result != VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT || !metadata.TimingAttached)
        return false;

    // The failed call did not enqueue the image or consume its present ID.
    // Remove only VkPresentTimingsInfoEXT and let the caller retry once with
    // the same wait semaphore, image and logical ID.
    metadata.Id2.pNext = metadata.Timings.pNext;
    metadata.TimingAttached = false;
    PresentTimingRuntimeEnabled = false;
    WaitDisabledReason = "present timing results queue full; timing metadata disabled";
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] %s; retrying present without optional timing metadata\n",
        WaitDisabledReason.c_str());
    return true;
}

void VulkanPresentPacer::NotifyPresentResult(VkResult result, u64 logicalId) noexcept
{
    if (logicalId != 0 && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR))
        LastPresentedId = logicalId;
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LastPresentedId = 0;
        LastWaitedId = 0;
    }
}

void VulkanPresentPacer::RefreshTimingProperties()
{
    if (!PresentTimingSurface || Swapchain == VK_NULL_HANDLE)
        return;
    VkSwapchainTimingPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
    u64 counter = 0;
    const VkResult result = Device->Fns().GetSwapchainTimingPropertiesEXT(
        Device->GetHandle(), Swapchain, &properties, &counter);
    if (result == VK_SUCCESS)
    {
        RefreshDurationNs = properties.refreshDuration;
        RefreshIntervalNs = properties.refreshInterval;
    }
}

void VulkanPresentPacer::ReportPastTiming()
{
    if (!PresentTimingRuntimeEnabled || Swapchain == VK_NULL_HANDLE)
        return;

    // The timing-results queue is finite. Drain it every frame even in release
    // builds; otherwise telemetry-only mode can eventually reject a present
    // with VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT.
    std::array<std::array<VkPresentStageTimeEXT, 4>, 16> stages{};
    std::array<VkPastPresentationTimingEXT, 16> reports{};
    for (std::size_t i = 0; i < reports.size(); ++i)
    {
        VkPastPresentationTimingEXT& report = reports[i];
        report.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
        report.presentStageCount = static_cast<u32>(stages[i].size());
        report.pPresentStages = stages[i].data();
    }

    VkPastPresentationTimingInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT;
    info.flags = VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT;
    info.swapchain = Swapchain;
    VkPastPresentationTimingPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT;
    properties.presentationTimingCount = static_cast<u32>(reports.size());
    properties.pPresentationTimings = reports.data();
    const VkResult result = Device->Fns().GetPastPresentationTimingEXT(
        Device->GetHandle(), &info, &properties);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        PresentTimingRuntimeEnabled = false;
        WaitDisabledReason = "present timing report query failed: " + Vk::FormatResult(result);
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] %s; timing metadata disabled\n", WaitDisabledReason.c_str());
        return;
    }

    if (properties.presentationTimingCount > 0)
    {
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        if (++TimingReportCountdown < TimingLogPeriodFrames)
            return;
        TimingReportCountdown = 0;
        const VkPastPresentationTimingEXT& latest = reports[
            std::min<std::size_t>(properties.presentationTimingCount, reports.size()) - 1];
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] present timing: id=%llu target=%llu complete=%s stages=%u "
            "refreshDurationNs=%llu refreshIntervalNs=%llu waitTimeouts=%u\n",
            static_cast<unsigned long long>(latest.presentId),
            static_cast<unsigned long long>(latest.targetTime),
            latest.reportComplete ? "yes" : "no",
            latest.presentStageCount,
            static_cast<unsigned long long>(RefreshDurationNs),
            static_cast<unsigned long long>(RefreshIntervalNs),
            WaitTimeouts);
#endif
    }
}

void VulkanPresentPacer::DisableWait(const char* reason)
{
    WaitRuntimeEnabled = false;
    WaitDisabledReason = reason ? reason : "runtime failure";
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] generic present wait disabled: %s; falling back to host pacing\n",
        WaitDisabledReason.c_str());
}

void VulkanPresentPacer::LogState(const char* context) const
{
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] %s generic present pacing: policy=%s authority=%s caps2=%s "
        "present-id2=%s present-wait2=%s present-timing=%s fifo-latest-ready=%s "
        "presentMode=%d reason=%s\n",
        context ? context : "state:",
        VulkanPresentPacingPolicyName(GetPolicy()),
        VulkanPacingAuthorityName(GetAuthority()),
        Caps2Available ? "yes" : "no",
        PresentId2Surface ? "yes" : "no",
        PresentWait2Surface ? "yes" : "no",
        PresentTimingRuntimeEnabled ? "yes" : "no",
        (LatestReadyDevice && PresentTimingRuntimeEnabled) ? "yes" : "no",
        static_cast<int>(PresentMode),
        WaitDisabledReason.empty() ? "available capabilities are optional" : WaitDisabledReason.c_str());
}

VulkanPacingAuthority VulkanPresentPacer::GetAuthority() const noexcept
{
    return static_cast<VulkanPacingAuthority>(Authority.load(std::memory_order_acquire));
}

} // namespace melonDS

#endif
