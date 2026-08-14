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

#include "types.h"

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

// How a frame asks the presentation engine to schedule its display.
//
// The two modes mean genuinely different things and must not be treated as two
// clocks for the same quantity:
//
//   Absolute -- a point on the presentation timeline. "Show this at time T."
//   Relative -- a duration. "Keep the PREVIOUS image visible for at least N ns."
//
// Absolute is preferred because it is expressed directly in the same terms the
// feedback baseline is measured in, so a rebase corrects drift exactly.
// Relative exists because a surface may support only that -- which is the case
// on the NVIDIA driver this path was first validated against, where the device
// advertises presentAtAbsoluteTime but the surface does not support it.
enum class VulkanTargetSchedulingMode : int
{
    None = 0,
    Absolute,
    Relative,
};

// Which mutually-exclusive timing metadata implementation owns a present.
// This is intentionally separate from VulkanTargetSchedulingMode: Google uses
// absolute timestamps too, but has different IDs, feedback and lifecycle.
enum class VulkanPresentTimingBackend : int
{
    None = 0,
    ExtPresentTiming,
    GoogleDisplayTiming,
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
    // Neither absolute nor relative scheduling is available. Reported per
    // level, because "the driver cannot do it at all" and "this surface cannot
    // do it" lead to different conclusions. Falling back from absolute to
    // relative is NOT one of these: a working relative mode reports None.
    NoTargetTimingModeDevice,
    NoTargetTimingModeSurface,
    NonFifoPresentMode,
    NoFrameInterval,
    TimingPropertiesNotReady,
    TimeDomainsNotReady,
    NoValidTargetStage,
    // Absolute scheduling needs a measured presentation to project from.
    BootstrapWaitingForFeedback,
    // Relative scheduling needs a previous presentation to be relative to; the
    // spec ignores a relative target on a swapchain that has never presented.
    BootstrapWaitingForFirstPresent,
    DomainChanged,
    TimingQueryFailed,
    // Diagnostic only: the optional bounded wait is unavailable. This never
    // blocks target-time scheduling -- it is reported alongside it.
    PresentWait2Unsupported,
    // The finite present-timing results queue reached capacity and metadata
    // was paused deliberately. This is recoverable pressure, not a failed
    // GetPastPresentationTimingEXT query. Keep this new value at the end so
    // existing numeric fallback-reason values in capture CSVs remain stable.
    TimingQueuePressure,
};

// What happened during the pacer's pre-input phase.
//
// This is an enum rather than a bool because the two failure classes it has to
// report are not interchangeable. VK_ERROR_OUT_OF_DATE_KHR means the swapchain
// or surface changed and a rebuild fixes it; VK_ERROR_DEVICE_LOST means the
// device is gone, and rebuilding a swapchain on a lost device just repeats the
// failure. They shared one `true` result before, which quietly routed device
// loss into swapchain recreation.
enum class VulkanPacerBeginResult : int
{
    Continue = 0,
    SwapchainOutOfDate,
    DeviceLost,
    SurfaceLost,
};

// How the presenter must react to a begin result. Kept next to the enum so the
// routing itself is testable without a Vulkan device.
struct VulkanPacerBeginAction
{
    bool RebuildSwapchain = false;
    bool FailRenderer = false;
};

constexpr VulkanPacerBeginAction VulkanPacerActionFor(VulkanPacerBeginResult result) noexcept
{
    switch (result)
    {
    case VulkanPacerBeginResult::SwapchainOutOfDate:
        return {true, false};
    case VulkanPacerBeginResult::DeviceLost:
        // Deliberately not a rebuild: device loss belongs to the existing
        // Vulkan runtime-failure path, which tears the renderer down and
        // reports it, rather than to the swapchain recreation loop.
        return {false, true};
    case VulkanPacerBeginResult::SurfaceLost:
        return {false, true};
    case VulkanPacerBeginResult::Continue:
        break;
    }
    return {false, false};
}

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
    // True only when metadata was paused because the finite results queue
    // reached capacity. Keep this separate from TimingMetadataEnabled so the
    // diagnostic reason distinguishes pressure from a query failure.
    bool TimingQueuePressure = false;
    // vkCreateDevice enabled presentAtAbsoluteTime and the time-domain query
    // entry point resolved.
    bool AbsoluteTimingDevice = false;
    bool AbsoluteTimingSurface = false;
    // The same for presentAtRelativeTime. Kept as a separate pair rather than
    // an "any timing" flag because the surface can support one and not the
    // other, which is exactly the case this fallback exists for.
    bool RelativeTimingDevice = false;
    bool RelativeTimingSurface = false;
    // Target presentation time semantics are defined for the FIFO family only.
    bool FifoPresentMode = false;
    bool TimingPropertiesReady = false;
    bool TimeDomainsReady = false;
    bool TargetStageValid = false;
    // The emulator's own frame interval is known for this frame.
    bool FrameIntervalKnown = false;

    // --- VK_GOOGLE_display_timing fallback ---------------------------------
    bool GoogleDisplayTimingAvailable = false;
    bool GoogleDisplayTimingRuntimeEnabled = false;
    bool GoogleRefreshDurationReady = false;
    // Device-level VK_KHR_present_mode_fifo_latest_ready support. This mode
    // depends only on VK_KHR_swapchain plus its feature bit; it is not an EXT
    // present-timing dependency and can therefore pair with GOOGLE.
    bool LatestReadyDevice = false;
    // Sticky for this pacer/surface lifetime (including swapchain recreation).
    // This is a structural runtime failure, not the temporary bootstrap state
    // where timing properties or domains are simply not ready yet. Initialize
    // / Shutdown clears it when the device or surface lifetime ends.
    bool TargetSchedulingLifecycleFailed = false;
};

