/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
*/

#include "VulkanPresentPacer.h"
#include "VulkanPerf.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace melonDS
{

namespace
{

constexpr u64 MaxPresentWaitNs = 2'000'000; // A driver stall must never hang input.
constexpr u64 MinPresentWaitNs = 250'000;
constexpr u32 TimingLogPeriodFrames = 600;

// Optional timing-results queue sizing. A report holds its slot until the
// presentation engine completes it, which can span several refreshes, so the
// floor covers a normal swapchain and the per-image term covers deeper ones.
// The ceiling exists because a queue the emulator can never fill is just
// driver memory it will not use.
constexpr u32 MinTimingQueueSize = 16;
constexpr u32 MaxTimingQueueSize = 64;
constexpr u32 TimingQueueImageFactor = 4;

constexpr u32 TimingQueueSizeFor(u32 imageCount) noexcept
{
    const u32 scaled = imageCount * TimingQueueImageFactor;
    return std::min(MaxTimingQueueSize, std::max(MinTimingQueueSize, scaled));
}

// One present stage the target time is expressed against, most display-visible
// first. Requesting a stage the surface does not report is invalid usage, so
// the first supported entry wins and an empty intersection means no target
// scheduling at all.
constexpr std::array<VkPresentStageFlagsEXT, 4> TargetStagePreference{
    VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT,
    VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT,
    VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT,
    VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT,
};

bool HasEnabledExtension(const VulkanDevice& device, const char* name) noexcept
{
    return Vk::ExtensionEnabled(device.GetEnabledExtensions(), name);
}

// Target presentation time semantics are defined for the FIFO family. In
// IMMEDIATE or MAILBOX the engine presents as soon as it can, so asking it to
// hold an image until a deadline has no defined meaning.
bool IsFifoFamily(VkPresentModeKHR mode) noexcept
{
    return mode == VK_PRESENT_MODE_FIFO_KHR
        || mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR
        || mode == VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
}

u64 GoogleMonotonicTimeNs() noexcept
{
#if defined(__APPLE__)
    // MoltenVK implements VK_GOOGLE_display_timing on mach_absolute_time(),
    // which pauses while the Mac sleeps. libc++ steady_clock uses the
    // continuous clock and includes sleep, so its epoch can be days ahead.
    // Convert the exact clock MoltenVK reports into the extension's ns unit.
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t value{};
        mach_timebase_info(&value);
        return value;
    }();
    const __uint128_t scaled = static_cast<__uint128_t>(mach_absolute_time())
        * timebase.numer;
    return static_cast<u64>(scaled / timebase.denom);
#else
    using Clock = std::chrono::steady_clock;
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now().time_since_epoch()).count());
#endif
}

const char* PresentLifecycleRouteName(VulkanPacerBeginResult result) noexcept
{
    switch (result)
    {
    case VulkanPacerBeginResult::SwapchainOutOfDate: return "SwapchainOutOfDate";
    case VulkanPacerBeginResult::SwapchainSuboptimal: return "SwapchainSuboptimal";
    case VulkanPacerBeginResult::DeviceLost: return "DeviceLost";
    case VulkanPacerBeginResult::SurfaceLost: return "SurfaceLost";
    case VulkanPacerBeginResult::Continue: return "Continue";
    }
    return "Continue";
}

const char* PresentLifecycleActionName(VulkanPacerBeginResult result) noexcept
{
    const VulkanPacerBeginAction action = VulkanPacerActionFor(result);
    if (action.RebuildSwapchain)
        return "RebuildSwapchain";
    if (action.FailRenderer)
        return "FailRenderer";
    return "Continue";
}

void LogPresentLifecycleRoute(
    const char* query, VkResult result, VulkanPacerBeginResult route,
    Platform::LogLevel level = Platform::LogLevel::Error)
{
    Platform::Log(level,
        "[Vulkan] present_timing query=%s result=%s route=%s action=%s\n",
        query, Vk::FormatResult(result).c_str(), PresentLifecycleRouteName(route),
        PresentLifecycleActionName(route));
}

// Both name helpers only feed the developer state-change log.
[[maybe_unused]] const char* PresentStageName(VkPresentStageFlagsEXT stage) noexcept
{
    switch (stage)
    {
    case VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT: return "IMAGE_FIRST_PIXEL_VISIBLE";
    case VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT: return "IMAGE_FIRST_PIXEL_OUT";
    case VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT: return "REQUEST_DEQUEUED";
    case VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT: return "QUEUE_OPERATIONS_END";
    default: return "none";
    }
}

