/*
    Copyright 2016-2026 melonDS team

    Vendor-neutral Vulkan presentation pacing policy resolution.

    This header holds the decision -- which pacing authority owns the frame, may
    a bounded present wait run, and may a target presentation time be requested
    -- as pure constexpr logic over plain bools. It has no Vulkan includes on
    purpose, so the capability matrix is executable on any host
    (tools/testing/vulkan-present-timing-tests.cpp).

    The reason it is separated at all: `VK_KHR_present_wait2` and
    `VK_EXT_present_timing` are independent capabilities that were once
    conflated here. Waiting on the previous present and scheduling this
    present's display time are different mechanisms with different extension
    dependencies, and a driver that exposes only the latter must still get
    target-time presentation.
*/

#ifndef VULKAN_PRESENT_PACING_POLICY_H
#define VULKAN_PRESENT_PACING_POLICY_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

namespace melonDS
{

enum class VulkanPresentPacingPolicy : int
{
    TelemetryOnly = 0,
    PresentWait = 1,
    JustInTime = 2,
    JustInTimeFifoLatestReady = 3,
};

enum class VulkanPacingAuthority : int
{
    GenericHost = 0,
    NvidiaReflex,
    AmdAntiLag2,
    GenericPresentTiming,
};

// Why a target presentation time was not requested. Reported in the developer
// summary so an A/B session can tell "the policy does not ask for one" apart
// from "this driver never answered".
enum class VulkanJitFallbackReason : int
{
    None = 0,
    TelemetryOnlyPolicy,
    PresentWaitPolicyNoTarget,
    VendorLatencyApiOwnsPacing,
    NotNormalSpeed,
    PresentId2Unsupported,
    PresentTimingUnsupported,
    AbsoluteTimingUnsupportedDevice,
    AbsoluteTimingUnsupportedSurface,
    NonFifoPresentMode,
    NoFrameInterval,
    TimingPropertiesNotReady,
    TimeDomainsNotReady,
    NoValidTargetStage,
    BootstrapWaitingForFeedback,
    DomainChanged,
    TimingQueryFailed,
    // Diagnostic only: the optional bounded wait is unavailable. This never
    // blocks target-time scheduling -- it is reported alongside it.
    PresentWait2Unsupported,
};

// Everything the pacing decision depends on that is not a per-frame value.
// Split into device-level and surface-level members wherever the extension
// draws that line, because a capability can be present on one and not the other.
struct VulkanPacingCapabilities
{
    bool SwapchainValid = false;
    // VK_KHR_present_id2 on this surface. Every generic path needs it: it is
    // what correlates a present with its wait and its timing report.
    bool PresentId2Surface = false;

    // --- bounded previous-present wait (VK_KHR_present_wait2) ---------------
    bool PresentWait2Surface = false;
    // Cleared when a runtime failure retired the wait; the rest of the pacer
    // keeps working.
    bool PresentWaitRuntimeEnabled = false;

