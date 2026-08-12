/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
*/

#include "VulkanPresentPacer.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace melonDS
{

namespace
{

constexpr u64 MaxPresentWaitNs = 2'000'000; // A driver stall must never hang input.
constexpr u32 TimingLogPeriodFrames = 600;

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

[[maybe_unused]] const char* TimeDomainName(VkTimeDomainKHR domain) noexcept
{
    switch (domain)
    {
    case VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT: return "SWAPCHAIN_LOCAL";
    case VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT: return "PRESENT_STAGE_LOCAL";
    case VK_TIME_DOMAIN_DEVICE_KHR: return "DEVICE";
    case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR: return "CLOCK_MONOTONIC";
    case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR: return "CLOCK_MONOTONIC_RAW";
    case VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR: return "QUERY_PERFORMANCE_COUNTER";
    default: return "unknown";
    }
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
    case VulkanJitFallbackReason::VendorLatencyApiOwnsPacing:
        return "vendor latency API owns pacing";
    case VulkanJitFallbackReason::NotNormalSpeed: return "not normal speed";
    case VulkanJitFallbackReason::PresentId2Unsupported: return "present_id2 unsupported";
    case VulkanJitFallbackReason::PresentWait2Unsupported: return "present_wait2 unsupported";
    case VulkanJitFallbackReason::PresentTimingUnsupported: return "present timing unsupported";
    case VulkanJitFallbackReason::AbsoluteTimingUnsupportedDevice:
        return "absolute timing unsupported by device";
    case VulkanJitFallbackReason::AbsoluteTimingUnsupportedSurface:
        return "absolute timing unsupported by surface";
    case VulkanJitFallbackReason::NonFifoPresentMode: return "present mode is not FIFO";
    case VulkanJitFallbackReason::NoFrameInterval: return "no emulator frame interval";
    case VulkanJitFallbackReason::TimingPropertiesNotReady: return "timing properties not ready";
    case VulkanJitFallbackReason::TimeDomainsNotReady: return "time domains not ready";
    case VulkanJitFallbackReason::NoValidTargetStage: return "no valid target stage";
    case VulkanJitFallbackReason::BootstrapWaitingForFeedback:
        return "bootstrap waiting for feedback";
    case VulkanJitFallbackReason::DomainChanged: return "domain changed";
    case VulkanJitFallbackReason::TimingQueryFailed: return "timing query failed";
    }
    return "none";
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
    // Absolute target scheduling additionally needs the feature bit that
    // vkCreateDevice enabled and the entry point that enumerates the swapchain's
    // time domains; without the latter there is no legal timeDomainId to name.
    TimeDomainQueryAvailable = device.Fns().GetSwapchainTimeDomainPropertiesEXT != nullptr;
    AbsoluteTimingDevice = PresentTimingDevice && TimeDomainQueryAvailable
        && device.GetPresentTimingFeatures().PresentAtAbsoluteTime;
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
    AbsoluteTimingDevice = false;
    LatestReadyDevice = false;
    TimeDomainQueryAvailable = false;
    PresentId2Surface = false;
    PresentWait2Surface = false;
    PresentTimingSurface = false;
    PresentTimingRuntimeEnabled = false;
    PresentTimingRelative = false;
    PresentTimingAbsolute = false;
    PresentStageQueries = 0;
    TargetSchedulingLifecycleFailed = false;
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
    PresentTimingAbsolute = false;
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
            PresentTimingAbsolute = PresentTimingSurface && AbsoluteTimingDevice
                && timing.presentAtAbsoluteTimeSupported == VK_TRUE;
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
    if (TargetSchedulingLifecycleFailed)
        return false;
    return PresentTimingSurface && PresentId2Surface && LatestReadyDevice
        && PresentTimingAbsolute && TimeDomainQueryAvailable
        && Device != nullptr
        && Device->Fns().SetSwapchainPresentTimingQueueSizeEXT != nullptr;
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
    TimeDomainsRetryPending = false;
    TargetTimeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
    TargetTimeDomainId = 0;
    TargetPresentStage = 0;
    RefreshDurationNs = 0;
    RefreshIntervalNs = 0;
    LastTargetTimeNs = 0;
    LastFeedbackId = 0;
    LastFeedbackStageTimeNs = 0;
    TimingModel.Reset();
    TimingModel.ClearTimeDomain();
    TargetSchedulingActive.store(false, std::memory_order_release);
}

void VulkanPresentPacer::OnSwapchainCreated(
    VkSwapchainKHR swapchain, VkPresentModeKHR presentMode)
{
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
    WaitRuntimeEnabled = PresentWait2Surface;
    PresentTimingRuntimeEnabled = PresentTimingSurface;
    LoggedFallbackReason = VulkanJitFallbackReason::None;
    LoggedTargetSchedulingActive = false;
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
        // Both queries are allowed to answer VK_NOT_READY here: a swapchain
        // need not know its refresh timing or its time domains until it has
        // presented at least once. That is a pending state, not an error, and
        // BeginFrame retries it once results start arriving.
        RefreshTimingProperties();
        RefreshTimeDomains();
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
    bool reflexActive, bool antiLagActive, bool normalSpeed, u64 targetFrameIntervalNs)
{
    SelectAuthority(reflexActive, antiLagActive, normalSpeed);

    // The emulator's own frame interval, straight from the frame limiter. It is
    // zero for fast-forward, slow motion and unlimited FPS, and that zero is
    // what turns target scheduling off for those modes: the presentation engine
    // must never be handed a cadence the emulator is not running at.
    TargetFrameIntervalNs = normalSpeed ? targetFrameIntervalNs : 0;

    // Telemetry-only is the safe default: collect periodic timing reports even
    // though no behavioural wait owns pacing. Draining also feeds the
    // scheduling baseline and carries the property/domain change counters.
    ReportPastTiming();

    // Retry the two lifecycle queries that are allowed to answer VK_NOT_READY
    // before the first present. Gated on the pending flag so this is not a
    // per-frame driver query in the steady state.
    if (TimingPropertiesRetryPending && LastPresentedId != 0)
        RefreshTimingProperties();
    if (TimeDomainsRetryPending && LastPresentedId != 0)
        RefreshTimeDomains();

    LogTargetSchedulingIfChanged();

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
    if (result == VK_ERROR_DEVICE_LOST)
    {
        // Not an optional-feature failure. Surfacing it lets the presenter's
        // existing runtime-failure path handle a lost device instead of the
        // pacer quietly downgrading itself and continuing on a dead device.
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] vkWaitForPresent2KHR reported VK_ERROR_DEVICE_LOST\n");
        DisableWait("VK_ERROR_DEVICE_LOST");
        return true;
    }

    DisableWait(Vk::FormatResult(result).c_str());
    return false;
}