// Deliberately an if-chain, not a switch: on an SDK older than
// VK_EXT_present_timing the two swapchain domains come from
// VulkanModernPresentCompat.h as casts rather than enumerators, and a switch
// over them warns about case values outside the enumerated type.
[[maybe_unused]] const char* TimeDomainName(VkTimeDomainKHR domain) noexcept
{
    if (domain == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT) return "SWAPCHAIN_LOCAL";
    if (domain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) return "PRESENT_STAGE_LOCAL";
    if (domain == VK_TIME_DOMAIN_DEVICE_KHR) return "DEVICE";
    if (domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) return "CLOCK_MONOTONIC";
    if (domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR) return "CLOCK_MONOTONIC_RAW";
    if (domain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR)
        return "QUERY_PERFORMANCE_COUNTER";
    return "unknown";
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
    case VulkanPresentPacingPolicy::PresenterOneFrameBudget:
        return "PresenterOneFrameBudget";
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

const char* VulkanTargetSchedulingModeName(VulkanTargetSchedulingMode mode) noexcept
{
    switch (mode)
    {
    case VulkanTargetSchedulingMode::None: return "none";
    case VulkanTargetSchedulingMode::Absolute: return "absolute";
    case VulkanTargetSchedulingMode::Relative: return "relative";
    }
    return "none";
}

const char* VulkanPresentTimingBackendName(VulkanPresentTimingBackend backend) noexcept
{
    switch (backend)
    {
    case VulkanPresentTimingBackend::None: return "none";
    case VulkanPresentTimingBackend::ExtPresentTiming: return "ext_present_timing";
    case VulkanPresentTimingBackend::GoogleDisplayTiming: return "google_display_timing";
    }
    return "none";
}

const char* VulkanRefreshDynamicsName(VulkanRefreshDynamics dynamics) noexcept
{
    switch (dynamics)
    {
    case VulkanRefreshDynamics::Unknown: return "unknown";
    case VulkanRefreshDynamics::VariableRefresh: return "VRR";
    case VulkanRefreshDynamics::FixedRefresh: return "FRR";
    case VulkanRefreshDynamics::DynamicRefresh: return "dynamic";
    }
    return "unknown";
}

const char* VulkanJitFallbackReasonName(VulkanJitFallbackReason reason) noexcept
{
    switch (reason)
    {
    case VulkanJitFallbackReason::None: return "none";
    case VulkanJitFallbackReason::TelemetryOnlyPolicy: return "telemetry-only policy";
    case VulkanJitFallbackReason::PresentWaitPolicyNoTarget:
        return "present-wait policy requests no target time";
    case VulkanJitFallbackReason::VendorLatencyApiOwnsPacing:
        return "vendor latency API owns pacing";
    case VulkanJitFallbackReason::NotNormalSpeed: return "not normal speed";
    case VulkanJitFallbackReason::PresentId2Unsupported: return "present_id2 unsupported";
    case VulkanJitFallbackReason::PresentWait2Unsupported:
        return "present_wait2 unsupported (optional bounded wait only)";
    case VulkanJitFallbackReason::PresentTimingUnsupported: return "present timing unsupported";
    case VulkanJitFallbackReason::NoTargetTimingModeDevice:
        return "device supports neither absolute nor relative target timing";
    case VulkanJitFallbackReason::NoTargetTimingModeSurface:
        return "surface supports neither absolute nor relative target timing";
    case VulkanJitFallbackReason::NonFifoPresentMode: return "present mode is not FIFO";
    case VulkanJitFallbackReason::NoFrameInterval: return "no emulator frame interval";
    case VulkanJitFallbackReason::TimingPropertiesNotReady: return "timing properties not ready";
    case VulkanJitFallbackReason::TimeDomainsNotReady: return "time domains not ready";
    case VulkanJitFallbackReason::NoValidTargetStage: return "no valid target stage";
    case VulkanJitFallbackReason::BootstrapWaitingForFeedback:
        return "bootstrap waiting for feedback";
    case VulkanJitFallbackReason::BootstrapWaitingForFirstPresent:
        return "bootstrap waiting for the first present";
    case VulkanJitFallbackReason::DomainChanged: return "domain changed";
    case VulkanJitFallbackReason::TimingQueuePressure: return "timing queue pressure";
    case VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation:
        return "frame decision invalidated by swapchain recreation";
    case VulkanJitFallbackReason::TimingQueryFailed: return "timing query failed";
    }
    return "none";
}

bool VulkanPresentPacer::Initialize(const VulkanDevice& device, VkSurfaceKHR surface)
{
    const Vk::InstanceDispatch& instanceFns = device.InstanceFns();
    const Vk::DeviceDispatch& deviceFns = device.Fns();

    VulkanPresentPacerDispatch dispatch;
    dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR =
        instanceFns.GetPhysicalDeviceSurfaceCapabilities2KHR;
    dispatch.GetPhysicalDeviceSurfaceCapabilitiesKHR =
        instanceFns.GetPhysicalDeviceSurfaceCapabilitiesKHR;
    dispatch.SetSwapchainPresentTimingQueueSizeEXT =
        deviceFns.SetSwapchainPresentTimingQueueSizeEXT;
    dispatch.WaitForPresentKHR = deviceFns.WaitForPresentKHR;
    dispatch.WaitForPresent2KHR = deviceFns.WaitForPresent2KHR;
    dispatch.GetSwapchainTimingPropertiesEXT = deviceFns.GetSwapchainTimingPropertiesEXT;
    dispatch.GetSwapchainTimeDomainPropertiesEXT =
        deviceFns.GetSwapchainTimeDomainPropertiesEXT;
    dispatch.GetPastPresentationTimingEXT = deviceFns.GetPastPresentationTimingEXT;
    dispatch.GetRefreshCycleDurationGOOGLE = deviceFns.GetRefreshCycleDurationGOOGLE;
    dispatch.GetPastPresentationTimingGOOGLE = deviceFns.GetPastPresentationTimingGOOGLE;

    VulkanPresentPacerInitInfo info;
    info.Device = device.GetHandle();
    info.PhysicalDevice = device.GetPhysicalDevice();
    info.PresentIdExtensionEnabled =
        HasEnabledExtension(device, VK_KHR_PRESENT_ID_EXTENSION_NAME);
    info.PresentWaitLegacyExtensionEnabled =
        HasEnabledExtension(device, VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    info.PresentId2ExtensionEnabled =
        HasEnabledExtension(device, VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
    info.PresentWait2ExtensionEnabled =
        HasEnabledExtension(device, VK_KHR_PRESENT_WAIT_2_EXTENSION_NAME);
    info.PresentTimingExtensionEnabled =
        HasEnabledExtension(device, VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
    info.GoogleDisplayTimingExtensionEnabled =
        HasEnabledExtension(device, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    info.GoogleDisplayTimingFeatureEnabled =
        device.GetPresentTimingFeatures().GoogleDisplayTiming;
    info.PresentAtAbsoluteTimeFeatureEnabled =
        device.GetPresentTimingFeatures().PresentAtAbsoluteTime;
    info.PresentAtRelativeTimeFeatureEnabled =
        device.GetPresentTimingFeatures().PresentAtRelativeTime;
    info.LatestReadyExtensionEnabled = HasEnabledExtension(
        device, VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME);
    return InitializeCommon(dispatch, info, surface);
}

#if defined(MELONPRIME_VULKAN_PRESENT_PACER_TESTING)
bool VulkanPresentPacer::InitializeForTesting(
    const VulkanPresentPacerDispatch& dispatch,
    const VulkanPresentPacerInitInfo& info,
    VkSurfaceKHR surface)
{
    return InitializeCommon(dispatch, info, surface);
}
#endif

bool VulkanPresentPacer::InitializeCommon(
    const VulkanPresentPacerDispatch& dispatch,
    const VulkanPresentPacerInitInfo& info,
    VkSurfaceKHR surface)
{
    Shutdown();
    Dispatch = dispatch;
    DeviceHandle = info.Device;
    PhysicalDeviceHandle = info.PhysicalDevice;
    Surface = surface;
    Caps2Available = Dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR != nullptr;
    PresentIdDevice = info.PresentIdExtensionEnabled;
    PresentWaitLegacyDevice = info.PresentWaitLegacyExtensionEnabled
        && PresentIdDevice && Dispatch.WaitForPresentKHR != nullptr;
    PresentId2Device = info.PresentId2ExtensionEnabled;
    PresentWait2Device = info.PresentWait2ExtensionEnabled
        && Dispatch.WaitForPresent2KHR;
    PresentTimingDevice = info.PresentTimingExtensionEnabled
        && Dispatch.GetSwapchainTimingPropertiesEXT
        && Dispatch.GetPastPresentationTimingEXT;
    GoogleDisplayTimingDevice =
        info.GoogleDisplayTimingExtensionEnabled
        && info.GoogleDisplayTimingFeatureEnabled
        && Dispatch.GetPastPresentationTimingGOOGLE
        && Dispatch.GetRefreshCycleDurationGOOGLE;
    GoogleDisplayTimingRuntimeEnabled = GoogleDisplayTimingDevice;
    // Absolute target scheduling additionally needs the feature bit that
    // vkCreateDevice enabled and the entry point that enumerates the swapchain's
    // time domains; without the latter there is no legal timeDomainId to name.
    TimeDomainQueryAvailable = Dispatch.GetSwapchainTimeDomainPropertiesEXT != nullptr;
    AbsoluteTimingDevice = PresentTimingDevice && TimeDomainQueryAvailable
        && info.PresentAtAbsoluteTimeFeatureEnabled;
    // A relative target is a duration and needs no clock of its own, but the
    // metadata carrying it still has a mandatory timeDomainId, so the
    // time-domain query is required for this mode too.
    RelativeTimingDevice = PresentTimingDevice && TimeDomainQueryAvailable
        && info.PresentAtRelativeTimeFeatureEnabled;
    LatestReadyDevice = info.LatestReadyExtensionEnabled;
    PresentWait2RuntimeEnabled = PresentWait2Device;
    PresentWaitLegacyRuntimeEnabled = PresentWaitLegacyDevice;
    WaitRuntimeEnabled = PresentWait2RuntimeEnabled || PresentWaitLegacyRuntimeEnabled;
    return DeviceHandle != VK_NULL_HANDLE && Surface != VK_NULL_HANDLE;
}

void VulkanPresentPacer::Shutdown() noexcept
{
    OnSwapchainDestroyed();
    Dispatch = {};
    DeviceHandle = VK_NULL_HANDLE;
    PhysicalDeviceHandle = VK_NULL_HANDLE;
    Surface = VK_NULL_HANDLE;
    Caps2Available = false;
    PresentIdDevice = false;
    PresentWaitLegacyDevice = false;
    PresentId2Device = false;
    PresentWait2Device = false;
    PresentTimingDevice = false;
    GoogleDisplayTimingDevice = false;
    GoogleDisplayTimingRuntimeEnabled = false;
    GoogleRefreshDurationReady = false;
    AbsoluteTimingDevice = false;
    RelativeTimingDevice = false;
    LatestReadyDevice = false;
    TimeDomainQueryAvailable = false;
    PresentId2Surface = false;
    PresentWaitLegacySurface = false;
    PresentWait2Surface = false;
    PresentTimingSurface = false;
    PresentTimingRelativeSurface = false;
    PresentTimingAbsoluteSurface = false;
    TimingMetadataEnabled = false;
    TimingResultsQueryEnabled = false;
    PresentStageQueries = 0;
    TargetPresentStage = 0;
    TargetSchedulingLifecycleFailed = false;
    PresentWaitLegacyRuntimeEnabled = false;
    PresentWait2RuntimeEnabled = false;
    WaitRuntimeEnabled = false;
    WaitDisabledReason.clear();
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
}

void VulkanPresentPacer::SetPolicy(int value) noexcept
{
    const int first = static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly);
    const int last = static_cast<int>(VulkanPresentPacingPolicy::PresenterOneFrameBudget);
    Policy.store(std::clamp(value, first, last), std::memory_order_release);
}

VulkanPresentPacingPolicy VulkanPresentPacer::GetPolicy() const noexcept
{
    return static_cast<VulkanPresentPacingPolicy>(Policy.load(std::memory_order_acquire));
}

bool VulkanPresentPacer::QuerySurfaceCapabilities(VkSurfaceCapabilitiesKHR& capabilities)
{
    if (DeviceHandle == VK_NULL_HANDLE || Surface == VK_NULL_HANDLE)
        return false;

    PresentId2Surface = false;
    PresentWaitLegacySurface = false;
    PresentWait2Surface = false;
    PresentTimingSurface = false;
    PresentTimingRelativeSurface = false;
    PresentTimingAbsoluteSurface = false;
    TimingMetadataEnabled = false;
    TimingResultsQueryEnabled = false;
    PresentStageQueries = 0;
    TargetPresentStage = 0;

    const VulkanPresentPacerDispatch& fns = Dispatch;
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
            PhysicalDeviceHandle, &surfaceInfo, &caps2);
        if (result == VK_SUCCESS)
        {
            capabilities = caps2.surfaceCapabilities;
            PresentId2Surface = PresentId2Device && id2.presentId2Supported == VK_TRUE;
            // VK_KHR_present_wait has no surface capability structure. A
            // successful surface query is the lifecycle boundary at which a
            // device-level legacy wait can be used for this swapchain.
            PresentWaitLegacySurface = PresentWaitLegacyDevice;
            PresentWait2Surface = PresentWait2Device && PresentId2Surface
                && wait2.presentWait2Supported == VK_TRUE;
            PresentTimingSurface = PresentTimingDevice && PresentId2Surface
                && timing.presentTimingSupported == VK_TRUE;
            TimingMetadataEnabled = PresentTimingSurface;
            TimingResultsQueryEnabled = PresentTimingSurface;
            PresentTimingRelativeSurface = PresentTimingSurface
                && timing.presentAtRelativeTimeSupported == VK_TRUE;
            PresentTimingAbsoluteSurface = PresentTimingSurface
                && timing.presentAtAbsoluteTimeSupported == VK_TRUE;
            PresentStageQueries = PresentTimingSurface ? timing.presentStageQueries : 0;
            // The stage the targets are expressed against is a property of the
            // surface, so it is known here -- before any swapchain exists.
            SelectTargetPresentStage();
            PresentWait2RuntimeEnabled = PresentWait2Surface && PresentWait2Device;
            PresentWaitLegacyRuntimeEnabled = PresentWaitLegacySurface
                && PresentWaitLegacyDevice;
            WaitRuntimeEnabled = PresentWait2RuntimeEnabled
                || PresentWaitLegacyRuntimeEnabled;
            return true;
        }

        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkGetPhysicalDeviceSurfaceCapabilities2KHR failed (%s); "
            "modern present pacing disabled for this surface\n",
            Vk::FormatResult(result).c_str());
    }

    const VkResult legacy = fns.GetPhysicalDeviceSurfaceCapabilitiesKHR(
        PhysicalDeviceHandle, Surface, &capabilities);
    if (legacy == VK_SUCCESS)
    {
        PresentWaitLegacySurface = PresentWaitLegacyDevice;
        PresentWaitLegacyRuntimeEnabled = PresentWaitLegacySurface
            && PresentWaitLegacyDevice;
        PresentWait2RuntimeEnabled = false;
        WaitRuntimeEnabled = PresentWaitLegacyRuntimeEnabled;
    }
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
    // FIFO_LATEST_READY earns its place only as the swapchain mode a working
    // target-time scheduler presents into: it lets the engine skip a stale
    // queued image in favour of the one that was scheduled for this refresh.
    // Selecting it on capability alone produced a present mode whose entire
    // point -- time-based selection -- was never exercised.
    //
    // The gate deliberately does NOT include a valid feedback baseline. A
    // baseline can only exist after presenting, and the present mode has to be
    // chosen before the first present; requiring it would make the mode
    // unreachable. What is required is that the scheduler can become active:
    // every capability, entry point and lifecycle step is in place.
    if (GetPolicy() != VulkanPresentPacingPolicy::JustInTimeFifoLatestReady)
        return false;
    // A failed EXT lifecycle must stay sticky for EXT, but it must not block a
    // usable GOOGLE fallback on the same device.
    if (TargetSchedulingLifecycleFailed && !GoogleDisplayTimingRuntimeEnabled)
        return false;
    // Deliberately independent of VK_KHR_present_wait2: the bounded wait is a
    // different mechanism and its absence must not cost this present mode.
    // Build the same target-backend capability matrix used by the per-frame
    // resolver. In particular, GOOGLE does not require present_id2, EXT time
    // domains, or the EXT results queue -- all of those are EXT-only details.
    const VulkanPacingCapabilities caps = BuildCapabilities();
    if (!VulkanTargetCanUseFifoLatestReady(GetPolicy(), caps))
        return false;

    const VulkanPresentTimingBackend backend = SelectVulkanPresentTargetBackend(caps);
    if (backend == VulkanPresentTimingBackend::GoogleDisplayTiming)
        return DeviceHandle != VK_NULL_HANDLE;

    // EXT still needs the time-domain and results-queue entry points because
    // its target metadata is carried by VK_EXT_present_timing.
    const bool absoluteCapable = PresentTimingAbsoluteSurface && AbsoluteTimingDevice;
    const bool relativeCapable = PresentTimingRelativeSurface && RelativeTimingDevice;
    return backend == VulkanPresentTimingBackend::ExtPresentTiming
        && (absoluteCapable || relativeCapable)
        && TimeDomainQueryAvailable
        && DeviceHandle != VK_NULL_HANDLE
        && Dispatch.SetSwapchainPresentTimingQueueSizeEXT != nullptr;
}

VkPresentStageFlagsEXT VulkanPresentPacer::RequestedStageQueries() const noexcept
{
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    // Developer builds want the whole picture: the periodic summary exists to
    // show where a frame actually spent its time inside the presentation engine.
    return PresentStageQueries;
#else
    // Production only needs the one stage the target times are expressed
    // against. Every additional stage is another timestamp the engine has to
    // complete before the report frees its results-queue slot.
    return TargetPresentStage != 0 ? TargetPresentStage : PresentStageQueries;
#endif
}

bool VulkanPresentPacer::ApplyTimingQueueSize(u32 size)
{
    if (DeviceHandle == VK_NULL_HANDLE || Swapchain == VK_NULL_HANDLE
        || !Dispatch.SetSwapchainPresentTimingQueueSizeEXT)
    {
        return false;
    }

    const VkResult result = Dispatch.SetSwapchainPresentTimingQueueSizeEXT(
        DeviceHandle, Swapchain, size);
    if (result != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] present timing queue sizing to %u failed: %s\n",
            size, Vk::FormatResult(result).c_str());
        return false;
    }
    TimingQueueSize = size;
    TimingQueueAllocated = true;
    return true;
}