struct VulkanPacingDecision
{
    VulkanPacingAuthority Authority = VulkanPacingAuthority::GenericHost;
    // May vkWaitForPresent2KHR run before late input this frame.
    bool BoundedPresentWait = false;
    // Which kind of target the frame may request. `TargetTimeScheduling` is
    // derived from this so the two can never disagree -- the bool exists only
    // because most call sites just want "is a target being requested at all".
    VulkanTargetSchedulingMode TargetMode = VulkanTargetSchedulingMode::None;
    bool TargetTimeScheduling = false;
    VulkanJitFallbackReason Reason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
    // Diagnostic companion: the policy would take the bounded wait, but the
    // capability is missing. Never a reason to refuse target scheduling.
    bool OptionalWaitUnavailable = false;
    VulkanPresentTimingBackend TimingBackend = VulkanPresentTimingBackend::None;
};

// What a present actually carried, as opposed to what the resolver permitted.
//
// These are not the same thing and must not be recorded as if they were. The
// resolver can allow target scheduling for a frame that then requests no target
// at all -- absolute is still waiting for its feedback baseline, relative is
// still waiting for a first present, or a full timing-results queue made the
// present retry with its timing metadata stripped off. An A/B run judges
// "target scheduling active >= 95%" from this column, so recording the
// permission instead of the outcome would count misses as hits.
struct VulkanAppliedTarget
{
    bool Applied = false;
    VulkanTargetSchedulingMode Mode = VulkanTargetSchedulingMode::None;
    u64 ValueNs = 0;
};

// Derives the applied state from what the present info actually ended up
// carrying. Pure so the three cases that matter -- bootstrap, steady state, and
// queue-full retry -- are testable without a Vulkan device.
constexpr VulkanAppliedTarget ResolveVulkanAppliedTarget(
    bool timingAttached, VulkanTargetSchedulingMode mode, u64 valueNs) noexcept
{
    const bool applied = timingAttached
        && mode != VulkanTargetSchedulingMode::None
        && valueNs != 0;
    if (!applied)
        return {false, VulkanTargetSchedulingMode::None, 0};
    return {true, mode, valueNs};
}

// Absolute wins wherever it is available; relative is the fallback, not a
// preference. Absolute targets are expressed in the same units the feedback
// baseline measures, so drift is corrected exactly on every rebase, whereas a
// relative duration only constrains the gap between two presentations.
constexpr VulkanTargetSchedulingMode SelectVulkanTargetSchedulingMode(
    const VulkanPacingCapabilities& caps) noexcept
{
    if (caps.AbsoluteTimingDevice && caps.AbsoluteTimingSurface)
        return VulkanTargetSchedulingMode::Absolute;
    if (caps.RelativeTimingDevice && caps.RelativeTimingSurface)
        return VulkanTargetSchedulingMode::Relative;
    return VulkanTargetSchedulingMode::None;
}

// VK_EXT_present_timing is the primary implementation. Google is selected only
// when the EXT path is unavailable or has retired itself at runtime.
constexpr VulkanPresentTimingBackend SelectVulkanPresentTimingBackend(
    const VulkanPacingCapabilities& caps) noexcept
{
    if (caps.PresentTimingSurface && caps.TimingMetadataEnabled)
        return VulkanPresentTimingBackend::ExtPresentTiming;
    if (caps.GoogleDisplayTimingAvailable && caps.GoogleDisplayTimingRuntimeEnabled)
        return VulkanPresentTimingBackend::GoogleDisplayTiming;
    return VulkanPresentTimingBackend::None;
}