u64 VulkanPresentPacer::EvaluateTargetTime(u64 sequence) noexcept
{
    // Every reason the target time is zero is recorded rather than merged into
    // one "unavailable", because during A/B testing the interesting question is
    // always which of these is the one blocking the path.
    const VulkanPresentPacingPolicy policy = GetPolicy();
    if (policy != VulkanPresentPacingPolicy::JustInTime
        && policy != VulkanPresentPacingPolicy::JustInTimeFifoLatestReady)
    {
        // TelemetryOnly and PresentWait both keep targetTime at zero by
        // definition; neither promises a scheduled presentation time.
        FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
        return 0;
    }

    const VulkanPacingAuthority authority = GetAuthority();
    if (authority == VulkanPacingAuthority::NvidiaReflex
        || authority == VulkanPacingAuthority::AmdAntiLag2)
    {
        // Reflex and Anti-Lag 2 already own frame pacing end to end. Adding a
        // second scheduler on top would fight their driver-side model.
        FallbackReason = VulkanJitFallbackReason::VendorLatencyApiOwnsPacing;
        return 0;
    }
    if (authority != VulkanPacingAuthority::GenericPresentTiming)
    {
        FallbackReason = VulkanJitFallbackReason::NotNormalSpeed;
        return 0;
    }
    if (!PresentId2Surface)
    {
        FallbackReason = VulkanJitFallbackReason::PresentId2Unsupported;
        return 0;
    }
    if (!PresentWait2Surface)
    {
        FallbackReason = VulkanJitFallbackReason::PresentWait2Unsupported;
        return 0;
    }
    if (!PresentTimingRuntimeEnabled)
    {
        FallbackReason = PresentTimingSurface
            ? VulkanJitFallbackReason::TimingQueryFailed
            : VulkanJitFallbackReason::PresentTimingUnsupported;
        return 0;
    }
    if (!AbsoluteTimingDevice)
    {
        FallbackReason = VulkanJitFallbackReason::AbsoluteTimingUnsupportedDevice;
        return 0;
    }
    if (!PresentTimingAbsolute)
    {
        FallbackReason = VulkanJitFallbackReason::AbsoluteTimingUnsupportedSurface;
        return 0;
    }
    if (!FifoFamilyPresentMode)
    {
        FallbackReason = VulkanJitFallbackReason::NonFifoPresentMode;
        return 0;
    }
    if (TargetFrameIntervalNs == 0)
    {
        FallbackReason = VulkanJitFallbackReason::NoFrameInterval;
        return 0;
    }
    if (!TimingPropertiesReady)
    {
        FallbackReason = VulkanJitFallbackReason::TimingPropertiesNotReady;
        return 0;
    }
    if (!TimeDomainsReady || !TimingModel.HasTimeDomain())
    {
        FallbackReason = VulkanJitFallbackReason::TimeDomainsNotReady;
        return 0;
    }
    if (TargetTimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT && TargetPresentStage == 0)
    {
        FallbackReason = VulkanJitFallbackReason::NoValidTargetStage;
        return 0;
    }
    if (TargetPresentStage == 0)
    {
        FallbackReason = VulkanJitFallbackReason::NoValidTargetStage;
        return 0;
    }

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

u64 VulkanPresentPacer::PreparePresent(
    VkPresentInfoKHR& present, u64 preferredId, PresentMetadata& metadata)
{
    metadata = PresentMetadata{};
    if (!PresentId2Surface || Swapchain == VK_NULL_HANDLE)
        return 0;

    metadata.LogicalId = preferredId != 0 ? preferredId : LastSubmittedId + 1;
    LastSubmittedId = metadata.LogicalId;

    // The logical ID counts emulation frames; the sequence counts accepted
    // presents. Only the latter may be multiplied by the frame interval.
    metadata.Sequence = TimingModel.BeginPresent(metadata.LogicalId);

    if (PresentTimingRuntimeEnabled)
    {
        metadata.TargetTimeNs = EvaluateTargetTime(metadata.Sequence);
        metadata.Timing.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
        metadata.Timing.presentStageQueries = PresentStageQueries;
        if (metadata.TargetTimeNs != 0)
        {
            // NEAREST_REFRESH_CYCLE rather than a hand-written cadence: at 60
            // emulated FPS on a 144 Hz display the ideal present lands between
            // refresh cycles, and choosing which one is the presentation
            // engine's job, not the emulator's.
            metadata.Timing.flags =
                VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
            metadata.Timing.targetTime = metadata.TargetTimeNs;
            metadata.Timing.timeDomainId = TargetTimeDomainId;
            metadata.Timing.targetTimeDomainPresentStage =
                TargetTimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
                    ? TargetPresentStage
                    : 0;
            LastTargetTimeNs = metadata.TargetTimeNs;
            TargetSchedulingActive.store(true, std::memory_order_release);
        }
        else
        {
            // flags=0 with targetTime=0 is telemetry-only metadata: results are
            // still reported, but no presentation deadline is requested.
            TargetSchedulingActive.store(false, std::memory_order_release);
        }
        metadata.Timings.sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT;
        metadata.Timings.swapchainCount = 1;
        metadata.Timings.pTimingInfos = &metadata.Timing;
        metadata.Timings.pNext = present.pNext;
        present.pNext = &metadata.Timings;
        metadata.TimingAttached = true;
    }
    else
    {
        TargetSchedulingActive.store(false, std::memory_order_release);
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
    // the same wait semaphore, image, logical ID and presentation sequence --
    // the sequence is only committed once a present is actually accepted, so a
    // retry cannot leave a hole in the cadence.
    metadata.Id2.pNext = metadata.Timings.pNext;
    metadata.TimingAttached = false;
    metadata.TargetTimeNs = 0;
    ++TimingQueueFullCount;
    PresentTimingRuntimeEnabled = false;
    TargetSchedulingActive.store(false, std::memory_order_release);
    WaitDisabledReason = "present timing results queue full; timing metadata disabled";
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
        TimingModel.CommitPresent();
        return;
    }

    // A rejected present never reached the presentation engine, so it must not
    // consume a presentation sequence number.
    TimingModel.AbandonPresent();
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

VulkanTimingRefreshResult VulkanPresentPacer::RefreshTimingProperties()
{
    if (!PresentTimingSurface || Swapchain == VK_NULL_HANDLE
        || !Device->Fns().GetSwapchainTimingPropertiesEXT)
    {
        TimingPropertiesRetryPending = false;
        return VulkanTimingRefreshResult::Unavailable;
    }

    VkSwapchainTimingPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
    u64 counter = 0;
    const VkResult result = Device->Fns().GetSwapchainTimingPropertiesEXT(
        Device->GetHandle(), Swapchain, &properties, &counter);

    if (result == VK_NOT_READY)
    {
        // Documented and expected before the first present. Keep the pending
        // flag so BeginFrame retries once a present has been accepted.
        TimingPropertiesRetryPending = true;
        return VulkanTimingRefreshResult::NotReady;
    }
    if (result != VK_SUCCESS)
    {
        TimingPropertiesRetryPending = false;
        TimingPropertiesReady = false;
        TargetSchedulingLifecycleFailed = true;
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkGetSwapchainTimingPropertiesEXT failed (%s); target-time "
            "scheduling stays off and presentation falls back to host pacing\n",
            Vk::FormatResult(result).c_str());
        return VulkanTimingRefreshResult::Failed;
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

    return VulkanTimingRefreshResult::Updated;
}

bool VulkanPresentPacer::RefreshTimeDomains()
{
    if (!PresentTimingSurface || Swapchain == VK_NULL_HANDLE || !TimeDomainQueryAvailable)
    {
        TimeDomainsRetryPending = false;
        return false;
    }

    // Two-call enumeration, once per swapchain or per counter change. The
    // temporary vectors are deliberate: this never runs on the present path.
    VkSwapchainTimeDomainPropertiesEXT properties{};
    properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT;
    u64 counter = 0;
    VkResult result = Device->Fns().GetSwapchainTimeDomainPropertiesEXT(
        Device->GetHandle(), Swapchain, &properties, &counter);
    if (result == VK_NOT_READY)
    {
        TimeDomainsRetryPending = true;
        return false;
    }
    if (result != VK_SUCCESS || properties.timeDomainCount == 0)
    {
        TimeDomainsRetryPending = false;
        TimeDomainsReady = false;
        TimingModel.ClearTimeDomain();
        if (result != VK_SUCCESS)
        {
            TargetSchedulingLifecycleFailed = true;
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] vkGetSwapchainTimeDomainPropertiesEXT failed (%s); "
                "target-time scheduling stays off\n",
                Vk::FormatResult(result).c_str());
        }
        return false;
    }

    std::vector<VkTimeDomainKHR> domains(properties.timeDomainCount);
    std::vector<u64> domainIds(properties.timeDomainCount);
    properties.pTimeDomains = domains.data();
    properties.pTimeDomainIds = domainIds.data();
    result = Device->Fns().GetSwapchainTimeDomainPropertiesEXT(
        Device->GetHandle(), Swapchain, &properties, &counter);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        TimeDomainsRetryPending = false;
        TimeDomainsReady = false;
        TimingModel.ClearTimeDomain();
        return false;
    }

    const u32 count = std::min<u32>(properties.timeDomainCount,
                                    static_cast<u32>(domains.size()));

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

    const bool picked = pick(VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
        || pick(VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT);

    TimeDomainsCounter = counter;
    TimeDomainsRetryPending = false;
    if (!picked)
    {
        TimeDomainsReady = false;
        TargetTimeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
        TargetTimeDomainId = 0;
        TimingModel.ClearTimeDomain();
        return false;
    }

    SelectTargetPresentStage();
    TimeDomainsReady = true;
    // A domain change drops the baseline inside the model: a timestamp taken on
    // the old clock cannot be extrapolated on the new one.
    TimingModel.SetTimeDomain(static_cast<s32>(TargetTimeDomain), TargetTimeDomainId);
    return true;
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
        TargetSchedulingActive.store(false, std::memory_order_release);
        FallbackReason = VulkanJitFallbackReason::TimingQueryFailed;
        WaitDisabledReason = "present timing report query failed: " + Vk::FormatResult(result);
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] %s; timing metadata disabled\n", WaitDisabledReason.c_str());
        return;
    }

    // Swapchain timing properties and time domains are both allowed to change
    // while the swapchain lives -- refresh-rate change, fullscreen transition,
    // power state, VRR/FRR switch. The counters are the only notification the
    // extension gives, so they are checked on every drain.
    if (properties.timingPropertiesCounter != TimingPropertiesCounter)
        RefreshTimingProperties();
    if (properties.timeDomainsCounter != TimeDomainsCounter)
    {
        // Re-enumerating drops the baseline through SetTimeDomain, so a target
        // computed against an old timeDomainId can never be presented.
        FallbackReason = VulkanJitFallbackReason::DomainChanged;
        RefreshTimeDomains();
    }

    const u32 reportCount = std::min<u32>(
        properties.presentationTimingCount, static_cast<u32>(reports.size()));
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
            RefreshTimeDomains();
            break;
        }
    }

    // The first successful present is also the point where a swapchain that
    // answered VK_NOT_READY earlier can finally report its refresh timing.
    if (reportCount > 0)
    {
        if (TimingPropertiesRetryPending)
            RefreshTimingProperties();
        if (TimeDomainsRetryPending)
            RefreshTimeDomains();
    }

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    if (reportCount > 0)
    {
        if (++TimingReportCountdown < TimingLogPeriodFrames)
            return;
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
            static_cast<unsigned long long>(LastTargetTimeNs),
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
}