void VulkanPresentPacer::ResetTimingLifecycle() noexcept
{
    // Nothing about the old swapchain's presentation timeline survives into a
    // new one: neither the refresh properties, the time-domain IDs, the
    // sequence numbering, nor any measured stage time.
    TimingPropertiesCounter = 0;
    TimingPropertiesReady = false;
    TimingPropertiesRetryPending = false;
    RefreshDynamics = VulkanRefreshDynamics::Unknown;
    TimeDomainsCounter = 0;
    TimeDomainsReady = false;
    TimeDomainsEnumerationRetryPending = false;
    TargetTimeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
    TargetTimeDomainId = 0;
    TimingQueueAllocated = false;
    TimingQueueSize = 0;
    TimingQueueRecoveries = 0;
    TimingQueuePressureActive = false;
    TimingQueueRecoveryPending = false;
    OutstandingTimedPresents = 0;
    RefreshDurationNs = 0;
    RefreshIntervalNs = 0;
    LastTargetValueNs = 0;
    LastAppliedTargetMode = VulkanTargetSchedulingMode::None;
    LastTargetMode = VulkanTargetSchedulingMode::None;
    LastRelativeRequest = VulkanRelativeCadence::Request{};
    LastFeedbackId = 0;
    LastFeedbackStageTimeNs = 0;
    TimingModel.Reset();
    TimingModel.ClearTimeDomain();
    GoogleTimingModel.Reset();
    GoogleRefreshDurationReady = false;
    LastDecision = VulkanPacingDecision{};
    DecisionSwapchainGeneration = 0;
    TargetFrameIntervalNs = 0;
    WaitAttemptedThisFrame = false;
    WaitAttemptSwapchainGeneration = 0;
    // A reset has no current policy decision to explain. Keep the baseline
    // neutral; CaptureState() supplies the explicit recreation reason when a
    // stale frame is observed, while BeginFrame() records the live policy
    // reason for the next generation.
    FallbackReason = VulkanJitFallbackReason::None;
    PendingBeginResult = VulkanPacerBeginResult::Continue;
    // The cadence phase belongs to the retired swapchain's refresh grid.
    RelativeCadence.Reset();
    RelativeCadence.Configure(0, 0, 0);
    TargetSchedulingActive.store(false, std::memory_order_release);
}

void VulkanPresentPacer::LatchPendingBeginResult(VulkanPacerBeginResult result) noexcept
{
    if (result == VulkanPacerBeginResult::Continue)
        return;
    // Preserve the first observed lifecycle failure for this swapchain. A
    // later optional query must not overwrite the causal result that will be
    // routed through the presenter on the next BeginFrame().
    PendingBeginResult = VulkanLatchBeginResult(PendingBeginResult, result);
}

void VulkanPresentPacer::OnSwapchainCreated(
    VkSwapchainKHR swapchain, VkPresentModeKHR presentMode, u32 imageCount)
{
    ++SwapchainGeneration;
    ResetTimingLifecycle();
    Swapchain = swapchain;
    PresentMode = presentMode;
    FifoFamilyPresentMode = IsFifoFamily(presentMode);
    LastSubmittedId = 0;
    LastPresentedId = 0;
    LastWaitedId = 0;
    TimingReportCountdown = 0;
    WaitTimeouts = 0;
    TimingQueueFullCount = 0;
    PresentWait2RuntimeEnabled = PresentWait2Surface && PresentWait2Device;
    PresentWaitLegacyRuntimeEnabled = PresentWaitLegacySurface
        && PresentWaitLegacyDevice;
    WaitRuntimeEnabled = PresentWait2RuntimeEnabled
        || PresentWaitLegacyRuntimeEnabled;
    TimingMetadataEnabled = false;
    TimingResultsQueryEnabled = false;
    LoggedFallbackReason = VulkanJitFallbackReason::None;
    LoggedTargetSchedulingActive = false;
    WaitDisabledReason.clear();

    if (PresentTimingSurface)
    {
        // The results queue must exist before any present may request timing:
        // a present with a non-zero presentStageQueries needs a slot to report
        // into. If the initial allocation fails there is no queue at all, so
        // both switches stay off rather than attaching metadata the swapchain
        // cannot service -- and, importantly, no recovery is armed, because the
        // recovery trigger is a drained report that could never arrive.
        // The renderer itself continues; only target-time pacing is lost.
        if (ApplyTimingQueueSize(TimingQueueSizeFor(imageCount)))
        {
            TimingMetadataEnabled = true;
            TimingResultsQueryEnabled = true;
            // Only timing properties may answer VK_NOT_READY before the first
            // present. Time-domain enumeration is a SUCCESS/INCOMPLETE
            // contract; a failure there is a lifecycle result, not bootstrap.
            const VulkanPacerBeginResult timingProperties = RefreshTimingProperties();
            if (timingProperties != VulkanPacerBeginResult::Continue)
                LatchPendingBeginResult(timingProperties);
            if (timingProperties == VulkanPacerBeginResult::Continue)
            {
                const VulkanPacerBeginResult timeDomains = RefreshTimeDomains();
                if (timeDomains != VulkanPacerBeginResult::Continue)
                    LatchPendingBeginResult(timeDomains);
            }
        }
        else
        {
            TimingQueueRecoveryPending = false;
            TargetSchedulingLifecycleFailed = true;
            WaitDisabledReason =
                "present timing results queue could not be allocated; "
                "target-time pacing is off for this swapchain";
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] %s\n", WaitDisabledReason.c_str());
        }
    }
    // Use the same policy-aware backend choice as BeginFrame/ResolveDecision.
    // In a mixed EXT+GOOGLE surface, target policies may choose GOOGLE even
    // while EXT telemetry remains active; eager refresh initialization must not
    // skip that bootstrap merely because PresentTimingSurface is true.
    const VulkanPacingCapabilities lifecycleCaps = BuildCapabilities();
    const VulkanPresentTimingBackend lifecycleBackend =
        SelectVulkanPresentBackendForPolicy(GetPolicy(), lifecycleCaps);
    if (lifecycleBackend == VulkanPresentTimingBackend::GoogleDisplayTiming
        && GoogleDisplayTimingRuntimeEnabled)
    {
        const VulkanPacerBeginResult google = RefreshGoogleTiming();
        if (google != VulkanPacerBeginResult::Continue)
            LatchPendingBeginResult(google);
    }

    LogState("swapchain ready:");
}

void VulkanPresentPacer::OnSwapchainDestroyed() noexcept
{
    Swapchain = VK_NULL_HANDLE;
    ResetTimingLifecycle();
    LastSubmittedId = 0;
    LastPresentedId = 0;
    LastWaitedId = 0;
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
}

VulkanPacingCapabilities VulkanPresentPacer::BuildCapabilities() const noexcept
{
    VulkanPacingCapabilities caps;
    caps.SwapchainValid = Swapchain != VK_NULL_HANDLE;
    caps.PresentId2Surface = PresentId2Surface;
    caps.PresentWait2Surface = PresentWait2Surface;
    caps.PresentWaitRuntimeEnabled = PresentWait2RuntimeEnabled;
    caps.PresentWaitLegacySurface = PresentWaitLegacySurface;
    caps.PresentWaitLegacyRuntimeEnabled = PresentWaitLegacyRuntimeEnabled;
    caps.PresentTimingSurface = PresentTimingSurface;
    caps.TimingMetadataEnabled = TimingMetadataEnabled;
    caps.TimingQueuePressure = TimingQueuePressureActive;
    caps.AbsoluteTimingDevice = AbsoluteTimingDevice;
    caps.RelativeTimingDevice = RelativeTimingDevice;
    caps.RelativeTimingSurface = PresentTimingRelativeSurface;
    caps.AbsoluteTimingSurface = PresentTimingAbsoluteSurface;
    caps.FifoPresentMode = FifoFamilyPresentMode;
    caps.TimingPropertiesReady = TimingPropertiesReady;
    caps.TimeDomainsReady = TimeDomainsReady && TimingModel.HasTimeDomain();
    caps.TargetStageValid = TargetPresentStage != 0;
    caps.FrameIntervalKnown = TargetFrameIntervalNs != 0;
    caps.GoogleDisplayTimingAvailable = GoogleDisplayTimingDevice;
    caps.GoogleDisplayTimingRuntimeEnabled = GoogleDisplayTimingRuntimeEnabled;
    caps.GoogleRefreshDurationReady = GoogleRefreshDurationReady;
    caps.LatestReadyDevice = LatestReadyDevice;
    caps.TargetSchedulingLifecycleFailed = TargetSchedulingLifecycleFailed;
    return caps;
}

VulkanPacingDecision VulkanPresentPacer::ResolveDecision(
    bool reflexActive, bool antiLagActive, bool normalSpeed) const noexcept
{
    return ResolveVulkanPresentPacing(
        GetPolicy(), reflexActive, antiLagActive, normalSpeed, BuildCapabilities());
}