// Target scheduling has a stricter question than telemetry: EXT is preferred
// only when its target path can actually carry a present. An EXT surface that
// has metadata but no usable target mode (or no present_id2 correlation) must
// not hide a fully usable GOOGLE scheduler behind the EXT priority. The
// telemetry selector above intentionally remains EXT-first so TelemetryOnly
// keeps its existing backend preference.
constexpr VulkanPresentTimingBackend SelectVulkanPresentTargetBackend(
    const VulkanPacingCapabilities& caps) noexcept
{
    const bool extTargetCapable = caps.PresentTimingSurface
        && caps.TimingMetadataEnabled
        && caps.PresentId2Surface
        && !caps.TargetSchedulingLifecycleFailed
        && SelectVulkanTargetSchedulingMode(caps) != VulkanTargetSchedulingMode::None;
    if (extTargetCapable)
        return VulkanPresentTimingBackend::ExtPresentTiming;
    if (caps.GoogleDisplayTimingAvailable && caps.GoogleDisplayTimingRuntimeEnabled)
        return VulkanPresentTimingBackend::GoogleDisplayTiming;
    return VulkanPresentTimingBackend::None;
}

constexpr bool VulkanTargetCanUseFifoLatestReady(
    VulkanPresentPacingPolicy policy, const VulkanPacingCapabilities& caps) noexcept
{
    if (policy != VulkanPresentPacingPolicy::JustInTimeFifoLatestReady
        || !caps.LatestReadyDevice)
    {
        return false;
    }
    const VulkanPresentTimingBackend backend = SelectVulkanPresentTargetBackend(caps);
    return backend == VulkanPresentTimingBackend::ExtPresentTiming
        || backend == VulkanPresentTimingBackend::GoogleDisplayTiming;
}

constexpr bool VulkanPolicyRequestsTargetTime(VulkanPresentPacingPolicy policy) noexcept
{
    return policy == VulkanPresentPacingPolicy::JustInTime
        || policy == VulkanPresentPacingPolicy::JustInTimeFifoLatestReady;
}

// One policy-aware backend choice shared by the resolver and the runtime
// lifecycle. TelemetryOnly/PresentWait retain EXT telemetry priority, while
// JIT policies use the stricter target-capability selector. Keeping this as a
// pure helper prevents feedback polling from silently selecting a different
// backend than target metadata preparation.
constexpr VulkanPresentTimingBackend SelectVulkanPresentBackendForPolicy(
    VulkanPresentPacingPolicy policy, const VulkanPacingCapabilities& caps) noexcept
{
    return VulkanPolicyRequestsTargetTime(policy)
        ? SelectVulkanPresentTargetBackend(caps)
        : SelectVulkanPresentTimingBackend(caps);
}