void VulkanPresentPacer::DisableWait(const char* reason)
{
    WaitRuntimeEnabled = false;
    WaitDisabledReason = reason ? reason : "runtime failure";
    Authority.store(static_cast<int>(VulkanPacingAuthority::GenericHost),
                    std::memory_order_release);
    TargetSchedulingActive.store(false, std::memory_order_release);
    Platform::Log(Platform::LogLevel::Warn,
        "[Vulkan] generic present wait disabled: %s; falling back to host pacing\n",
        WaitDisabledReason.c_str());
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
        "[Vulkan] present JIT: policy=%s authority=%s state=%s timingReady=%s "
        "timeDomainsReady=%s absoluteSupported=%s targetStage=%s timeDomain=%s "
        "domainId=%llu frameIntervalNs=%llu baselineId=%llu baselineSequence=%llu "
        "baselineTime=%llu targetTime=%llu fallback=%s\n",
        VulkanPresentPacingPolicyName(GetPolicy()),
        VulkanPacingAuthorityName(GetAuthority()),
        active ? "TargetSchedulingActive" : "TelemetryBootstrap",
        TimingPropertiesReady ? "yes" : "no",
        TimeDomainsReady ? "yes" : "no",
        PresentTimingAbsolute ? "yes" : "no",
        PresentStageName(TargetPresentStage),
        TimeDomainName(TargetTimeDomain),
        static_cast<unsigned long long>(TargetTimeDomainId),
        static_cast<unsigned long long>(TargetFrameIntervalNs),
        static_cast<unsigned long long>(TimingModel.GetBaselineLogicalId()),
        static_cast<unsigned long long>(TimingModel.GetBaselineSequence()),
        static_cast<unsigned long long>(TimingModel.GetBaselineStageTimeNs()),
        static_cast<unsigned long long>(LastTargetTimeNs),
        VulkanJitFallbackReasonName(FallbackReason));
#endif
}

void VulkanPresentPacer::LogState(const char* context) const
{
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] %s generic present pacing: policy=%s authority=%s caps2=%s "
        "present-id2=%s present-wait2=%s present-timing=%s absolute-timing=%s "
        "fifo-latest-ready=%s presentMode=%d reason=%s\n",
        context ? context : "state:",
        VulkanPresentPacingPolicyName(GetPolicy()),
        VulkanPacingAuthorityName(GetAuthority()),
        Caps2Available ? "yes" : "no",
        PresentId2Surface ? "yes" : "no",
        PresentWait2Surface ? "yes" : "no",
        PresentTimingRuntimeEnabled ? "yes" : "no",
        PresentTimingAbsolute ? "yes" : "no",
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

} // namespace melonDS

#endif