VulkanPacerBeginResult VulkanPresentPacer::BeginFrame(
    bool reflexActive, bool antiLagActive, bool normalSpeed, u64 targetFrameIntervalNs)
{
    if (PendingBeginResult != VulkanPacerBeginResult::Continue)
    {
        const VulkanPacerBeginResult pending = PendingBeginResult;
        PendingBeginResult = VulkanPacerBeginResult::Continue;
        return pending;
    }

    // The emulator's own frame interval, straight from the frame limiter. It is
    // zero for fast-forward, slow motion and unlimited FPS, and that zero is
    // what turns target scheduling off for those modes: the presentation engine
    // must never be handed a cadence the emulator is not running at.
    //
    // Set before the decision is resolved, because it is one of the inputs.
    TargetFrameIntervalNs = normalSpeed ? targetFrameIntervalNs : 0;
    // Permission is resolved below; whether the wait actually ran is only known
    // once every skip condition has been checked.
    WaitAttemptedThisFrame = false;
    WaitAttemptSwapchainGeneration = 0;

    // Google feedback is independent of present_id2 and has no finite queue to
    // size. Poll a fixed stack buffer only while the same policy-aware backend
    // that will resolve this frame selects GOOGLE. Vendor APIs, abnormal speed
    // and Telemetry/target policy differences are handled by the pure helper.
    const VulkanPacingCapabilities frameCaps = BuildCapabilities();
    if (VulkanShouldPollGoogleForFrame(
            GetPolicy(), reflexActive, antiLagActive, normalSpeed, frameCaps))
    {
        const VulkanPacerBeginResult google = ReportGooglePastTiming();
        if (google != VulkanPacerBeginResult::Continue)
            return google;
    }

    // Telemetry-only is the safe default: collect periodic timing reports even
    // though no behavioural wait owns pacing. Draining also feeds the
    // scheduling baseline and carries the property/domain change counters.
    const VulkanPacerBeginResult extTiming = ReportPastTiming();
    if (extTiming != VulkanPacerBeginResult::Continue)
        return extTiming;

    // Retry timing properties after their documented VK_NOT_READY bootstrap,
    // and retry a bounded time-domain enumeration only after VK_INCOMPLETE.
    // Both flags are lifecycle-only, so this is not a per-frame driver query
    // in the steady state.
    if (TimingPropertiesRetryPending && LastPresentedId != 0)
    {
        const VulkanPacerBeginResult timingProperties = RefreshTimingProperties();
        if (timingProperties != VulkanPacerBeginResult::Continue)
            return timingProperties;
    }
    if (TimeDomainsEnumerationRetryPending && LastPresentedId != 0)
    {
        const VulkanPacerBeginResult timeDomains = RefreshTimeDomains();
        if (timeDomains != VulkanPacerBeginResult::Continue)
            return timeDomains;
    }

    const VulkanPacingDecision decision =
        ResolveDecision(reflexActive, antiLagActive, normalSpeed);
    // A mode change restarts the relative cadence. The accumulated fraction
    // describes a phase against one scheduling scheme; carrying it across a
    // switch (or across a spell of vendor-owned pacing, or fast-forward) would
    // apply a stale phase to a fresh cadence.
    if (decision.TargetMode != LastTargetMode)
    {
        RelativeCadence.Reset();
        LastTargetMode = decision.TargetMode;
    }

    LastDecision = decision;
    DecisionSwapchainGeneration = SwapchainGeneration;
    Authority.store(static_cast<int>(decision.Authority), std::memory_order_release);
    FallbackReason = decision.Reason;
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::VulkanPacingAuthority,
        static_cast<u64>(decision.Authority));

    LogTargetSchedulingIfChanged();

    // The bounded wait and target-time scheduling are independent mechanisms:
    // VK_KHR_present_wait2 / VK_KHR_present_wait wait on the *previous*
    // present, while VK_EXT_present_timing schedules *this* one. A driver that
    // exposes only the latter still gets full target-time presentation; it
    // simply skips the wait below.
    if (!decision.BoundedPresentWait)
        return VulkanPacerBeginResult::Continue;

    // A skipped frame must not wait for the same present twice. Only a present
    // that QueuePresentKHR actually accepted advances LastPresentedId.
    if (LastPresentedId == 0 || LastPresentedId == LastWaitedId)
        return VulkanPacerBeginResult::Continue;

    // Prefer the modern wait2 path. If its runtime entry point or surface
    // capability is unavailable, legacy present_wait remains a valid bounded
    // fallback as long as the legacy present-id was attached to this
    // swapchain's presents.
    const bool useWait2 = PresentWait2Surface && PresentWait2RuntimeEnabled
        && Dispatch.WaitForPresent2KHR != nullptr;
    const bool useLegacyWait = !useWait2
        && PresentWaitLegacySurface && PresentWaitLegacyRuntimeEnabled
        && Dispatch.WaitForPresentKHR != nullptr;
    if (!useWait2 && !useLegacyWait)
        return VulkanPacerBeginResult::Continue;

    const VulkanPresentPacingPolicy policy = GetPolicy();
    u64 waitTimeout = 0;
    if (policy == VulkanPresentPacingPolicy::PresenterOneFrameBudget)
    {
        // The strict prototype is allowed to consume the emulator's own
        // frame budget, but the hard cap keeps a broken WSI implementation
        // from freezing the input thread indefinitely.
        const u64 frameBudget = TargetFrameIntervalNs != 0
            ? TargetFrameIntervalNs
            : RefreshDurationNs;
        waitTimeout = VulkanPresenterOneFrameBudgetTimeoutNs(frameBudget);
    }
    else
    {
        waitTimeout = policy == VulkanPresentPacingPolicy::PresentWait
            ? MaxPresentWaitNs
            : (RefreshDurationNs > 0
                ? std::min(MaxPresentWaitNs,
                    std::max(MinPresentWaitNs, RefreshDurationNs / 4))
                : MaxPresentWaitNs);
    }

    WaitAttemptedThisFrame = true;
    WaitAttemptSwapchainGeneration = SwapchainGeneration;
    const bool perfEnabled = VulkanPerf::IsEnabled();
    const VulkanPerf::Clock::time_point waitStart = perfEnabled
        ? VulkanPerf::Clock::now()
        : VulkanPerf::Clock::time_point{};
    VkResult result = VK_ERROR_UNKNOWN;
    if (useWait2)
    {
        VkPresentWait2InfoKHR wait{};
        wait.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
        wait.presentId = LastPresentedId;
        wait.timeout = waitTimeout;
        result = Dispatch.WaitForPresent2KHR(DeviceHandle, Swapchain, &wait);
    }
    else
    {
        result = Dispatch.WaitForPresentKHR(
            DeviceHandle, Swapchain, LastPresentedId, waitTimeout);
    }
    if (perfEnabled)
    {
        const u64 waitNs = static_cast<u64>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(VulkanPerf::Clock::now() - waitStart).count());
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanPreviousPresentWaitCount);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanPreviousPresentWaitNs, waitNs);
    }
    LastWaitedId = LastPresentedId;
    switch (ClassifyVulkanPresentWait2Result(result))
    {
    case VulkanPresentWait2ResultAction::Continue:
        return VulkanPacerBeginResult::Continue;
    case VulkanPresentWait2ResultAction::Timeout:
        ++WaitTimeouts;
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::VulkanPreviousPresentWaitTimeoutCount);
        return VulkanPacerBeginResult::Continue;
    case VulkanPresentWait2ResultAction::SwapchainSuboptimal:
        LogPresentLifecycleRoute(
            useWait2 ? "WaitForPresent2KHR" : "WaitForPresentKHR",
            result, VulkanPacerBeginResult::SwapchainSuboptimal,
            Platform::LogLevel::Warn);
        return VulkanPacerBeginResult::SwapchainSuboptimal;
    case VulkanPresentWait2ResultAction::SwapchainOutOfDate:
        LogPresentLifecycleRoute(
            useWait2 ? "WaitForPresent2KHR" : "WaitForPresentKHR",
            result, VulkanPacerBeginResult::SwapchainOutOfDate,
            Platform::LogLevel::Warn);
        return VulkanPacerBeginResult::SwapchainOutOfDate;
    case VulkanPresentWait2ResultAction::DeviceLost:
        // A lost device is not a stale swapchain, and rebuilding a swapchain on
        // it would just fail again. Report it as its own class so the caller
        // routes it into the existing Vulkan runtime-failure path. The pacer
        // deliberately does NOT call DisableWait() here: downgrading an
        // optional feature would imply the renderer can carry on, which is
        // exactly the wrong conclusion to draw from device loss.
        TargetSchedulingActive.store(false, std::memory_order_release);
        LogPresentLifecycleRoute(
            useWait2 ? "WaitForPresent2KHR" : "WaitForPresentKHR",
            result, VulkanPacerBeginResult::DeviceLost);
        return VulkanPacerBeginResult::DeviceLost;
    case VulkanPresentWait2ResultAction::SurfaceLost:
        TargetSchedulingActive.store(false, std::memory_order_release);
        LogPresentLifecycleRoute(
            useWait2 ? "WaitForPresent2KHR" : "WaitForPresentKHR",
            result, VulkanPacerBeginResult::SurfaceLost);
        return VulkanPacerBeginResult::SurfaceLost;
    case VulkanPresentWait2ResultAction::DisableWait:
        DisableWait(Vk::FormatResult(result).c_str(), !useWait2);
        return VulkanPacerBeginResult::Continue;
    }
    return VulkanPacerBeginResult::Continue;
}

u64 VulkanPresentPacer::EvaluateAbsoluteTargetTime(u64 sequence) noexcept
{
    const u64 target = TimingModel.ComputeTargetTime(sequence, TargetFrameIntervalNs);
    if (target == 0)
    {
        // No baseline yet is the normal bootstrap state right after swapchain
        // creation: present untimed, collect feedback, start scheduling on the
        // frame after the first complete report.
        FallbackReason = VulkanJitFallbackReason::BootstrapWaitingForFeedback;
        if (TimingModel.IsBaselineStale(sequence))
        {
            // The driver stopped reporting long enough that extrapolation is
            // guesswork. Drop the baseline and re-bootstrap from fresh feedback
            // rather than keep projecting from a stale timestamp.
            TimingModel.InvalidateBaseline();
        }
        return 0;
    }

    FallbackReason = VulkanJitFallbackReason::None;
    return target;
}

VulkanRelativeCadence::Request VulkanPresentPacer::EvaluateRelativeTargetDuration() noexcept
{
    // A relative target says "hold the PREVIOUS image at least this long". On a
    // swapchain that has never presented there is no previous image, and the
    // spec has the engine ignore the request. Reporting that as bootstrap
    // rather than silently sending an ignored value keeps the log honest about
    // when scheduling actually became active.
    if (LastPresentedId == 0)
    {
        FallbackReason = VulkanJitFallbackReason::BootstrapWaitingForFirstPresent;
        return VulkanRelativeCadence::Request{};
    }

    // Refresh properties come from the swapchain and the interval from the
    // emulator; re-configuring on every frame is what makes a refresh-rate or
    // TargetFPS change reset the accumulated fraction instead of carrying a
    // phase from one grid onto another.
    RelativeCadence.Configure(RefreshIntervalNs, RefreshDurationNs, TargetFrameIntervalNs);

    const VulkanRelativeCadence::Request request = RelativeCadence.Prepare();
    if (request.DurationNs == 0)
    {
        FallbackReason = VulkanJitFallbackReason::NoFrameInterval;
        return VulkanRelativeCadence::Request{};
    }

    // Kept for the A/B capture so the cadence can be re-derived per present.
    LastRelativeRequest = request;
    FallbackReason = VulkanJitFallbackReason::None;
    return request;
}

VulkanPresentPacer::TargetTimingRequest
    VulkanPresentPacer::EvaluateTargetTiming(u64 sequence) noexcept
{
    // The capability decision was made once at the top of this frame, against
    // the same pure resolver the authority came from. Re-deriving it here is
    // how the two used to drift apart.
    //
    // Note what is absent: VK_KHR_present_wait2. Target-time presentation
    // depends on VK_EXT_present_timing, VK_KHR_present_id2,
    // VK_KHR_get_surface_capabilities2 and VK_KHR_calibrated_timestamps -- not
    // on the previous-present wait. A surface offering only present timing gets
    // scheduling with no wait rather than nothing at all.
    TargetTimingRequest request;
    if (!VulkanFrameDecisionMatchesSwapchain(
        DecisionSwapchainGeneration, SwapchainGeneration))
    {
        FallbackReason =
            VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation;
        return request;
    }
    if (!LastDecision.TargetTimeScheduling)
    {
        FallbackReason = LastDecision.Reason;
        return request;
    }

    switch (LastDecision.TargetMode)
    {
    case VulkanTargetSchedulingMode::Absolute:
    {
        const u64 target = EvaluateAbsoluteTargetTime(sequence);
        if (target == 0)
            return request;
        request.Mode = VulkanTargetSchedulingMode::Absolute;
        request.ValueNs = target;
        // An absolute target names an instant, so asking for the nearest
        // refresh cycle is always meaningful.
        request.Quantized = true;
        return request;
    }
    case VulkanTargetSchedulingMode::Relative:
    {
        const VulkanRelativeCadence::Request cadence = EvaluateRelativeTargetDuration();
        if (cadence.DurationNs == 0)
            return request;
        request.Mode = VulkanTargetSchedulingMode::Relative;
        request.ValueNs = cadence.DurationNs;
        request.Quantized = cadence.Quantized;
        request.Cadence = cadence;
        return request;
    }
    case VulkanTargetSchedulingMode::None:
        break;
    }

    FallbackReason = LastDecision.Reason;
    return request;
}