constexpr bool VulkanShouldPollGoogleForFrame(
    VulkanPresentPacingPolicy policy,
    bool reflexActive,
    bool antiLagActive,
    bool normalSpeed,
    const VulkanPacingCapabilities& caps) noexcept
{
    if (reflexActive || antiLagActive || !normalSpeed)
        return false;
    return SelectVulkanPresentBackendForPolicy(policy, caps)
        == VulkanPresentTimingBackend::GoogleDisplayTiming;
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
    if (!caps.SwapchainValid)
        return VulkanJitFallbackReason::PresentTimingUnsupported;

    const VulkanPresentTimingBackend backend =
        SelectVulkanPresentBackendForPolicy(policy, caps);
    if (backend == VulkanPresentTimingBackend::None)
    {
        const bool googleUsable = caps.GoogleDisplayTimingAvailable
            && caps.GoogleDisplayTimingRuntimeEnabled;
        if (caps.PresentTimingSurface && caps.TimingMetadataEnabled && !googleUsable)
        {
            if (!caps.PresentId2Surface)
                return VulkanJitFallbackReason::PresentId2Unsupported;
            if (SelectVulkanTargetSchedulingMode(caps) == VulkanTargetSchedulingMode::None)
            {
                return (!caps.AbsoluteTimingDevice && !caps.RelativeTimingDevice)
                    ? VulkanJitFallbackReason::NoTargetTimingModeDevice
                    : VulkanJitFallbackReason::NoTargetTimingModeSurface;
            }
        }
        if (caps.TimingQueuePressure)
            return VulkanJitFallbackReason::TimingQueuePressure;
        if (caps.GoogleDisplayTimingAvailable
            && !caps.GoogleDisplayTimingRuntimeEnabled)
        {
            return VulkanJitFallbackReason::TimingQueryFailed;
        }
        if (caps.GoogleDisplayTimingAvailable && !caps.GoogleRefreshDurationReady)
            return VulkanJitFallbackReason::TimingPropertiesNotReady;
        // A surface that advertised present timing but is no longer reporting
        // failed at runtime; one that never advertised it simply lacks it.
        return caps.PresentTimingSurface
            ? VulkanJitFallbackReason::TimingQueryFailed
            : VulkanJitFallbackReason::PresentTimingUnsupported;
    }
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming
        && !caps.PresentId2Surface)
    {
        return VulkanJitFallbackReason::PresentId2Unsupported;
    }
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming
        && SelectVulkanTargetSchedulingMode(caps) == VulkanTargetSchedulingMode::None)
    {
        // Falling back from absolute to relative is a supported outcome and
        // never lands here; reaching this means neither mode is usable.
        return (!caps.AbsoluteTimingDevice && !caps.RelativeTimingDevice)
            ? VulkanJitFallbackReason::NoTargetTimingModeDevice
            : VulkanJitFallbackReason::NoTargetTimingModeSurface;
    }
    if (!caps.FifoPresentMode)
        return VulkanJitFallbackReason::NonFifoPresentMode;
    if (!caps.FrameIntervalKnown)
        return VulkanJitFallbackReason::NoFrameInterval;
    if (backend == VulkanPresentTimingBackend::GoogleDisplayTiming
        && !caps.GoogleRefreshDurationReady)
    {
        return VulkanJitFallbackReason::TimingPropertiesNotReady;
    }
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming
        && !caps.TimingPropertiesReady)
        return VulkanJitFallbackReason::TimingPropertiesNotReady;
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming && !caps.TimeDomainsReady)
        return VulkanJitFallbackReason::TimeDomainsNotReady;
    if (backend == VulkanPresentTimingBackend::ExtPresentTiming && !caps.TargetStageValid)
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
    constexpr VulkanTargetSchedulingMode noMode = VulkanTargetSchedulingMode::None;
    constexpr VulkanPresentTimingBackend noBackend = VulkanPresentTimingBackend::None;
    if (reflexActive)
    {
        return {VulkanPacingAuthority::NvidiaReflex, false, noMode, false,
                VulkanJitFallbackReason::VendorLatencyApiOwnsPacing, false, noBackend};
    }
    if (antiLagActive)
    {
        return {VulkanPacingAuthority::AmdAntiLag2, false, noMode, false,
                VulkanJitFallbackReason::VendorLatencyApiOwnsPacing, false, noBackend};
    }
    const VulkanPresentTimingBackend backend =
        SelectVulkanPresentBackendForPolicy(policy, caps);
    if (!normalSpeed)
    {
        // Fast-forward and slow motion are not presentation problems. Neither
        // mechanism may hold frames to a cadence the emulator is not running at.
        return {VulkanPacingAuthority::GenericHost, false, noMode, false,
                VulkanJitFallbackReason::NotNormalSpeed, false, noBackend};
    }
    if (policy == VulkanPresentPacingPolicy::TelemetryOnly)
    {
        return {VulkanPacingAuthority::GenericHost, false, noMode, false,
                VulkanJitFallbackReason::TelemetryOnlyPolicy, false, backend};
    }
    if (!caps.SwapchainValid)
    {
        return {VulkanPacingAuthority::GenericHost, false, noMode, false,
                VulkanJitFallbackReason::PresentTimingUnsupported, false, noBackend};
    }

    const bool wait = caps.PresentId2Surface
        && caps.PresentWait2Surface && caps.PresentWaitRuntimeEnabled;
    const VulkanJitFallbackReason reason = ClassifyVulkanTargetFallback(policy, caps);
    const bool target = VulkanPolicyRequestsTargetTime(policy)
        && reason == VulkanJitFallbackReason::None;
    const VulkanTargetSchedulingMode mode = target
        ? (backend == VulkanPresentTimingBackend::GoogleDisplayTiming
            ? VulkanTargetSchedulingMode::Absolute
            : SelectVulkanTargetSchedulingMode(caps))
        : noMode;

    // The authority exists to keep exactly one owner of optional late waiting
    // and presentation scheduling. Either mechanism alone is enough to claim it.
    const VulkanPacingAuthority authority = (wait || target)
        ? VulkanPacingAuthority::GenericPresentTiming
        : VulkanPacingAuthority::GenericHost;
    return {authority, wait, mode, target, reason, !wait, backend};
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENT_PACING_POLICY_H