    // --- target-time scheduling (VK_EXT_present_timing) --------------------
    bool PresentTimingSurface = false;
    // Cleared when the results queue filled or the query failed.
    bool TimingMetadataEnabled = false;
    // vkCreateDevice enabled presentAtAbsoluteTime and the time-domain query
    // entry point resolved.
    bool AbsoluteTimingDevice = false;
    bool AbsoluteTimingSurface = false;
    // Target presentation time semantics are defined for the FIFO family only.
    bool FifoPresentMode = false;
    bool TimingPropertiesReady = false;
    bool TimeDomainsReady = false;
    bool TargetStageValid = false;
    // The emulator's own frame interval is known for this frame.
    bool FrameIntervalKnown = false;
};

struct VulkanPacingDecision
{
    VulkanPacingAuthority Authority = VulkanPacingAuthority::GenericHost;
    // May vkWaitForPresent2KHR run before late input this frame.
    bool BoundedPresentWait = false;
    // May VkPresentTimingInfoEXT carry a non-zero targetTime this frame, once a
    // feedback baseline exists.
    bool TargetTimeScheduling = false;
    VulkanJitFallbackReason Reason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
    // Diagnostic companion: the policy would take the bounded wait, but the
    // capability is missing. Never a reason to refuse target scheduling.
    bool OptionalWaitUnavailable = false;
};

constexpr bool VulkanPolicyRequestsTargetTime(VulkanPresentPacingPolicy policy) noexcept
{
    return policy == VulkanPresentPacingPolicy::JustInTime
        || policy == VulkanPresentPacingPolicy::JustInTimeFifoLatestReady;
}

// The first missing prerequisite for absolute target-time scheduling, in the
// order a reader would want to debug them. Deliberately does NOT consider
// VK_KHR_present_wait2: target-time presentation does not depend on it.
constexpr VulkanJitFallbackReason ClassifyVulkanTargetFallback(
    VulkanPresentPacingPolicy policy, const VulkanPacingCapabilities& caps) noexcept
{
    if (policy == VulkanPresentPacingPolicy::TelemetryOnly)
        return VulkanJitFallbackReason::TelemetryOnlyPolicy;
    if (policy == VulkanPresentPacingPolicy::PresentWait)
        return VulkanJitFallbackReason::PresentWaitPolicyNoTarget;
    if (!caps.SwapchainValid || !caps.PresentId2Surface)
        return VulkanJitFallbackReason::PresentId2Unsupported;
    if (!caps.TimingMetadataEnabled)
    {
        // A surface that advertised present timing but is no longer reporting
        // failed at runtime; one that never advertised it simply lacks it.
        return caps.PresentTimingSurface
            ? VulkanJitFallbackReason::TimingQueryFailed
            : VulkanJitFallbackReason::PresentTimingUnsupported;
    }
    if (!caps.AbsoluteTimingDevice)
        return VulkanJitFallbackReason::AbsoluteTimingUnsupportedDevice;
    if (!caps.AbsoluteTimingSurface)
        return VulkanJitFallbackReason::AbsoluteTimingUnsupportedSurface;
    if (!caps.FifoPresentMode)
        return VulkanJitFallbackReason::NonFifoPresentMode;
    if (!caps.FrameIntervalKnown)
        return VulkanJitFallbackReason::NoFrameInterval;
    if (!caps.TimingPropertiesReady)
        return VulkanJitFallbackReason::TimingPropertiesNotReady;
    if (!caps.TimeDomainsReady)
        return VulkanJitFallbackReason::TimeDomainsNotReady;
    if (!caps.TargetStageValid)
        return VulkanJitFallbackReason::NoValidTargetStage;
    return VulkanJitFallbackReason::None;
}

// One decision per frame, covering both generic mechanisms.
//
// Vendor latency APIs win outright: Reflex and Anti-Lag 2 already own frame
// pacing end to end, and layering a second scheduler on top would fight their
// driver-side model. Everything below them is optional and independently
// gated, so a driver missing one capability keeps the other.
constexpr VulkanPacingDecision ResolveVulkanPresentPacing(
    VulkanPresentPacingPolicy policy,
    bool reflexActive,
    bool antiLagActive,
    bool normalSpeed,
    const VulkanPacingCapabilities& caps) noexcept
{
    if (reflexActive)
    {
        return {VulkanPacingAuthority::NvidiaReflex, false, false,
                VulkanJitFallbackReason::VendorLatencyApiOwnsPacing, false};
    }
    if (antiLagActive)
    {
        return {VulkanPacingAuthority::AmdAntiLag2, false, false,
                VulkanJitFallbackReason::VendorLatencyApiOwnsPacing, false};
    }
    if (!normalSpeed)
    {
        // Fast-forward and slow motion are not presentation problems. Neither
        // mechanism may hold frames to a cadence the emulator is not running at.
        return {VulkanPacingAuthority::GenericHost, false, false,
                VulkanJitFallbackReason::NotNormalSpeed, false};
    }
    if (policy == VulkanPresentPacingPolicy::TelemetryOnly)
    {
        return {VulkanPacingAuthority::GenericHost, false, false,
                VulkanJitFallbackReason::TelemetryOnlyPolicy, false};
    }
    if (!caps.SwapchainValid || !caps.PresentId2Surface)
    {
        return {VulkanPacingAuthority::GenericHost, false, false,
                VulkanJitFallbackReason::PresentId2Unsupported, false};
    }

    const bool wait = caps.PresentWait2Surface && caps.PresentWaitRuntimeEnabled;
    const VulkanJitFallbackReason reason = ClassifyVulkanTargetFallback(policy, caps);
    const bool target = VulkanPolicyRequestsTargetTime(policy)
        && reason == VulkanJitFallbackReason::None;

    // The authority exists to keep exactly one owner of optional late waiting
    // and presentation scheduling. Either mechanism alone is enough to claim it.
    const VulkanPacingAuthority authority = (wait || target)
        ? VulkanPacingAuthority::GenericPresentTiming
        : VulkanPacingAuthority::GenericHost;
    return {authority, wait, target, reason, !wait};
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENT_PACING_POLICY_H