u64 VulkanPresentPacer::PreparePresent(
    VkPresentInfoKHR& present, u64 preferredId, PresentMetadata& metadata)
{
    metadata = PresentMetadata{};
    if (Swapchain == VK_NULL_HANDLE)
        return 0;

    const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(
        DecisionSwapchainGeneration, SwapchainGeneration);
    // Swapchain recreation can happen after the pre-input BeginFrame() and
    // before this draw-side call. Never let the retired generation's backend
    // or target permission control the first present on the new swapchain.
    const VulkanPresentTimingBackend backend = decisionCurrent
        ? LastDecision.TimingBackend
        : VulkanPresentTimingBackend::None;
    // Attach the legacy ID whenever the selected policy owns a bounded wait,
    // even while wait2 is currently healthy. If wait2 retires at runtime, the
    // next frame can then wait the immediately previous present through the
    // legacy ladder without trying to reuse an uncorrelated ID.
    const bool legacyWaitCurrent = decisionCurrent
        && LastDecision.BoundedPresentWait
        && PresentWaitLegacySurface && PresentWaitLegacyRuntimeEnabled
        && Dispatch.WaitForPresentKHR != nullptr;
    if (!PresentId2Surface && backend != VulkanPresentTimingBackend::GoogleDisplayTiming
        && !legacyWaitCurrent)
        return 0;

    metadata.LogicalId = preferredId != 0 ? preferredId : LastSubmittedId + 1;
    LastSubmittedId = metadata.LogicalId;

    // The logical ID counts emulation frames; the sequence counts accepted
    // presents. Only the latter may be multiplied by the frame interval.
    if (PresentId2Surface)
        metadata.Sequence = TimingModel.BeginPresent(metadata.LogicalId);

    // VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT is returned after the present
    // operation has already enqueued its semaphore waits. Retrying that same
    // image without the waits is required for VUID-03268, but synchronization
    // validation quite reasonably still sees the first call as a present write.
    // Avoid making that retry the normal queue-pressure path: once every slot
    // is represented by an accepted present whose result is still outstanding,
    // stop attaching optional timing metadata before vkQueuePresentKHR is
    // called. The render/present path keeps running, while ReportPastTiming()
    // drains the queue and arms the bounded growth recovery below.
    if (TimingMetadataEnabled && TimingQueueAllocated && TimingQueueSize != 0
        && OutstandingTimedPresents >= TimingQueueSize)
    {
        TimingMetadataEnabled = false;
        TimingQueuePressureActive = true;
        TimingQueueRecoveryPending = true;
        WaitDisabledReason =
            "present timing results queue at capacity; timing metadata paused pending drain";
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] %s\n", WaitDisabledReason.c_str());
    }

    // VkPresentTimingInfoEXT::timeDomainId must always be an ID that
    // vkGetSwapchainTimeDomainPropertiesEXT returned -- not only when a target
    // time is requested. Attaching the struct before the enumeration succeeded
    // would present a zero ID, which the validation layer rejects with
    // VUID-VkPresentTimingInfoEXT-timeDomainId-12400. Telemetry therefore waits
    // for the domains, which are enumerated at swapchain creation and retried
    // only after a bounded VK_INCOMPLETE enumeration.
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming
        && TimingMetadataEnabled && TimeDomainsReady)
    {
        const TargetTimingRequest target = EvaluateTargetTiming(metadata.Sequence);
        metadata.TargetMode = target.Mode;
        metadata.TargetValueNs = target.ValueNs;
        metadata.RelativeRequest = target.Cadence;
        metadata.Timing.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
        metadata.Timing.presentStageQueries = RequestedStageQueries();
        metadata.Timing.timeDomainId = TargetTimeDomainId;
        if (target.Mode != VulkanTargetSchedulingMode::None && target.ValueNs != 0)
        {
            // Absolute: targetTime is an instant on the presentation timeline.
            // Relative: targetTime is how long the PREVIOUS image must stay
            // visible. The same field carries both, which is exactly why the
            // flag has to be set from the mode rather than assumed.
            metadata.Timing.flags =
                target.Mode == VulkanTargetSchedulingMode::Relative
                    ? VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT
                    : 0u;
            // NEAREST_REFRESH_CYCLE rather than a hand-written cadence: at 60
            // emulated FPS on a 144 Hz display the ideal present lands between
            // refresh cycles, and choosing which one is the presentation
            // engine's job, not the emulator's. For a relative duration it is
            // only added when that duration is a whole number of refreshes --
            // on a variable or unknown refresh grid there is no cycle to snap
            // to and asking for one would be meaningless.
            if (target.Quantized)
            {
                metadata.Timing.flags |=
                    VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
            }
            metadata.Timing.targetTime = target.ValueNs;
            // Only meaningful alongside a target, and only for the per-stage
            // domain. Left zero otherwise rather than guessed -- and not
            // zeroed just because the target happens to be relative.
            metadata.Timing.targetTimeDomainPresentStage =
                TargetTimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
                    ? TargetPresentStage
                    : 0;
            LastTargetValueNs = target.ValueNs;
            LastAppliedTargetMode = target.Mode;
            TargetSchedulingActive.store(true, std::memory_order_release);
        }
        else
        {
            // flags=0 with targetTime=0 is telemetry-only metadata: results are
            // still reported, but no presentation deadline is requested.
            LastAppliedTargetMode = VulkanTargetSchedulingMode::None;
            TargetSchedulingActive.store(false, std::memory_order_release);
        }
        metadata.Timings.sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT;
        metadata.Timings.swapchainCount = 1;
        metadata.Timings.pTimingInfos = &metadata.Timing;
        metadata.Timings.pNext = present.pNext;
        present.pNext = &metadata.Timings;
        metadata.TimingAttached = true;
        metadata.TimingBackend = VulkanPresentTimingBackend::ExtPresentTiming;
    }
    else if (backend == VulkanPresentTimingBackend::GoogleDisplayTiming
        && GoogleDisplayTimingRuntimeEnabled)
    {
        const u64 nowNs = GoogleMonotonicTimeNs();
        const bool requestTarget = decisionCurrent && LastDecision.TargetTimeScheduling;
        const VulkanGooglePresentRequest request = GoogleTimingModel.Prepare(
            nowNs, TargetFrameIntervalNs, requestTarget);
        metadata.GoogleTime.presentID = request.PresentId;
        metadata.GoogleTime.desiredPresentTime = request.DesiredPresentTimeNs;
        metadata.GoogleTimes.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
        metadata.GoogleTimes.swapchainCount = 1;
        metadata.GoogleTimes.pTimes = &metadata.GoogleTime;
        metadata.GoogleTimes.pNext = present.pNext;
        present.pNext = &metadata.GoogleTimes;
        metadata.GoogleTimingAttached = true;
        metadata.TimingBackend = VulkanPresentTimingBackend::GoogleDisplayTiming;
        metadata.Sequence = request.PresentId;
        if (!PresentId2Surface)
        {
            metadata.LogicalId = request.PresentId;
            LastSubmittedId = metadata.LogicalId;
        }
        if (request.DesiredPresentTimeNs != 0)
        {
            metadata.TargetMode = VulkanTargetSchedulingMode::Absolute;
            metadata.TargetValueNs = request.DesiredPresentTimeNs;
            LastTargetValueNs = request.DesiredPresentTimeNs;
            LastAppliedTargetMode = VulkanTargetSchedulingMode::Absolute;
            TargetSchedulingActive.store(true, std::memory_order_release);
        }
        else
        {
            LastAppliedTargetMode = VulkanTargetSchedulingMode::None;
            TargetSchedulingActive.store(false, std::memory_order_release);
        }
    }
    else
    {
        TargetSchedulingActive.store(false, std::memory_order_release);
    }

    // Legacy present-wait needs the device-level present ID. It is kept inside
    // ID2 when both are present, so a queue-full retry can remove only timing
    // metadata while preserving both correlation paths and any pre-existing
    // outer chain.
    if (legacyWaitCurrent)
    {
        metadata.LegacyId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        metadata.LegacyId.swapchainCount = 1;
        metadata.LegacyId.pPresentIds = &metadata.LogicalId;
        metadata.LegacyId.pNext = present.pNext;
        present.pNext = &metadata.LegacyId;
        metadata.LegacyIdAttached = true;
    }

    // Keep ID2 outermost. If timing metadata makes vkQueuePresentKHR reject
    // the operation, retry preparation can splice only that node out while
    // preserving the legacy ID, Reflex's outer VkPresentIdKHR and any
    // pre-existing chain.
    if (PresentId2Surface)
    {
        metadata.Id2.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
        metadata.Id2.swapchainCount = 1;
        metadata.Id2.pPresentIds = &metadata.LogicalId;
        metadata.Id2.pNext = present.pNext;
        present.pNext = &metadata.Id2;
        metadata.Id2Attached = true;
    }
    return metadata.LogicalId;
}

bool VulkanPresentPacer::PrepareRetryWithoutTiming(
    VkResult result, VkPresentInfoKHR& present, PresentMetadata& metadata)
{
    if (result != VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT || !metadata.TimingAttached)
        return false;

    // The failed call did not enqueue the image or consume its present ID, so
    // the retry keeps the same image, logical ID and presentation sequence --
    // the sequence is only committed once a present is actually accepted, so a
    // retry cannot leave a hole in the cadence.
    //
    // The wait semaphores are the exception, and they must be dropped. A
    // rejected vkQueuePresentKHR still enqueues the semaphore wait operations
    // it was given; only the presentation itself is refused. Re-waiting them
    // asks for a second signal that nothing will ever produce, which deadlocks
    // the present queue and which the validation layer reports as
    // VUID-vkQueuePresentKHR-pWaitSemaphores-03268. Ordering survives the
    // removal: the wait the first call enqueued is already ahead of the retry
    // on the same queue, and queue operations start in submission order, so
    // the retried presentation still follows the rendering it depends on.
    present.waitSemaphoreCount = 0;
    present.pWaitSemaphores = nullptr;

    // Remove only VkPresentTimingsInfoEXT from the chain. Legacy ID may be
    // between ID2 and timing metadata when both correlation extensions are
    // active; preserve that ladder rather than dropping the fallback ID.
    if (metadata.LegacyIdAttached)
        metadata.LegacyId.pNext = metadata.Timings.pNext;
    else if (metadata.Id2Attached)
        metadata.Id2.pNext = metadata.Timings.pNext;
    metadata.TimingAttached = false;
    metadata.TargetValueNs = 0;
    metadata.TargetMode = VulkanTargetSchedulingMode::None;
    // The retried present carries no target, so it must not carry the cadence
    // inputs of the target it no longer has. The capture reads this metadata.
    metadata.RelativeRequest = VulkanRelativeCadence::Request{};
    ++TimingQueueFullCount;
    TimingMetadataEnabled = false;
    TimingQueuePressureActive = true;
    TargetSchedulingActive.store(false, std::memory_order_release);

    // Draining stays on: it is what frees the slots. Recovery is attempted from
    // the next drain, a bounded number of times, by growing the queue rather
    // than by simply re-enabling metadata into the same full queue. Re-enabling
    // without more room would reject the very next present again, and that
    // reject-retry pair would then repeat every frame.
    const bool recoverable = TimingQueueRecoveries < MaxTimingQueueRecoveries
        && TimingQueueSize < MaxTimingQueueSize;
    TimingQueueRecoveryPending = recoverable;
    WaitDisabledReason = recoverable
        ? "present timing results queue full; timing metadata paused pending a larger queue"
        : "present timing results queue full; timing metadata disabled for this swapchain";
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] %s; retrying present without optional timing metadata\n",
        WaitDisabledReason.c_str());
    return true;
}

void VulkanPresentPacer::NotifyPresentResult(
    VkResult result, const PresentMetadata& metadata) noexcept
{
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
    {
        if (metadata.LogicalId != 0)
            LastPresentedId = metadata.LogicalId;
        if (metadata.Id2Attached)
            TimingModel.CommitPresent();
        if (metadata.GoogleTimingAttached)
            GoogleTimingModel.Commit();
        // Exactly one cadence commit per accepted present -- including a
        // queue-full retry that dropped its timing metadata. The frame was
        // still displayed, so the relative cadence advances with it; skipping
        // it would leave the fraction describing fewer frames than were shown.
        if (metadata.TimingAttached)
            RelativeCadence.Commit();
        // Only an accepted present with timing metadata occupies a results-queue
        // slot and owes a report. The retry path clears TimingAttached before
        // re-presenting, so a queue-full retry is not counted.
        if (metadata.TimingAttached)
            ++OutstandingTimedPresents;
        return;
    }

    // A rejected present never reached the presentation engine, so it must not
    // consume a presentation sequence number or a cadence step.
    if (metadata.Id2Attached)
        TimingModel.AbandonPresent();
    if (metadata.GoogleTimingAttached)
        GoogleTimingModel.Abandon();
    if (metadata.TimingAttached)
        RelativeCadence.Abandon();
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LastPresentedId = 0;
        LastWaitedId = 0;
        // The swapchain is retired: its timing baseline, sequence history and
        // time-domain IDs all belong to a presentation timeline that no longer
        // exists. The rebuild calls OnSwapchainCreated, but resetting here
        // means no frame in between can present against the stale model.
        ResetTimingLifecycle();
    }
}

VulkanPacerBeginResult VulkanPresentPacer::RefreshTimingProperties()
{
    if (!PresentTimingSurface || Swapchain == VK_NULL_HANDLE
        || !Dispatch.GetSwapchainTimingPropertiesEXT)
    {
        TimingPropertiesRetryPending = false;
        return VulkanPacerBeginResult::Continue;
    }

    VkSwapchainTimingPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
    u64 counter = 0;
    const VkResult result = Dispatch.GetSwapchainTimingPropertiesEXT(
        DeviceHandle, Swapchain, &properties, &counter);

    const VulkanPresentTimingQueryAction queryAction =
        ClassifyVulkanTimingPropertiesResult(result);
    if (queryAction == VulkanPresentTimingQueryAction::RetryAfterPresent)
    {
        // Documented and expected before the first present. Keep the pending
        // flag so BeginFrame retries once a present has been accepted.
        TimingPropertiesRetryPending = true;
        return VulkanPacerBeginResult::Continue;
    }
    if (result != VK_SUCCESS)
    {
        TimingPropertiesRetryPending = false;
        TimingPropertiesReady = false;
        if (queryAction == VulkanPresentTimingQueryAction::SurfaceLost)
        {
            const VulkanPacerBeginResult lifecycle =
                ClassifyPresentLifecycleResult(result);
            LogPresentLifecycleRoute(
                "GetSwapchainTimingPropertiesEXT", result, lifecycle);
            TargetSchedulingLifecycleFailed = true;
            return lifecycle;
        }
        TargetSchedulingLifecycleFailed = true;
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkGetSwapchainTimingPropertiesEXT failed (%s); target-time "
            "scheduling stays off and presentation falls back to host pacing\n",
            Vk::FormatResult(result).c_str());
        return VulkanPacerBeginResult::Continue;
    }

    RefreshDurationNs = properties.refreshDuration;
    RefreshIntervalNs = properties.refreshInterval;
    TimingPropertiesCounter = counter;
    TimingPropertiesReady = true;
    TimingPropertiesRetryPending = false;

    if (RefreshIntervalNs == 0)
        RefreshDynamics = VulkanRefreshDynamics::Unknown;
    else if (RefreshIntervalNs == (std::numeric_limits<u64>::max)())
        RefreshDynamics = VulkanRefreshDynamics::VariableRefresh;
    else if (RefreshIntervalNs == RefreshDurationNs)
        RefreshDynamics = VulkanRefreshDynamics::FixedRefresh;
    else
        RefreshDynamics = VulkanRefreshDynamics::DynamicRefresh;

    return VulkanPacerBeginResult::Continue;
}

VulkanPacerBeginResult VulkanPresentPacer::RefreshTimeDomains()
{
    if (!PresentTimingSurface || Swapchain == VK_NULL_HANDLE || !TimeDomainQueryAvailable)
    {
        TimeDomainsEnumerationRetryPending = false;
        return VulkanPacerBeginResult::Continue;
    }

    // Two-call enumeration. The temporary vectors are deliberate: this is not a
    // steady-state per-frame allocation. It runs on swapchain creation, on a
    // bounded VK_INCOMPLETE retry, and when the driver bumps
    // timeDomainsCounter -- all of which are lifecycle events, though the
    // latter is noticed from inside the per-frame drain.
    VkSwapchainTimeDomainPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT;
    u64 counter = 0;
    std::vector<VkTimeDomainKHR> domains;
    std::vector<u64> domainIds;
    u32 count = 0;

    // Standard count/allocate/query enumeration, retried on VK_INCOMPLETE.
    // VK_INCOMPLETE means more domains existed than the array could hold, and
    // the preferred domain may be one of the truncated ones -- accepting the
    // subset would silently keep target scheduling off until the next counter
    // change. The retry count is bounded so a driver that grows its list every
    // call cannot spin here.
    constexpr int MaxTimeDomainEnumerateAttempts = 3;
    bool enumerated = false;
    for (int attempt = 0; attempt < MaxTimeDomainEnumerateAttempts; ++attempt)
    {
        properties.timeDomainCount = 0;
        properties.pTimeDomains = nullptr;
        properties.pTimeDomainIds = nullptr;
        VkResult result = Dispatch.GetSwapchainTimeDomainPropertiesEXT(
            DeviceHandle, Swapchain, &properties, &counter);
        const VulkanPresentTimingQueryAction countAction =
            ClassifyVulkanTimeDomainResult(result);
        if (countAction == VulkanPresentTimingQueryAction::RetryEnumeration)
            continue;
        if (countAction != VulkanPresentTimingQueryAction::Continue)
        {
            TimeDomainsEnumerationRetryPending = false;
            TimeDomainsReady = false;
            TimingModel.ClearTimeDomain();
            if (countAction == VulkanPresentTimingQueryAction::SurfaceLost)
            {
                const VulkanPacerBeginResult lifecycle =
                    ClassifyPresentLifecycleResult(result);
                LogPresentLifecycleRoute(
                    "GetSwapchainTimeDomainPropertiesEXT(count)", result, lifecycle);
                TargetSchedulingLifecycleFailed = true;
                return lifecycle;
            }
            TargetSchedulingLifecycleFailed = true;
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] vkGetSwapchainTimeDomainPropertiesEXT count query failed (%s); "
                "target-time scheduling stays off\n",
                Vk::FormatResult(result).c_str());
            return VulkanPacerBeginResult::Continue;
        }
        if (properties.timeDomainCount == 0)
        {
            TimeDomainsEnumerationRetryPending = false;
            TimeDomainsReady = false;
            TargetSchedulingLifecycleFailed = true;
            TimingModel.ClearTimeDomain();
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] vkGetSwapchainTimeDomainPropertiesEXT returned no domains; "
                "required PRESENT_STAGE_LOCAL contract is unavailable\n");
            return VulkanPacerBeginResult::Continue;
        }

        domains.assign(properties.timeDomainCount, VK_TIME_DOMAIN_DEVICE_KHR);
        domainIds.assign(properties.timeDomainCount, 0);
        properties.pTimeDomains = domains.data();
        properties.pTimeDomainIds = domainIds.data();
        result = Dispatch.GetSwapchainTimeDomainPropertiesEXT(
            DeviceHandle, Swapchain, &properties, &counter);
        const VulkanPresentTimingQueryAction arrayAction =
            ClassifyVulkanTimeDomainResult(result);
        if (arrayAction == VulkanPresentTimingQueryAction::Continue)
        {
            count = std::min<u32>(properties.timeDomainCount,
                                  static_cast<u32>(domains.size()));
            enumerated = true;
            break;
        }
        if (arrayAction != VulkanPresentTimingQueryAction::RetryEnumeration)
        {
            TimeDomainsEnumerationRetryPending = false;
            TimeDomainsReady = false;
            TimingModel.ClearTimeDomain();
            if (arrayAction == VulkanPresentTimingQueryAction::SurfaceLost)
            {
                const VulkanPacerBeginResult lifecycle =
                    ClassifyPresentLifecycleResult(result);
                LogPresentLifecycleRoute(
                    "GetSwapchainTimeDomainPropertiesEXT(array)", result, lifecycle);
                TargetSchedulingLifecycleFailed = true;
                return lifecycle;
            }
            TargetSchedulingLifecycleFailed = true;
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] vkGetSwapchainTimeDomainPropertiesEXT array query failed (%s); "
                "target-time scheduling stays off\n",
                Vk::FormatResult(result).c_str());
            return VulkanPacerBeginResult::Continue;
        }
    }

    if (!enumerated)
    {
        // The list kept growing. Leave the retry armed rather than committing
        // to a domain chosen from a list known to be truncated.
        TimeDomainsEnumerationRetryPending = true;
        TimeDomainsReady = false;
        TimingModel.ClearTimeDomain();
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] swapchain time-domain enumeration kept returning VK_INCOMPLETE; "
            "retrying on the next timing report\n");
        return VulkanPacerBeginResult::Continue;
    }

    // SWAPCHAIN_LOCAL first: it is the domain the swapchain's own presentation
    // timestamps live in, so a reported stage time can be projected forward
    // without any cross-clock conversion. PRESENT_STAGE_LOCAL is the fallback
    // and additionally requires naming the stage the target refers to.
    auto pick = [&](VkTimeDomainKHR wanted) -> bool {
        for (u32 i = 0; i < count; ++i)
        {
            if (domains[i] != wanted)
                continue;
            TargetTimeDomain = domains[i];
            TargetTimeDomainId = domainIds[i];
            return true;
        }
        return false;
    };

    bool hasRequiredPresentStageDomain = false;
    for (u32 i = 0; i < count; ++i)
    {
        if (domains[i] == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT)
        {
            hasRequiredPresentStageDomain = true;
            break;
        }
    }
    const bool picked = hasRequiredPresentStageDomain
        && (pick(VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
            || pick(VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT));

    TimeDomainsCounter = counter;
    TimeDomainsEnumerationRetryPending = false;
    if (!picked)
    {
        TimeDomainsReady = false;
        TargetTimeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
        TargetTimeDomainId = 0;
        TimingModel.ClearTimeDomain();
        TargetSchedulingLifecycleFailed = true;
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] swapchain time-domain contract is incomplete; required "
            "PRESENT_STAGE_LOCAL domain or a usable target domain is missing\n");
        return VulkanPacerBeginResult::Continue;
    }

    SelectTargetPresentStage();
    TimeDomainsReady = true;
    // A domain change drops the baseline inside the model: a timestamp taken on
    // the old clock cannot be extrapolated on the new one.
    TimingModel.SetTimeDomain(static_cast<s32>(TargetTimeDomain), TargetTimeDomainId);
    return VulkanPacerBeginResult::Continue;
}

void VulkanPresentPacer::SelectTargetPresentStage() noexcept
{
    TargetPresentStage = 0;
    for (const VkPresentStageFlagsEXT stage : TargetStagePreference)
    {
        if ((PresentStageQueries & stage) != 0)
        {
            TargetPresentStage = stage;
            return;
        }
    }
}

VulkanPacerBeginResult VulkanPresentPacer::RefreshGoogleTiming()
{
    if (!GoogleDisplayTimingRuntimeEnabled || DeviceHandle == VK_NULL_HANDLE
        || Swapchain == VK_NULL_HANDLE || !Dispatch.GetRefreshCycleDurationGOOGLE)
    {
        GoogleRefreshDurationReady = false;
        return VulkanPacerBeginResult::Continue;
    }

    VkRefreshCycleDurationGOOGLE duration{};
    const VkResult result = Dispatch.GetRefreshCycleDurationGOOGLE(
        DeviceHandle, Swapchain, &duration);
    if (result == VK_SUCCESS && duration.refreshDuration != 0)
    {
        RefreshDurationNs = duration.refreshDuration;
        RefreshIntervalNs = duration.refreshDuration;
        GoogleRefreshDurationReady = true;
        return VulkanPacerBeginResult::Continue;
    }
    const VulkanPresentTimingQueryAction queryAction =
        ClassifyVulkanGoogleRefreshCycleResult(result);
    if (queryAction != VulkanPresentTimingQueryAction::Continue
        && queryAction != VulkanPresentTimingQueryAction::DisableOptional)
    {
        const VulkanPacerBeginResult lifecycle =
            ClassifyPresentLifecycleResult(result);
        LogPresentLifecycleRoute(
            "GetRefreshCycleDurationGOOGLE", result, lifecycle);
        return lifecycle;
    }

    GoogleRefreshDurationReady = false;
    GoogleDisplayTimingRuntimeEnabled = false;
    WaitDisabledReason = "GOOGLE refresh-cycle query failed: " + Vk::FormatResult(result);
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] %s; GOOGLE display timing disabled for this device\n",
        WaitDisabledReason.c_str());
    return VulkanPacerBeginResult::Continue;
}

VulkanPacerBeginResult VulkanPresentPacer::ReportGooglePastTiming()
{
    if (!GoogleDisplayTimingRuntimeEnabled || DeviceHandle == VK_NULL_HANDLE
        || Swapchain == VK_NULL_HANDLE)
        return VulkanPacerBeginResult::Continue;
    if (!GoogleRefreshDurationReady)
    {
        const VulkanPacerBeginResult refresh = RefreshGoogleTiming();
        if (refresh != VulkanPacerBeginResult::Continue)
            return refresh;
    }

    std::array<VkPastPresentationTimingGOOGLE, 16> reports{};
    u32 count = static_cast<u32>(reports.size());
    const VkResult result = Dispatch.GetPastPresentationTimingGOOGLE(
        DeviceHandle, Swapchain, &count, reports.data());
    const VulkanPresentTimingQueryAction queryAction =
        ClassifyVulkanGooglePastTimingResult(result);
    if (queryAction != VulkanPresentTimingQueryAction::Continue)
    {
        if (queryAction != VulkanPresentTimingQueryAction::DisableOptional)
        {
            const VulkanPacerBeginResult lifecycle =
                ClassifyPresentLifecycleResult(result);
            LogPresentLifecycleRoute(
                "GetPastPresentationTimingGOOGLE", result, lifecycle);
            return lifecycle;
        }

        GoogleDisplayTimingRuntimeEnabled = false;
        GoogleRefreshDurationReady = false;
        FallbackReason = VulkanJitFallbackReason::TimingQueryFailed;
        WaitDisabledReason = "GOOGLE presentation feedback query failed: "
            + Vk::FormatResult(result);
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] %s; GOOGLE display timing disabled\n", WaitDisabledReason.c_str());
        return VulkanPacerBeginResult::Continue;
    }

    count = std::min<u32>(count, static_cast<u32>(reports.size()));
    for (u32 i = 0; i < count; ++i)
    {
        const VkPastPresentationTimingGOOGLE& report = reports[i];
        VulkanGooglePresentationFeedback feedback;
        feedback.PresentId = report.presentID;
        feedback.DesiredPresentTimeNs = report.desiredPresentTime;
        feedback.ActualPresentTimeNs = report.actualPresentTime;
        feedback.EarliestPresentTimeNs = report.earliestPresentTime;
        feedback.PresentMarginNs = report.presentMargin;
        GoogleTimingModel.RecordFeedback(feedback);
        LastFeedbackId = report.presentID;
    }
    return VulkanPacerBeginResult::Continue;
}

VulkanPacerBeginResult VulkanPresentPacer::ReportPastTiming()
{
    // Draining is gated separately from attaching metadata: a full queue pauses
    // metadata but must keep draining, because draining is what makes room.
    if (!TimingResultsQueryEnabled || Swapchain == VK_NULL_HANDLE)
        return VulkanPacerBeginResult::Continue;

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
    const VkResult result = Dispatch.GetPastPresentationTimingEXT(
        DeviceHandle, &info, &properties);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        const VulkanPresentTimingQueryAction queryAction =
            ClassifyVulkanPastTimingResult(result);
        if (queryAction != VulkanPresentTimingQueryAction::DisableOptional)
        {
            const VulkanPacerBeginResult lifecycle =
                ClassifyPresentLifecycleResult(result);
            TargetSchedulingActive.store(false, std::memory_order_release);
            LogPresentLifecycleRoute(
                "GetPastPresentationTimingEXT", result, lifecycle);
            return lifecycle;
        }
        // A failing query is not recoverable by draining: stop both switches so
        // the driver is not asked again every frame.
        TimingMetadataEnabled = false;
        TimingResultsQueryEnabled = false;
        TimingQueueRecoveryPending = false;
        TargetSchedulingActive.store(false, std::memory_order_release);
        FallbackReason = VulkanJitFallbackReason::TimingQueryFailed;
        WaitDisabledReason = "present timing report query failed: " + Vk::FormatResult(result);
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] %s; timing metadata disabled\n", WaitDisabledReason.c_str());
        return VulkanPacerBeginResult::Continue;
    }

    // Swapchain timing properties and time domains are both allowed to change
    // while the swapchain lives -- refresh-rate change, fullscreen transition,
    // power state, VRR/FRR switch. The counters are the only notification the
    // extension gives, so they are checked on every drain.
    if (properties.timingPropertiesCounter != TimingPropertiesCounter)
    {
        const VulkanPacerBeginResult timingProperties = RefreshTimingProperties();
        if (timingProperties != VulkanPacerBeginResult::Continue)
            return timingProperties;
    }
    if (properties.timeDomainsCounter != TimeDomainsCounter)
    {
        // Re-enumerating drops the baseline through SetTimeDomain, so a target
        // computed against an old timeDomainId can never be presented.
        FallbackReason = VulkanJitFallbackReason::DomainChanged;
        const VulkanPacerBeginResult timeDomains = RefreshTimeDomains();
        if (timeDomains != VulkanPacerBeginResult::Continue)
            return timeDomains;
    }

    const u32 reportCount = std::min<u32>(
        properties.presentationTimingCount, static_cast<u32>(reports.size()));

    // With ALLOW_PARTIAL_RESULTS, a returned entry may still be incomplete and
    // therefore still owns its queue slot. Only complete entries release a slot
    // and decrement the accepted-present count.
    u32 completedReportCount = 0;
    for (u32 i = 0; i < reportCount; ++i)
    {
        if (reports[i].reportComplete == VK_TRUE)
            ++completedReportCount;
    }
    OutstandingTimedPresents -= std::min<u64>(
        OutstandingTimedPresents, completedReportCount);

    for (u32 i = 0; i < reportCount; ++i)
    {
        const VkPastPresentationTimingEXT& report = reports[i];
        if (report.presentId == 0)
            continue;

        // Only the stage the targets are expressed against counts, and only
        // with a real timestamp: a zero time means "not measured", not "time 0".
        u64 stageTime = 0;
        for (u32 s = 0; s < report.presentStageCount; ++s)
        {
            if (report.pPresentStages[s].stage == TargetPresentStage
                && report.pPresentStages[s].time != 0)
            {
                stageTime = report.pPresentStages[s].time;
                break;
            }
        }
        if (stageTime == 0)
            continue;

        LastFeedbackId = report.presentId;
        LastFeedbackStageTimeNs = stageTime;

        const VulkanPresentFeedbackResult feedback = TimingModel.RecordFeedback(
            report.presentId, stageTime,
            static_cast<s32>(report.timeDomain), report.timeDomainId);
        if (feedback == VulkanPresentFeedbackResult::DomainMismatch)
        {
            // The driver answered in a domain other than the requested one.
            // Scheduling against a clock the timestamps do not come from is
            // worse than not scheduling, so fall back and re-enumerate.
            FallbackReason = VulkanJitFallbackReason::DomainChanged;
            TargetSchedulingActive.store(false, std::memory_order_release);
            const VulkanPacerBeginResult timeDomains = RefreshTimeDomains();
            if (timeDomains != VulkanPacerBeginResult::Continue)
                return timeDomains;
            break;
        }
    }

    // The first successful present is also the point where a swapchain that
    // answered VK_NOT_READY for timing properties earlier can finally report
    // its refresh timing. Time-domain enumeration has no VK_NOT_READY state.
    if (reportCount > 0)
    {
        if (TimingPropertiesRetryPending)
        {
            const VulkanPacerBeginResult timingProperties = RefreshTimingProperties();
            if (timingProperties != VulkanPacerBeginResult::Continue)
                return timingProperties;
        }
        if (TimeDomainsEnumerationRetryPending)
        {
            const VulkanPacerBeginResult timeDomains = RefreshTimeDomains();
            if (timeDomains != VulkanPacerBeginResult::Continue)
                return timeDomains;
        }
    }

    // Queue-full recovery. This drain freed slots, so the queue can now be
    // grown and metadata switched back on. Bounded by MaxTimingQueueRecoveries
    // and by the size ceiling: a driver whose reports complete too slowly for
    // this present rate settles into telemetry-off instead of oscillating.
    // TimingQueueAllocated separates the two failure classes: growth is only
    // meaningful on a queue that was allocated in the first place.
    if (TimingQueueRecoveryPending && completedReportCount > 0 && TimingQueueAllocated)
    {
        TimingQueueRecoveryPending = false;
        const u32 grown = std::min(MaxTimingQueueSize, std::max(
            MinTimingQueueSize, TimingQueueSize * 2));
        // A failed growth is not a failed allocation: the previous queue is
        // still there and still working, so only the re-enable is skipped. If
        // the ceiling has already been reached, clearing the pending bit also
        // lets the normal drained-queue stop condition retire polling.
        if (grown > TimingQueueSize && ApplyTimingQueueSize(grown))
        {
            ++TimingQueueRecoveries;
            TimingQueuePressureActive = false;
            TimingMetadataEnabled = PresentTimingSurface;
            WaitDisabledReason.clear();
            Platform::Log(Platform::LogLevel::Info,
                "[Vulkan] present timing results queue grown to %u after draining; "
                "timing metadata re-enabled (recovery %u/%u, queue-full errors=%u)\n",
                TimingQueueSize,
                TimingQueueRecoveries, MaxTimingQueueRecoveries,
                TimingQueueFullCount);
        }
    }

    // Nothing left to poll for: metadata is off for good, no recovery is armed,
    // and every timed present has been reported. The outstanding counter is
    // what makes this provable -- an empty poll on its own does not, because
    // the extension only guarantees a result appears in finite time, with no
    // relationship to when it is asked for.
    if (!TimingMetadataEnabled && !TimingQueueRecoveryPending
        && OutstandingTimedPresents == 0 && reportCount == 0)
    {
        TimingResultsQueryEnabled = false;
        TargetSchedulingActive.store(false, std::memory_order_release);
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] all timed presents reported after %u queue-full events; "
            "stopping per-frame timing polling for this swapchain\n",
            TimingQueueFullCount);
    }

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    if (reportCount > 0)
    {
        if (++TimingReportCountdown < TimingLogPeriodFrames)
            return VulkanPacerBeginResult::Continue;
        TimingReportCountdown = 0;
        const VkPastPresentationTimingEXT& latest = reports[reportCount - 1];
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] present timing: id=%llu target=%llu complete=%s stages=%u "
            "jit=%s lastTarget=%llu feedbackId=%llu feedbackStageTime=%llu "
            "baselineSeq=%llu seq=%llu frameIntervalNs=%llu "
            "refreshDurationNs=%llu refreshIntervalNs=%llu dynamics=%s "
            "timingCounter=%llu domainCounter=%llu waitTimeouts=%u queueFull=%u "
            "fallback=%s\n",
            static_cast<unsigned long long>(latest.presentId),
            static_cast<unsigned long long>(latest.targetTime),
            latest.reportComplete ? "yes" : "no",
            latest.presentStageCount,
            IsTargetSchedulingActive() ? "active" : "inactive",
            static_cast<unsigned long long>(LastTargetValueNs),
            static_cast<unsigned long long>(LastFeedbackId),
            static_cast<unsigned long long>(LastFeedbackStageTimeNs),
            static_cast<unsigned long long>(TimingModel.GetBaselineSequence()),
            static_cast<unsigned long long>(TimingModel.GetCommittedSequence()),
            static_cast<unsigned long long>(TargetFrameIntervalNs),
            static_cast<unsigned long long>(RefreshDurationNs),
            static_cast<unsigned long long>(RefreshIntervalNs),
            VulkanRefreshDynamicsName(RefreshDynamics),
            static_cast<unsigned long long>(TimingPropertiesCounter),
            static_cast<unsigned long long>(TimeDomainsCounter),
            WaitTimeouts,
            TimingQueueFullCount,
            VulkanJitFallbackReasonName(FallbackReason));
    }
#endif
    return VulkanPacerBeginResult::Continue;
}

void VulkanPresentPacer::DisableWait(const char* reason)
{
    DisableWait(reason, false);
}

void VulkanPresentPacer::DisableWait(const char* reason, bool legacyWait)
{
    if (legacyWait)
        PresentWaitLegacyRuntimeEnabled = false;
    else
        PresentWait2RuntimeEnabled = false;
    WaitRuntimeEnabled = PresentWait2RuntimeEnabled
        || PresentWaitLegacyRuntimeEnabled;
    WaitDisabledReason = reason ? reason : "runtime failure";

    // Only the bounded wait is retired here. Target-time scheduling is a
    // separate capability and keeps running if the policy asks for it -- losing
    // the previous-present wait says nothing about whether the presentation
    // engine can still honour a deadline. The next BeginFrame re-resolves the
    // authority from the updated capability set.
    if (!LastDecision.TargetTimeScheduling)
    {
        Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                        std::memory_order_release);
        TargetSchedulingActive.store(false, std::memory_order_release);
    }
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] generic present wait (%s) disabled: %s; %s\n",
        legacyWait ? "legacy" : "present_wait2",
        WaitDisabledReason.c_str(),
        LastDecision.TargetTimeScheduling
            ? "target-time scheduling continues without it"
            : "falling back to host pacing");
}

void VulkanPresentPacer::LogTargetSchedulingIfChanged()
{
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    const bool active = IsTargetSchedulingActive();
    if (active == LoggedTargetSchedulingActive && FallbackReason == LoggedFallbackReason)
        return;
    LoggedTargetSchedulingActive = active;
    LoggedFallbackReason = FallbackReason;

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] present JIT: policy=%s authority=%s state=%s targetScheduling=%s "
        "timingBackend=%s targetMode=%s boundedWait=%s optionalWait=%s timingReady=%s "
        "timeDomainsReady=%s absoluteSupported=%s relativeSupported=%s "
        "targetStage=%s timeDomain=%s "
        "domainId=%llu frameIntervalNs=%llu refreshIntervalNs=%llu dynamics=%s "
        "relativeQuanta=%llu baselineId=%llu baselineSequence=%llu "
        "baselineTime=%llu targetValue=%llu fallback=%s\n",
        VulkanPresentPacingPolicyName(GetPolicy()),
        VulkanPacingAuthorityName(GetAuthority()),
        active ? "TargetSchedulingActive" : "TelemetryBootstrap",
        LastDecision.TargetTimeScheduling ? "capable" : "off",
        VulkanPresentTimingBackendName(LastDecision.TimingBackend),
        // The mode is what tells an A/B reader whether a JustInTime run really
        // scheduled, and against which semantics the target value should be
        // read: an instant for absolute, a duration for relative.
        VulkanTargetSchedulingModeName(LastDecision.TargetMode),
        LastDecision.BoundedPresentWait ? "on" : "off",
        // The bounded wait is optional and independent: reporting it here is
        // what keeps a wait-less driver from looking like a broken JIT setup.
        LastDecision.OptionalWaitUnavailable
            ? VulkanJitFallbackReasonName(VulkanJitFallbackReason::PresentWait2Unsupported)
            : "available",
        TimingPropertiesReady ? "yes" : "no",
        TimeDomainsReady ? "yes" : "no",
        (PresentTimingAbsoluteSurface && AbsoluteTimingDevice) ? "yes" : "no",
        (PresentTimingRelativeSurface && RelativeTimingDevice) ? "yes" : "no",
        PresentStageName(TargetPresentStage),
        TimeDomainName(TargetTimeDomain),
        static_cast<unsigned long long>(TargetTimeDomainId),
        static_cast<unsigned long long>(TargetFrameIntervalNs),
        static_cast<unsigned long long>(RefreshIntervalNs),
        VulkanRefreshDynamicsName(RefreshDynamics),
        static_cast<unsigned long long>(RelativeCadence.GetPendingQuanta()),
        static_cast<unsigned long long>(TimingModel.GetBaselineLogicalId()),
        static_cast<unsigned long long>(TimingModel.GetBaselineSequence()),
        static_cast<unsigned long long>(TimingModel.GetBaselineStageTimeNs()),
        static_cast<unsigned long long>(LastTargetValueNs),
        VulkanJitFallbackReasonName(FallbackReason));
#endif
}

void VulkanPresentPacer::LogState(const char* context) const
{
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] %s generic present pacing: policy=%s authority=%s caps2=%s "
        "present-id2=%s present-wait2=%s present-timing=%s absolute-timing=%s "
        "relative-timing=%s google-display-timing=%s timingBackend=%s "
        "target-stage=%s timing-queue=%u fifo-latest-ready=%s presentMode=%d reason=%s\n",
        context ? context : "state:",
        VulkanPresentPacingPolicyName(GetPolicy()),
        VulkanPacingAuthorityName(GetAuthority()),
        Caps2Available ? "yes" : "no",
        PresentId2Surface ? "yes" : "no",
        PresentWait2Surface ? "yes" : "no",
        TimingMetadataEnabled ? "yes" : "no",
        (PresentTimingAbsoluteSurface && AbsoluteTimingDevice) ? "yes" : "no",
        (PresentTimingRelativeSurface && RelativeTimingDevice) ? "yes" : "no",
        GoogleDisplayTimingRuntimeEnabled ? "yes" : "no",
        VulkanPresentTimingBackendName(LastDecision.TimingBackend),
        PresentStageName(TargetPresentStage),
        TimingQueueSize,
        (PresentMode == VK_PRESENT_MODE_FIFO_LATEST_READY_KHR) ? "yes" : "no",
        static_cast<int>(PresentMode),
        WaitDisabledReason.empty() ? "available capabilities are optional" : WaitDisabledReason.c_str());
}

VulkanPacingAuthority VulkanPresentPacer::GetAuthority() const noexcept
{
    return static_cast<VulkanPacingAuthority>(Authority.load(std::memory_order_acquire));
}

bool VulkanPresentPacer::IsTargetSchedulingActive() const noexcept
{
    return TargetSchedulingActive.load(std::memory_order_acquire);
}

VulkanPresentPacer::StateSnapshot VulkanPresentPacer::CaptureState(
    const PresentMetadata& metadata) const noexcept
{
    StateSnapshot snapshot;
    snapshot.Policy = static_cast<int>(GetPolicy());
    snapshot.Authority = static_cast<int>(GetAuthority());
    snapshot.PresentMode = static_cast<int>(PresentMode);
    snapshot.SwapchainGeneration = SwapchainGeneration;
    const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(
        DecisionSwapchainGeneration, SwapchainGeneration);
    const bool waitAttemptCurrent = VulkanFrameDecisionMatchesSwapchain(
        WaitAttemptSwapchainGeneration, SwapchainGeneration);
    snapshot.BoundedPresentWait = decisionCurrent && LastDecision.BoundedPresentWait;
    snapshot.BoundedWaitAttempted = waitAttemptCurrent && WaitAttemptedThisFrame;
    snapshot.FallbackReason = static_cast<int>(VulkanFrameDecisionFallbackReason(
        decisionCurrent, FallbackReason));

    // Everything about the target comes from this present, not from the
    // resolver's permission or the pacer's last-known values. A queue-full
    // retry re-presents the same frame with its timing metadata stripped: the
    // frame is displayed, but with no target, and the row has to say so.
    const VulkanAppliedTarget applied = ResolveVulkanAppliedTarget(
        metadata.TimingAttached || metadata.GoogleTimingAttached,
        metadata.TargetMode, metadata.TargetValueNs);
    snapshot.TargetTimeScheduling = applied.Applied;
    snapshot.TargetMode = static_cast<int>(applied.Mode);
    snapshot.TimingBackend = static_cast<int>(metadata.TimingBackend);
    snapshot.TargetValueNs = applied.ValueNs;

    const VulkanRelativeCadence::Request& cadence = metadata.RelativeRequest;
    const bool relativeApplied = applied.Applied
        && applied.Mode == VulkanTargetSchedulingMode::Relative;
    snapshot.TargetGenerationRefreshIntervalNs =
        relativeApplied ? cadence.RefreshIntervalNs : 0;
    snapshot.TargetGenerationRefreshDurationNs =
        relativeApplied ? cadence.RefreshDurationNs : 0;
    snapshot.RelativeQuanta = relativeApplied ? cadence.Quanta : 0;
    snapshot.RelativeAccumulatorBeforeNs =
        relativeApplied ? cadence.AccumulatorBeforeNs : 0;
    snapshot.RelativeAccumulatorAfterNs =
        relativeApplied ? cadence.AccumulatorAfterNs : 0;
    snapshot.FeedbackPresentId = LastFeedbackId;
    snapshot.FeedbackStageTimeNs = LastFeedbackStageTimeNs;
    const VulkanGooglePresentationFeedback& googleFeedback =
        GoogleTimingModel.GetFeedback();
    snapshot.FeedbackDesiredPresentTimeNs = googleFeedback.DesiredPresentTimeNs;
    snapshot.FeedbackActualPresentTimeNs = googleFeedback.ActualPresentTimeNs;
    snapshot.FeedbackEarliestPresentTimeNs = googleFeedback.EarliestPresentTimeNs;
    snapshot.FeedbackPresentMarginNs = googleFeedback.PresentMarginNs;
    snapshot.BaselineSequence = TimingModel.GetBaselineSequence();
    snapshot.PresentSequence = TimingModel.GetCommittedSequence();
    snapshot.FrameIntervalNs = decisionCurrent ? TargetFrameIntervalNs : 0;
    snapshot.WaitTimeouts = WaitTimeouts;
    snapshot.TimingQueueSize = TimingQueueSize;
    snapshot.TimingQueueFullCount = TimingQueueFullCount;
    snapshot.TimingQueueRecoveries = TimingQueueRecoveries;
    return snapshot;
}

} // namespace melonDS

#endif
