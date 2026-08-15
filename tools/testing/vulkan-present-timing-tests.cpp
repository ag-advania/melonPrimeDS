/*
    Hardware-independent contract tests for MelonPrime's Vulkan target-time
    presentation scheduling.

    Two pure pieces are covered. VulkanPresentTimingModel is the arithmetic a
    static audit cannot check: sequence accounting, baseline rebasing, staleness
    and overflow. VulkanPresentPacingPolicy is the capability matrix -- which
    driver gets the bounded wait, which gets target-time scheduling, and which
    gets both. Neither holds Vulkan objects, so these tests run on any host with
    no driver, no GPU and no window system.
*/

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "VulkanPresentPacingPolicy.h"
#include "VulkanPresentPacer.h"
#include "VulkanPresenterFrameBudget.h"
#include "VulkanGoogleDisplayTimingModel.h"
#include "VulkanPresentTimingModel.h"

namespace
{
using namespace melonDS;

// The two time-domain identities used throughout. Values are opaque to the
// model; only equality matters.
constexpr s32 DomainSwapchainLocal = 1000208001;
constexpr s32 DomainPresentStageLocal = 1000208000;
constexpr u64 DomainId = 7;

// 60 FPS in nanoseconds, as the frame limiter's step rounds it. Deliberately
// spelled out here rather than imported: the production code must derive this
// from storedFrametimeStep, and a test that shared a constant with it could not
// notice a hard-coded 60 FPS creeping back in.
constexpr u64 Interval60Fps = 16'666'667;

int Failures = 0;

void Require(bool condition, const std::string& message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    ++Failures;
}

// Presents one frame end to end: reserve a sequence, accept it.
u64 PresentAccepted(VulkanPresentTimingModel& model, u64 logicalId)
{
    const u64 sequence = model.BeginPresent(logicalId);
    model.CommitPresent();
    return sequence;
}

VulkanPresentTimingModel MakeModel()
{
    VulkanPresentTimingModel model;
    model.SetTimeDomain(DomainSwapchainLocal, DomainId);
    return model;
}


// A steady 60 FPS stream produces targets exactly one frame interval apart, and
// the display's refresh rate never enters the calculation: the same baseline
// and the same interval must give the same answer whether the monitor runs at
// 60, 120 or 144 Hz. The refresh cadence is the presentation engine's problem,
// which is why the production path asks for NEAREST_REFRESH_CYCLE.
void TestSteadyStateTargets()
{
    VulkanPresentTimingModel model = MakeModel();

    const u64 first = PresentAccepted(model, 100);
    Require(first == 1, "the first accepted present must occupy sequence 1");
    Require(model.ComputeTargetTime(first + 1, Interval60Fps) == 0,
        "no target may be requested before any feedback has arrived");

    Require(model.RecordFeedback(100, 1'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Accepted,
        "a complete report for a known present must establish the baseline");

    Require(model.ComputeTargetTime(2, Interval60Fps) == 1'016'666'667,
        "one frame past the baseline must target one interval later");
    Require(model.ComputeTargetTime(3, Interval60Fps) == 1'033'333'334,
        "two frames past the baseline must target two intervals later");
    Require(model.ComputeTargetTime(4, Interval60Fps) == 1'050'000'001,
        "three frames past the baseline must target three intervals later");
}


// The logical present ID is the Reflex frame ID, which counts emulation frames
// and skips values whenever a frame is simulated but never presented. Target
// times must advance by presentation sequence instead, or every dropped frame
// permanently shifts the requested cadence forward.
void TestLogicalIdGapIsNotASequenceGap()
{
    VulkanPresentTimingModel model = MakeModel();

    PresentAccepted(model, 500);
    // Frames 501..509 were simulated but not presented.
    const u64 second = PresentAccepted(model, 510);
    Require(second == 2, "a logical ID jump must still advance the sequence by one");

    Require(model.RecordFeedback(500, 2'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Accepted,
        "feedback must resolve through the ID-to-sequence history");
    Require(model.GetBaselineSequence() == 1,
        "the baseline must record the presentation sequence, not the logical ID");

    Require(model.ComputeTargetTime(2, Interval60Fps) == 2'000'000'000 + Interval60Fps,
        "a ten-frame logical ID gap must move the target by one frame interval only");
}


// VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT does not enqueue the image. Retrying
// with the same image and logical ID must reuse the same presentation sequence:
// counting the retry would leave a permanent one-frame hole in the cadence.
// (The wait semaphores are the one thing the retry may not reuse -- the
// rejected call still enqueued their waits. That is enforced as a source
// contract in audit-low-latency-contract.py, since it lives in the Vulkan call
// path rather than in this device-free model.)
void TestRetryReusesSequence()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);

    const u64 attempt = model.BeginPresent(2);
    Require(attempt == 2, "a new present must reserve the next sequence");
    // The queue-full path retries the same present without timing metadata.
    const u64 retry = model.BeginPresent(2);
    Require(retry == 2, "a retry of the same present must reuse its sequence");
    model.CommitPresent();
    Require(model.GetCommittedSequence() == 2,
        "one accepted present must advance the committed sequence exactly once");
}


// A present the engine rejected never happened. Its reserved sequence must be
// released so the next attempt takes it, otherwise the model believes a frame
// was displayed that never was.
void TestRejectedPresentReleasesSequence()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);

    model.BeginPresent(2);
    model.AbandonPresent();
    Require(model.GetCommittedSequence() == 1,
        "a rejected present must not advance the committed sequence");
    Require(PresentAccepted(model, 3) == 2,
        "the next accepted present must reuse the released sequence");
}


// Rebasing on every complete report is what keeps rounding error from
// accumulating; each target stays a few frames of extrapolation from a real
// measured presentation time.
void TestBaselineRebase()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);
    PresentAccepted(model, 2);
    PresentAccepted(model, 3);

    model.RecordFeedback(1, 1'000'000'000, DomainSwapchainLocal, DomainId);
    Require(model.GetBaselineSequence() == 1, "the first report must set the baseline");

    // The real presentation of sequence 3 drifted 400 us late.
    Require(model.RecordFeedback(3, 1'033'733'334, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Accepted,
        "a newer report must rebase the baseline");
    Require(model.GetBaselineSequence() == 3 && model.GetBaselineStageTimeNs() == 1'033'733'334,
        "the baseline must move onto the newest report");
    Require(model.ComputeTargetTime(4, Interval60Fps) == 1'033'733'334 + Interval60Fps,
        "targets must be projected from the rebased baseline, not the first one");

    // Out-of-order results are permitted by the extension; an older report must
    // not drag the baseline backwards.
    Require(model.RecordFeedback(2, 1'016'666'667, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Ignored,
        "an out-of-order older report must not replace a newer baseline");
    Require(model.GetBaselineSequence() == 3, "the newer baseline must survive");
}


// The driver may answer in a different time domain than the one requested.
// Projecting a target on a clock the timestamps did not come from is worse than
// not scheduling at all, so the baseline is dropped and the caller re-enumerates.
void TestDomainMismatchAndChange()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);
    model.RecordFeedback(1, 1'000'000'000, DomainSwapchainLocal, DomainId);
    Require(model.HasBaseline(), "the baseline must exist before the mismatch");

    PresentAccepted(model, 2);
    Require(model.RecordFeedback(2, 1'016'666'667, DomainPresentStageLocal, DomainId)
            == VulkanPresentFeedbackResult::DomainMismatch,
        "a report from another domain must be reported as a mismatch");
    Require(!model.HasBaseline(), "a domain mismatch must invalidate the baseline");

    // Re-selecting the same domain is a no-op; selecting a new one drops the
    // baseline because an old timestamp cannot be projected on a new clock.
    model.RecordFeedback(2, 1'016'666'667, DomainSwapchainLocal, DomainId);
    Require(model.HasBaseline(), "the baseline must be recoverable after re-selection");
    model.SetTimeDomain(DomainSwapchainLocal, DomainId);
    Require(model.HasBaseline(), "re-selecting the same domain must not drop the baseline");
    model.SetTimeDomain(DomainSwapchainLocal, DomainId + 1);
    Require(!model.HasBaseline(), "a changed time-domain ID must drop the baseline");
}


// Swapchain recreation invalidates the whole presentation timeline: sequence
// numbering, ID history and every measured timestamp.
void TestResetOnSwapchainRecreation()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);
    model.RecordFeedback(1, 1'000'000'000, DomainSwapchainLocal, DomainId);
    Require(model.HasBaseline() && model.GetCommittedSequence() == 1,
        "state must exist before the reset");

    model.Reset();
    Require(!model.HasBaseline() && model.GetCommittedSequence() == 0,
        "a reset must clear both the baseline and the sequence numbering");
    Require(model.ComputeTargetTime(1, Interval60Fps) == 0,
        "no target may be requested on a freshly reset model");

    // The old logical ID must no longer resolve to anything.
    model.SetTimeDomain(DomainSwapchainLocal, DomainId);
    Require(model.RecordFeedback(1, 2'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Ignored,
        "a report for a present from the retired swapchain must be ignored");
}


// Every rejection path must answer with zero rather than a wrong time. Zero
// means "present as usual", which is exactly the telemetry-only behaviour.
void TestGuards()
{
    VulkanPresentTimingModel model = MakeModel();
    PresentAccepted(model, 1);
    model.RecordFeedback(1, 1'000'000'000, DomainSwapchainLocal, DomainId);

    Require(model.ComputeTargetTime(2, 0) == 0,
        "a zero frame interval must never produce a target");
    Require(model.ComputeTargetTime(1, Interval60Fps) == 0,
        "a target at or behind the baseline must be refused");
    Require(model.ComputeTargetTime(0, Interval60Fps) == 0,
        "sequence zero is not a present and must be refused");

    const u64 stale = 1 + VulkanPresentTimingModel::MaxProjectedFrames + 1;
    Require(model.ComputeTargetTime(stale, Interval60Fps) == 0,
        "extrapolating past the projection limit must be refused");
    Require(model.IsBaselineStale(stale),
        "a baseline past the projection limit must report itself stale");
    Require(!model.IsBaselineStale(2), "a fresh baseline must not report itself stale");

    // Overflow: a baseline near the top of the range plus any projection.
    VulkanPresentTimingModel overflow = MakeModel();
    PresentAccepted(overflow, 1);
    PresentAccepted(overflow, 2);
    overflow.RecordFeedback(
        1, (std::numeric_limits<u64>::max)() - 1000, DomainSwapchainLocal, DomainId);
    Require(overflow.ComputeTargetTime(2, Interval60Fps) == 0,
        "a target time that would overflow must be refused");

    // Reports that carry no usable data.
    Require(model.RecordFeedback(0, 1'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Ignored,
        "a report without a present ID must be ignored");
    Require(model.RecordFeedback(1, 0, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Ignored,
        "a zero timestamp means 'not measured' and must be ignored");
    Require(model.RecordFeedback(9999, 2'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Ignored,
        "a report for an unknown present ID must be ignored");
}


// The ID-to-sequence history is a fixed ring on the present path. Once it wraps,
// the oldest entries stop resolving -- which must degrade to "no baseline
// update", never to a wrong sequence.
void TestHistoryWraparound()
{
    VulkanPresentTimingModel model = MakeModel();
    const u64 count = VulkanPresentTimingModel::HistorySize + 8;
    for (u64 i = 1; i <= count; ++i)
        PresentAccepted(model, i);

    u64 sequence = 0;
    Require(!model.FindSequence(1, sequence),
        "an evicted logical ID must no longer resolve");
    Require(model.FindSequence(count, sequence) && sequence == count,
        "the newest logical ID must still resolve to its sequence");
    Require(model.RecordFeedback(count, 5'000'000'000, DomainSwapchainLocal, DomainId)
            == VulkanPresentFeedbackResult::Accepted,
        "feedback for a still-known present must be accepted after wraparound");
}


// ---------------------------------------------------------------------------
// Capability matrix: VulkanPresentPacingPolicy
// ---------------------------------------------------------------------------

using Policy = VulkanPresentPacingPolicy;
using Authority = VulkanPacingAuthority;
using Reason = VulkanJitFallbackReason;
using TargetMode = VulkanTargetSchedulingMode;
using TimingBackend = VulkanPresentTimingBackend;

// A driver that supports everything, with a swapchain that is up and reporting.
VulkanPacingCapabilities FullCapabilities()
{
    VulkanPacingCapabilities caps;
    caps.SwapchainValid = true;
    caps.PresentId2Surface = true;
    caps.PresentWait2Surface = true;
    caps.PresentWaitRuntimeEnabled = true;
    caps.PresentTimingSurface = true;
    caps.TimingMetadataEnabled = true;
    caps.AbsoluteTimingDevice = true;
    caps.AbsoluteTimingSurface = true;
    caps.RelativeTimingDevice = true;
    caps.RelativeTimingSurface = true;
    caps.FifoPresentMode = true;
    caps.TimingPropertiesReady = true;
    caps.TimeDomainsReady = true;
    caps.TargetStageValid = true;
    caps.FrameIntervalKnown = true;
    caps.LatestReadyDevice = true;
    return caps;
}

VulkanPacingDecision Resolve(Policy policy, const VulkanPacingCapabilities& caps)
{
    return ResolveVulkanPresentPacing(policy, false, false, true, caps);
}

VulkanPacingCapabilities GoogleCapabilities()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.PresentId2Surface = false;
    caps.PresentWait2Surface = false;
    caps.PresentWaitRuntimeEnabled = false;
    caps.PresentTimingSurface = false;
    caps.TimingMetadataEnabled = false;
    caps.AbsoluteTimingDevice = false;
    caps.AbsoluteTimingSurface = false;
    caps.RelativeTimingDevice = false;
    caps.RelativeTimingSurface = false;
    caps.TimingPropertiesReady = false;
    caps.TimeDomainsReady = false;
    caps.TargetStageValid = false;
    caps.GoogleDisplayTimingAvailable = true;
    caps.GoogleDisplayTimingRuntimeEnabled = true;
    caps.GoogleRefreshDurationReady = true;
    return caps;
}

void TestTimingBackendSelection()
{
    VulkanPacingCapabilities both = FullCapabilities();
    both.GoogleDisplayTimingAvailable = true;
    both.GoogleDisplayTimingRuntimeEnabled = true;
    both.GoogleRefreshDurationReady = true;
    Require(SelectVulkanPresentTimingBackend(both) == TimingBackend::ExtPresentTiming,
        "VK_EXT_present_timing must win when EXT and GOOGLE are both usable");

    // EXT metadata is still preferred for telemetry, but an EXT surface that
    // cannot schedule either target mode must not block GOOGLE JIT fallback.
    VulkanPacingCapabilities mixed = both;
    mixed.AbsoluteTimingDevice = false;
    mixed.RelativeTimingDevice = false;
    Require(SelectVulkanPresentTimingBackend(mixed) == TimingBackend::ExtPresentTiming,
        "telemetry must retain EXT priority when both metadata backends exist");
    Require(SelectVulkanPresentTargetBackend(mixed) == TimingBackend::GoogleDisplayTiming,
        "target selection must fall back from target-incapable EXT to GOOGLE");
    const VulkanPacingDecision mixedDecision = Resolve(Policy::JustInTime, mixed);
    Require(mixedDecision.TimingBackend == TimingBackend::GoogleDisplayTiming
            && mixedDecision.TargetTimeScheduling
            && mixedDecision.TargetMode == TargetMode::Absolute,
        "mixed EXT metadata and GOOGLE target capability must schedule through GOOGLE");

    mixed = both;
    mixed.PresentId2Surface = false;
    Require(SelectVulkanPresentTargetBackend(mixed) == TimingBackend::GoogleDisplayTiming,
        "missing EXT present_id2 correlation must not suppress GOOGLE targeting");

    mixed = both;
    mixed.TimingPropertiesReady = false;
    mixed.TimeDomainsReady = false;
    Require(SelectVulkanPresentTargetBackend(mixed) == TimingBackend::ExtPresentTiming,
        "EXT bootstrap readiness must not flap the target backend to GOOGLE");

    const VulkanPacingCapabilities google = GoogleCapabilities();
    const VulkanPacingDecision googleDecision = Resolve(Policy::JustInTime, google);
    Require(googleDecision.TimingBackend == TimingBackend::GoogleDisplayTiming,
        "GOOGLE must be selected when EXT is absent");
    Require(googleDecision.TargetTimeScheduling
            && googleDecision.TargetMode == TargetMode::Absolute,
        "GOOGLE JIT must schedule absolute monotonic timestamps");
    Require(!googleDecision.BoundedPresentWait,
        "GOOGLE scheduling must not acquire an unsupported present_id2 wait");

    VulkanPacingCapabilities neither = google;
    neither.GoogleDisplayTimingAvailable = false;
    neither.GoogleDisplayTimingRuntimeEnabled = false;
    Require(SelectVulkanPresentTimingBackend(neither) == TimingBackend::None,
        "the backend must be none when neither extension is usable");

    const VulkanPacingDecision reflex = ResolveVulkanPresentPacing(
        Policy::JustInTime, true, false, true, google);
    const VulkanPacingDecision antiLag = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, true, true, google);
    Require(reflex.TimingBackend == TimingBackend::None
            && antiLag.TimingBackend == TimingBackend::None,
        "vendor latency APIs must suppress GOOGLE metadata, not stack with it");

    const VulkanPacingDecision telemetry = Resolve(Policy::TelemetryOnly, google);
    Require(!telemetry.TargetTimeScheduling
            && telemetry.TimingBackend == TimingBackend::GoogleDisplayTiming,
        "TelemetryOnly may collect GOOGLE feedback but must request no target");

    const VulkanPacingDecision abnormal = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, false, false, google);
    Require(!abnormal.TargetTimeScheduling
            && abnormal.TimingBackend == TimingBackend::None,
        "fast-forward and slow motion must suppress the GOOGLE backend entirely");

    VulkanPacingCapabilities nonFifo = google;
    nonFifo.FifoPresentMode = false;
    Require(!Resolve(Policy::JustInTime, nonFifo).TargetTimeScheduling,
        "VSync-off present modes must suppress GOOGLE targets");
}

void TestFifoLatestReadyBackendCompatibility()
{
    const VulkanPacingCapabilities google = GoogleCapabilities();
    Require(VulkanTargetCanUseFifoLatestReady(
                Policy::JustInTimeFifoLatestReady, google),
        "GOOGLE target scheduling must make FIFO_LATEST_READY eligible without EXT");

    VulkanPacingCapabilities noLatestReady = google;
    noLatestReady.LatestReadyDevice = false;
    Require(!VulkanTargetCanUseFifoLatestReady(
                 Policy::JustInTimeFifoLatestReady, noLatestReady),
        "FIFO_LATEST_READY must remain off when its feature is unavailable");

    const VulkanPacingCapabilities ext = FullCapabilities();
    Require(VulkanTargetCanUseFifoLatestReady(
                Policy::JustInTimeFifoLatestReady, ext),
        "EXT target scheduling must retain FIFO_LATEST_READY eligibility");

    VulkanPacingCapabilities mixed = ext;
    mixed.AbsoluteTimingDevice = false;
    mixed.RelativeTimingDevice = false;
    mixed.GoogleDisplayTimingAvailable = true;
    mixed.GoogleDisplayTimingRuntimeEnabled = true;
    mixed.GoogleRefreshDurationReady = true;
    Require(SelectVulkanPresentTargetBackend(mixed) == TimingBackend::GoogleDisplayTiming
            && VulkanTargetCanUseFifoLatestReady(
                Policy::JustInTimeFifoLatestReady, mixed),
        "target-incapable EXT plus GOOGLE must retain FIFO_LATEST_READY eligibility");

    mixed = ext;
    mixed.TargetSchedulingLifecycleFailed = true;
    mixed.GoogleDisplayTimingAvailable = true;
    mixed.GoogleDisplayTimingRuntimeEnabled = true;
    Require(SelectVulkanPresentTargetBackend(mixed) == TimingBackend::GoogleDisplayTiming,
        "a failed EXT timing lifecycle must still allow GOOGLE target fallback");
}

void TestPolicyAwareGooglePolling()
{
    VulkanPacingCapabilities mixed = FullCapabilities();
    mixed.AbsoluteTimingDevice = false;
    mixed.RelativeTimingDevice = false;
    mixed.GoogleDisplayTimingAvailable = true;
    mixed.GoogleDisplayTimingRuntimeEnabled = true;
    mixed.GoogleRefreshDurationReady = false;

    Require(SelectVulkanPresentBackendForPolicy(Policy::JustInTime, mixed)
                == TimingBackend::GoogleDisplayTiming,
        "JIT mixed capability must select GOOGLE before refresh bootstrap is ready");
    Require(VulkanShouldPollGoogleForFrame(
                Policy::JustInTime, false, false, true, mixed),
        "JIT mixed capability must request GOOGLE refresh/past-timing bootstrap");

    Require(SelectVulkanPresentBackendForPolicy(Policy::TelemetryOnly, mixed)
                == TimingBackend::ExtPresentTiming
            && !VulkanShouldPollGoogleForFrame(
                Policy::TelemetryOnly, false, false, true, mixed),
        "TelemetryOnly must retain EXT telemetry priority and skip GOOGLE polling");

    mixed.TargetSchedulingLifecycleFailed = true;
    Require(VulkanShouldPollGoogleForFrame(
                Policy::JustInTime, false, false, true, mixed),
        "EXT lifecycle failure must request GOOGLE bootstrap in the same JIT frame");
    Require(!VulkanShouldPollGoogleForFrame(
                 Policy::JustInTime, true, false, true, mixed)
            && !VulkanShouldPollGoogleForFrame(
                 Policy::JustInTime, false, true, true, mixed)
            && !VulkanShouldPollGoogleForFrame(
                 Policy::JustInTime, false, false, false, mixed),
        "vendor-owned pacing and abnormal speed must suppress GOOGLE polling");
}

void TestGoogleTimingTransactions()
{
    Require(NextGooglePresentId(0) == 1 && NextGooglePresentId(41) == 42,
        "GOOGLE present IDs must increment independently");
    Require(NextGooglePresentId(std::numeric_limits<u32>::max()) == 1,
        "GOOGLE present ID wrap must skip reserved ID zero");

    VulkanGoogleDisplayTimingModel model;
    const VulkanGooglePresentRequest first = model.Prepare(
        1'000'000'000, Interval60Fps, true);
    Require(first.PresentId == 1
            && first.DesiredPresentTimeNs == 1'016'666'667,
        "the first GOOGLE target must bootstrap one frame into the future");
    model.Commit();

    const VulkanGooglePresentRequest second = model.Prepare(
        1'005'000'000, Interval60Fps, true);
    Require(second.PresentId == 2
            && second.DesiredPresentTimeNs >= first.DesiredPresentTimeNs,
        "GOOGLE desired times must never move backwards");
    model.Abandon();
    const VulkanGooglePresentRequest retry = model.Prepare(
        1'005'000'000, Interval60Fps, true);
    Require(retry.PresentId == second.PresentId
            && retry.DesiredPresentTimeNs == second.DesiredPresentTimeNs,
        "a rejected present must roll back GOOGLE ID and target state");
    model.Commit();

    const VulkanGooglePresentRequest telemetry = model.Prepare(
        1'010'000'000, Interval60Fps, false);
    Require(telemetry.PresentId == 3 && telemetry.DesiredPresentTimeNs == 0,
        "telemetry metadata must carry a unique ID and desired time zero");
    model.Commit();

    VulkanGooglePresentationFeedback feedback;
    feedback.PresentId = 2;
    feedback.DesiredPresentTimeNs = retry.DesiredPresentTimeNs;
    feedback.ActualPresentTimeNs = retry.DesiredPresentTimeNs + 100;
    feedback.EarliestPresentTimeNs = retry.DesiredPresentTimeNs - 50;
    feedback.PresentMarginNs = 150;
    model.RecordFeedback(feedback);
    Require(model.GetFeedback().PresentMarginNs == 150,
        "GOOGLE feedback must preserve present margin without EXT field synthesis");

    model.Reset();
    Require(model.GetCommittedPresentId() == 0
            && model.GetCommittedDesiredPresentTimeNs() == 0
            && model.GetFeedback().PresentId == 0,
        "swapchain recreation must reset GOOGLE IDs, cadence and feedback");

    Require(VulkanGoogleActionFor(VulkanGoogleQueryStatus::Success)
            == VulkanGoogleQueryAction::Continue
            && VulkanGoogleActionFor(VulkanGoogleQueryStatus::Incomplete)
                == VulkanGoogleQueryAction::Continue,
        "empty feedback and VK_INCOMPLETE must remain normal polling states");
    Require(VulkanGoogleActionFor(VulkanGoogleQueryStatus::OutOfDate)
            == VulkanGoogleQueryAction::RebuildSwapchain,
        "GOOGLE out-of-date must request swapchain recreation");
    Require(VulkanGoogleActionFor(VulkanGoogleQueryStatus::DeviceLost)
            == VulkanGoogleQueryAction::FailDevice,
        "GOOGLE device loss must remain fatal");
    Require(VulkanGoogleActionFor(VulkanGoogleQueryStatus::SurfaceLost)
            == VulkanGoogleQueryAction::FailSurface,
        "GOOGLE surface loss must leave the swapchain-only rebuild loop");
    Require(VulkanGoogleActionFor(VulkanGoogleQueryStatus::Failure)
            == VulkanGoogleQueryAction::DisableBackend,
        "other GOOGLE query failures must disable only the optional backend");
}


// THE regression this matrix exists for. VK_EXT_present_timing depends on
// VK_KHR_swapchain, VK_KHR_present_id2, VK_KHR_get_surface_capabilities2 and
// VK_KHR_calibrated_timestamps -- not on VK_KHR_present_wait2. A driver that
// exposes present timing but not present wait2 must still schedule target
// times; it simply skips the optional bounded wait.
void TestTargetTimeDoesNotRequirePresentWait2()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.PresentWait2Surface = false;
    caps.PresentWaitRuntimeEnabled = false;

    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(decision.TargetTimeScheduling,
        "target-time scheduling must not require VK_KHR_present_wait2");
    Require(!decision.BoundedPresentWait,
        "a surface without present_wait2 must not attempt the bounded wait");
    Require(decision.Authority == Authority::GenericPresentTiming,
        "target-time scheduling alone must still claim the generic authority");
    Require(decision.Reason == Reason::None,
        "a fully capable target-time path must report no fallback reason");
    Require(decision.OptionalWaitUnavailable,
        "the missing bounded wait must be reported as a diagnostic, not a blocker");

    // The same is true for the FIFO_LATEST_READY variant, whose whole point is
    // time-based image selection.
    const VulkanPacingDecision latestReady =
        Resolve(Policy::JustInTimeFifoLatestReady, caps);
    Require(latestReady.TargetTimeScheduling && !latestReady.BoundedPresentWait,
        "FIFO_LATEST_READY JIT must not require present_wait2 either");

    // A runtime failure that retired the wait must not retire scheduling.
    VulkanPacingCapabilities retired = FullCapabilities();
    retired.PresentWaitRuntimeEnabled = false;
    const VulkanPacingDecision afterFailure = Resolve(Policy::JustInTime, retired);
    Require(afterFailure.TargetTimeScheduling && !afterFailure.BoundedPresentWait,
        "a failed present wait must not take target-time scheduling down with it");
}


// The converse: present_wait2 without present timing is still a valid, useful
// configuration -- it is exactly what the PresentWait policy asks for.
void TestBoundedWaitWithoutPresentTiming()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.PresentTimingSurface = false;
    caps.TimingMetadataEnabled = false;

    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(decision.BoundedPresentWait && !decision.TargetTimeScheduling,
        "a wait-capable surface without present timing must still take the wait");
    Require(decision.Authority == Authority::GenericPresentTiming,
        "the bounded wait alone must still claim the generic authority");
    Require(decision.Reason == Reason::PresentTimingUnsupported,
        "the absent capability must be named");

    const VulkanPacingDecision waitPolicy = Resolve(Policy::PresentWait, FullCapabilities());
    Require(waitPolicy.BoundedPresentWait && !waitPolicy.TargetTimeScheduling,
        "the PresentWait policy must wait and never request a target time");
    Require(waitPolicy.Reason == Reason::PresentWaitPolicyNoTarget,
        "PresentWait must report its own reason rather than the telemetry-only one");

    const VulkanPacingDecision strict =
        Resolve(Policy::PresenterOneFrameBudget, FullCapabilities());
    Require(VulkanPolicyUsesPresenterOneFrameBudget(Policy::PresenterOneFrameBudget)
            && strict.BoundedPresentWait && !strict.TargetTimeScheduling
            && strict.Authority == Authority::GenericPresentTiming,
        "the strict presenter policy must own only the bounded previous-present wait");
    Require(strict.Reason == Reason::PresentWaitPolicyNoTarget
            && !strict.OptionalWaitUnavailable,
        "the strict presenter policy must report no target-time scheduling and a usable wait");

    VulkanPacingCapabilities noWait = FullCapabilities();
    noWait.PresentWait2Surface = false;
    noWait.PresentWaitRuntimeEnabled = false;
    const VulkanPacingDecision strictNoWait =
        Resolve(Policy::PresenterOneFrameBudget, noWait);
    Require(!strictNoWait.BoundedPresentWait
            && strictNoWait.Authority == Authority::GenericHost
            && strictNoWait.OptionalWaitUnavailable,
        "strict presenter pacing must degrade to host ownership when present_wait2 is absent");
}


// Vendor latency APIs own frame pacing end to end. Layering a second scheduler
// or a second wait on top would fight their driver-side model.
void TestVendorLatencyApisWin()
{
    const VulkanPacingCapabilities caps = FullCapabilities();

    const VulkanPacingDecision reflex = ResolveVulkanPresentPacing(
        Policy::JustInTimeFifoLatestReady, true, false, true, caps);
    Require(reflex.Authority == Authority::NvidiaReflex
            && !reflex.BoundedPresentWait && !reflex.TargetTimeScheduling,
        "active Reflex must own pacing with no generic wait or target time");
    Require(reflex.Reason == Reason::VendorLatencyApiOwnsPacing,
        "Reflex must be named as the reason generic scheduling is off");

    const VulkanPacingDecision antiLag = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, true, true, caps);
    Require(antiLag.Authority == Authority::AmdAntiLag2
            && !antiLag.BoundedPresentWait && !antiLag.TargetTimeScheduling,
        "active Anti-Lag 2 must own pacing with no generic wait or target time");

    // Reflex outranks Anti-Lag when a driver somehow reports both.
    Require(ResolveVulkanPresentPacing(Policy::JustInTime, true, true, true, caps).Authority
            == Authority::NvidiaReflex,
        "Reflex must win the authority resolver over Anti-Lag 2");

    const VulkanPacingDecision strictReflex = ResolveVulkanPresentPacing(
        Policy::PresenterOneFrameBudget, true, false, true, caps);
    Require(strictReflex.Authority == Authority::NvidiaReflex
            && !strictReflex.BoundedPresentWait
            && !strictReflex.TargetTimeScheduling,
        "strict presenter pacing must not add a generic wait to active Reflex");
}


// Fast-forward, slow motion and unlimited FPS are not presentation problems.
// Neither mechanism may hold frames to a cadence the emulator is not running at.
void TestSpeedAndPolicyGates()
{
    const VulkanPacingCapabilities caps = FullCapabilities();

    const VulkanPacingDecision fastForward = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, false, false, caps);
    Require(fastForward.Authority == Authority::GenericHost
            && !fastForward.BoundedPresentWait && !fastForward.TargetTimeScheduling,
        "abnormal speed must disable both generic mechanisms");
    Require(fastForward.Reason == Reason::NotNormalSpeed, "the speed must be named");

    const VulkanPacingDecision strictFastForward = ResolveVulkanPresentPacing(
        Policy::PresenterOneFrameBudget, false, false, false, caps);
    Require(strictFastForward.Authority == Authority::GenericHost
            && !strictFastForward.BoundedPresentWait
            && !strictFastForward.TargetTimeScheduling,
        "strict presenter pacing must not hold fast-forward or slow motion");

    const VulkanPacingDecision telemetry = Resolve(Policy::TelemetryOnly, caps);
    Require(telemetry.Authority == Authority::GenericHost
            && !telemetry.BoundedPresentWait && !telemetry.TargetTimeScheduling,
        "the default telemetry-only policy must never wait or schedule");
    Require(telemetry.Reason == Reason::TelemetryOnlyPolicy,
        "telemetry-only must report its own reason");

    // A zero frame interval is how normal-speed-but-unlimited reaches here.
    VulkanPacingCapabilities noInterval = FullCapabilities();
    noInterval.FrameIntervalKnown = false;
    const VulkanPacingDecision unlimited = Resolve(Policy::JustInTime, noInterval);
    Require(!unlimited.TargetTimeScheduling && unlimited.BoundedPresentWait,
        "without an emulator frame interval there is no target, but the wait remains");
    Require(unlimited.Reason == Reason::NoFrameInterval, "the missing interval must be named");
}


// Every prerequisite must both block scheduling and name itself, so a developer
// log says which one to go and look at.
void TestFallbackReasonsAreSpecific()
{
    struct Case
    {
        const char* Name;
        void (*Break)(VulkanPacingCapabilities&);
        Reason Expected;
    };

    const Case cases[] = {
        {"present id2", [](VulkanPacingCapabilities& c) { c.PresentId2Surface = false; },
         Reason::PresentId2Unsupported},
        {"swapchain", [](VulkanPacingCapabilities& c) { c.SwapchainValid = false; },
         Reason::PresentTimingUnsupported},
        {"every device timing mode",
         [](VulkanPacingCapabilities& c) {
             c.AbsoluteTimingDevice = false;
             c.RelativeTimingDevice = false;
         },
         Reason::NoTargetTimingModeDevice},
        {"every surface timing mode",
         [](VulkanPacingCapabilities& c) {
             c.AbsoluteTimingSurface = false;
             c.RelativeTimingSurface = false;
         },
         Reason::NoTargetTimingModeSurface},
        {"present mode", [](VulkanPacingCapabilities& c) { c.FifoPresentMode = false; },
         Reason::NonFifoPresentMode},
        {"timing properties",
         [](VulkanPacingCapabilities& c) { c.TimingPropertiesReady = false; },
         Reason::TimingPropertiesNotReady},
        {"time domains", [](VulkanPacingCapabilities& c) { c.TimeDomainsReady = false; },
         Reason::TimeDomainsNotReady},
        {"target stage", [](VulkanPacingCapabilities& c) { c.TargetStageValid = false; },
         Reason::NoValidTargetStage},
    };

    for (const Case& test : cases)
    {
        VulkanPacingCapabilities caps = FullCapabilities();
        test.Break(caps);
        const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
        Require(!decision.TargetTimeScheduling,
            std::string("a missing ") + test.Name + " must block target scheduling");
        Require(decision.Reason == test.Expected,
            std::string("a missing ") + test.Name + " must name itself as the reason");
    }

    // A surface that advertised present timing but stopped reporting failed at
    // runtime; one that never advertised it simply lacks the capability.
    VulkanPacingCapabilities runtimeFailure = FullCapabilities();
    runtimeFailure.TimingMetadataEnabled = false;
    Require(Resolve(Policy::JustInTime, runtimeFailure).Reason == Reason::TimingQueryFailed,
        "a supported surface that stopped reporting must be a runtime failure");

    VulkanPacingCapabilities queuePressure = FullCapabilities();
    queuePressure.TimingMetadataEnabled = false;
    queuePressure.TimingQueuePressure = true;
    Require(Resolve(Policy::JustInTime, queuePressure).Reason == Reason::TimingQueuePressure,
        "a full timing queue must be reported as pressure, not a query failure");

    VulkanPacingCapabilities absent = FullCapabilities();
    absent.PresentTimingSurface = false;
    absent.TimingMetadataEnabled = false;
    Require(Resolve(Policy::JustInTime, absent).Reason == Reason::PresentTimingUnsupported,
        "a surface without present timing must be unsupported, not failed");
}


// THE regression this mode selection exists for, and a direct reproduction of
// the RTX 5070 Ti / driver 610.74.0.0 surface that motivated it: the device
// advertises presentAtAbsoluteTime, but the surface does not support it. Before
// relative scheduling that combination produced no target at all, so the
// JustInTime policies were indistinguishable from PresentWait.
void TestRelativeFallbackWhenSurfaceLacksAbsolute()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.AbsoluteTimingSurface = false;

    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(decision.TargetMode == TargetMode::Relative,
        "a surface without absolute timing must fall back to relative scheduling");
    Require(decision.TargetTimeScheduling,
        "relative scheduling must count as target scheduling");
    Require(decision.Reason == Reason::None,
        "falling back to relative is a supported outcome, not a fallback reason");
    Require(decision.Authority == Authority::GenericPresentTiming,
        "relative scheduling must claim the generic authority");

    // The same must hold for the latest-ready variant, whose whole purpose is
    // time-based image selection.
    const VulkanPacingDecision latestReady =
        Resolve(Policy::JustInTimeFifoLatestReady, caps);
    Require(latestReady.TargetMode == TargetMode::Relative,
        "FIFO_LATEST_READY JIT must also reach relative scheduling");
}


// Absolute is a preference, not a tie-break: whenever it is available it wins,
// because its targets are expressed in the units the feedback baseline measures.
void TestAbsolutePreferredOverRelative()
{
    const VulkanPacingCapabilities caps = FullCapabilities();
    Require(SelectVulkanTargetSchedulingMode(caps) == TargetMode::Absolute,
        "absolute must win when both modes are available");

    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(decision.TargetMode == TargetMode::Absolute,
        "the resolver must prefer absolute scheduling");

    // Device-level absolute without surface support is not absolute.
    VulkanPacingCapabilities deviceOnly = FullCapabilities();
    deviceOnly.AbsoluteTimingSurface = false;
    Require(SelectVulkanTargetSchedulingMode(deviceOnly) == TargetMode::Relative,
        "absolute needs both device and surface support");

    // ...and the same asymmetry applies to relative.
    VulkanPacingCapabilities relativeSurfaceOnly = FullCapabilities();
    relativeSurfaceOnly.AbsoluteTimingSurface = false;
    relativeSurfaceOnly.RelativeTimingDevice = false;
    Require(SelectVulkanTargetSchedulingMode(relativeSurfaceOnly) == TargetMode::None,
        "relative needs both device and surface support");
}


// Neither mode available: no target, but the bounded wait is untouched.
void TestNoTimingModeKeepsBoundedWait()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.AbsoluteTimingSurface = false;
    caps.RelativeTimingSurface = false;

    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(decision.TargetMode == TargetMode::None && !decision.TargetTimeScheduling,
        "without either mode there is no target scheduling");
    Require(decision.BoundedPresentWait,
        "losing both timing modes must not cost the bounded present wait");
    Require(decision.Reason == Reason::NoTargetTimingModeSurface,
        "the surface must be named as the level that lacks the modes");
}


// Vendor latency APIs and abnormal speed suppress relative exactly as they
// suppress absolute -- a new mode must not open a new bypass.
void TestRelativeSuppressedByVendorAndSpeed()
{
    const VulkanPacingCapabilities caps = FullCapabilities();
    VulkanPacingCapabilities relativeOnly = FullCapabilities();
    relativeOnly.AbsoluteTimingSurface = false;

    const VulkanPacingDecision reflex = ResolveVulkanPresentPacing(
        Policy::JustInTime, true, false, true, relativeOnly);
    Require(reflex.Authority == Authority::NvidiaReflex
            && reflex.TargetMode == TargetMode::None,
        "active Reflex must suppress relative scheduling too");

    const VulkanPacingDecision antiLag = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, true, true, relativeOnly);
    Require(antiLag.Authority == Authority::AmdAntiLag2
            && antiLag.TargetMode == TargetMode::None,
        "active Anti-Lag 2 must suppress relative scheduling too");

    const VulkanPacingDecision fastForward = ResolveVulkanPresentPacing(
        Policy::JustInTime, false, false, false, relativeOnly);
    Require(fastForward.TargetMode == TargetMode::None,
        "abnormal speed must suppress relative scheduling");

    Require(Resolve(Policy::TelemetryOnly, caps).TargetMode == TargetMode::None,
        "telemetry-only must request no target in either mode");
    Require(Resolve(Policy::PresentWait, relativeOnly).TargetMode == TargetMode::None,
        "the present-wait policy must not silently enable relative scheduling");

    // Non-FIFO present modes have no defined target semantics for either mode.
    VulkanPacingCapabilities immediate = relativeOnly;
    immediate.FifoPresentMode = false;
    const VulkanPacingDecision vsyncOff = Resolve(Policy::JustInTime, immediate);
    Require(vsyncOff.TargetMode == TargetMode::None,
        "relative targets must not be sent outside the FIFO family");
    Require(vsyncOff.BoundedPresentWait,
        "the bounded wait does not depend on the present mode");
}


// ---------------------------------------------------------------------------
// Relative cadence quantizer
// ---------------------------------------------------------------------------

// Runs `frames` presents and returns the requested durations, committing each.
std::vector<VulkanRelativeCadence::Request> RunCadence(
    VulkanRelativeCadence& cadence, int frames)
{
    std::vector<VulkanRelativeCadence::Request> requests;
    for (int i = 0; i < frames; ++i)
    {
        requests.push_back(cadence.Prepare());
        cadence.Commit();
    }
    return requests;
}


// 60 FPS on 144 Hz is 2.4 refreshes per frame. Rounding to 2 would run 17% fast
// and rounding to 3 25% slow, so the remainder has to be spread instead. The
// invariant that matters is the accumulated error, not one specific pattern.
void TestRelativeCadence60On144()
{
    constexpr u64 refresh = 6'944'444;  // 144 Hz
    VulkanRelativeCadence cadence;
    cadence.Configure(refresh, refresh, Interval60Fps);

    constexpr int frames = 300;
    const auto requests = RunCadence(cadence, frames);

    u64 totalQuanta = 0;
    for (const auto& request : requests)
    {
        Require(request.DurationNs != 0,
            "a relative duration must never be zero -- zero means no target");
        Require(request.Quantized,
            "a finite refresh interval must produce a quantized duration");
        Require(request.DurationNs % refresh == 0,
            "durations must be whole refresh intervals on a fixed-refresh display");
        Require(request.Quanta == 2 || request.Quanta == 3,
            "60 FPS on 144 Hz must alternate between 2 and 3 refreshes");
        totalQuanta += request.Quanta;
    }

    // Long-run average must be 2.4 refreshes per frame, i.e. the total duration
    // must track the emulator's own interval to within one refresh.
    const u64 totalDuration = totalQuanta * refresh;
    const u64 expected = Interval60Fps * frames;
    const u64 error = totalDuration > expected
        ? totalDuration - expected
        : expected - totalDuration;
    Require(error < refresh,
        "accumulated cadence error must stay under one refresh quantum");

    // Both quanta must actually occur; a run of only 2s would be the rounding
    // bug this exists to prevent.
    bool sawTwo = false;
    bool sawThree = false;
    for (const auto& request : requests)
    {
        sawTwo = sawTwo || request.Quanta == 2;
        sawThree = sawThree || request.Quanta == 3;
    }
    Require(sawTwo && sawThree,
        "the fractional remainder must produce a mixed cadence, not a fixed round");
}


// Integer ratios must be exact, with no fractional drift at all.
void TestRelativeCadenceIntegerRatios()
{
    constexpr u64 refresh120 = 8'333'333;
    VulkanRelativeCadence onOneTwenty;
    onOneTwenty.Configure(refresh120, refresh120, Interval60Fps);
    for (const auto& request : RunCadence(onOneTwenty, 60))
    {
        Require(request.Quanta == 2,
            "60 FPS on 120 Hz must always take exactly 2 refresh cycles");
    }

    constexpr u64 refresh60 = 16'666'667;
    VulkanRelativeCadence onSixty;
    onSixty.Configure(refresh60, refresh60, Interval60Fps);
    for (const auto& request : RunCadence(onSixty, 60))
    {
        Require(request.Quanta == 1,
            "60 FPS on 60 Hz must always take exactly 1 refresh cycle");
    }
}


// A display slower than the emulator cannot show every frame. The scheduler
// must ask for the next cycle it can have rather than computing zero quanta,
// because zero is the extension's "no target requested" value.
void TestRelativeCadenceDisplaySlowerThanTarget()
{
    constexpr u64 refresh50Hz = 20'000'000;
    VulkanRelativeCadence cadence;
    cadence.Configure(refresh50Hz, refresh50Hz, Interval60Fps);

    for (const auto& request : RunCadence(cadence, 60))
    {
        Require(request.DurationNs >= refresh50Hz,
            "a slow display must still get at least one refresh interval");
        Require(request.DurationNs != 0, "the duration must never be zero");
        Require(request.Quanta == 1, "one refresh is the most a slow display can give");
    }
}


// Variable refresh has no grid to quantize onto, and unknown refresh must not
// have one invented.
void TestRelativeCadenceVariableAndUnknownRefresh()
{
    VulkanRelativeCadence vrrFast;
    vrrFast.Configure(VulkanRelativeCadence::VariableRefreshInterval, 4'000'000,
                      Interval60Fps);
    const VulkanRelativeCadence::Request fast = vrrFast.Prepare();
    Require(fast.DurationNs == Interval60Fps,
        "VRR faster than the emulator must use the emulator's own interval");
    Require(!fast.Quantized,
        "a VRR duration has no refresh cycle to snap to");

    VulkanRelativeCadence vrrSlow;
    vrrSlow.Configure(VulkanRelativeCadence::VariableRefreshInterval, 25'000'000,
                      Interval60Fps);
    Require(vrrSlow.Prepare().DurationNs == 25'000'000,
        "VRR must not ask for less than the display's minimum cycle");

    VulkanRelativeCadence unknown;
    unknown.Configure(0, 0, Interval60Fps);
    const VulkanRelativeCadence::Request raw = unknown.Prepare();
    Require(raw.DurationNs == Interval60Fps,
        "an unknown refresh interval must fall back to the raw frame interval");
    Require(!raw.Quantized,
        "an unquantized duration must not ask for the nearest refresh cycle");

    VulkanRelativeCadence noInterval;
    noInterval.Configure(6'944'444, 6'944'444, 0);
    Require(noInterval.Prepare().DurationNs == 0,
        "without an emulator frame interval there is no relative duration");
}


// Every request carries the inputs it was computed from, so a capture can prove
// `duration == quanta * refreshInterval` per present rather than inferring it
// from a log line whose refresh interval may already have moved on.
void TestRelativeCadenceReportsItsInputs()
{
    constexpr u64 refresh = 6'944'444;
    VulkanRelativeCadence cadence;
    cadence.Configure(refresh, refresh, Interval60Fps);

    u64 previousAfter = 0;
    for (int i = 0; i < 20; ++i)
    {
        const VulkanRelativeCadence::Request request = cadence.Prepare();
        Require(request.RefreshIntervalNs == refresh && request.RefreshDurationNs == refresh,
            "a request must report the refresh values it was generated against");
        Require(request.DurationNs == request.Quanta * request.RefreshIntervalNs,
            "duration must be re-derivable from quanta and the refresh interval");
        Require(request.AccumulatorBeforeNs == previousAfter,
            "the accumulator must carry from one committed frame to the next");
        Require(request.AccumulatorAfterNs < refresh,
            "the carried fraction must always stay below one refresh interval");
        cadence.Commit();
        previousAfter = request.AccumulatorAfterNs;
        Require(cadence.GetAccumulatorNs() == request.AccumulatorAfterNs,
            "the committed accumulator must match what the request reported");
    }
}


// A malformed refresh interval must degrade to the unquantized path rather than
// overflow the accumulator sum. No real display reports one; a driver might.
void TestRelativeCadenceRejectsAbsurdRefreshInterval()
{
    constexpr u64 absurd = (std::numeric_limits<u64>::max)() - 4;
    VulkanRelativeCadence cadence;
    cadence.Configure(absurd, absurd, Interval60Fps);

    const VulkanRelativeCadence::Request request = cadence.Prepare();
    Require(request.DurationNs == Interval60Fps,
        "an unusable refresh interval must fall back to the raw frame interval");
    Require(!request.Quantized,
        "an unusable refresh interval must not claim a quantized duration");
    Require(request.AccumulatorAfterNs == 0,
        "the unquantized path must not accumulate a fraction");

    // Variable refresh uses the same sentinel magnitude but is a defined value
    // and must keep its own behaviour rather than being caught by the guard.
    VulkanRelativeCadence vrr;
    vrr.Configure(VulkanRelativeCadence::VariableRefreshInterval, 5'000'000, Interval60Fps);
    Require(vrr.Prepare().DurationNs == Interval60Fps,
        "the overflow guard must not swallow the variable-refresh path");
}


// The transactional contract, matching presentation sequence numbering: a
// rejected present releases its cadence step, and a queue-full retry that is
// finally accepted commits exactly once.
void TestRelativeCadenceTransactions()
{
    constexpr u64 refresh = 6'944'444;
    VulkanRelativeCadence cadence;
    cadence.Configure(refresh, refresh, Interval60Fps);

    // Drive to the point where the next frame would take the extra refresh.
    RunCadence(cadence, 2);
    const u64 accumulatorBefore = cadence.GetAccumulatorNs();

    const VulkanRelativeCadence::Request rejected = cadence.Prepare();
    cadence.Abandon();
    Require(cadence.GetAccumulatorNs() == accumulatorBefore,
        "a rejected present must not consume a cadence step");

    const VulkanRelativeCadence::Request retried = cadence.Prepare();
    Require(retried.Quanta == rejected.Quanta && retried.DurationNs == rejected.DurationNs,
        "the next attempt must recompute the same cadence step");

    // A queue-full retry re-prepares the same frame before it is accepted.
    const VulkanRelativeCadence::Request retryWithoutTiming = cadence.Prepare();
    Require(retryWithoutTiming.Quanta == retried.Quanta,
        "re-preparing the same frame must not advance the cadence");
    cadence.Commit();
    const u64 afterCommit = cadence.GetAccumulatorNs();
    cadence.Commit();
    Require(cadence.GetAccumulatorNs() == afterCommit,
        "a second commit for the same present must be a no-op");
}


// A phase accumulated against one refresh grid or one emulator rate describes
// nothing on another. Re-configuring must restart it.
void TestRelativeCadenceResets()
{
    constexpr u64 refresh144 = 6'944'444;
    VulkanRelativeCadence cadence;
    cadence.Configure(refresh144, refresh144, Interval60Fps);
    RunCadence(cadence, 3);
    Require(cadence.GetAccumulatorNs() != 0, "a fraction must have accumulated");

    // Refresh-rate change, as a timingPropertiesCounter bump would deliver.
    constexpr u64 refresh120 = 8'333'333;
    cadence.Configure(refresh120, refresh120, Interval60Fps);
    Require(cadence.GetAccumulatorNs() == 0,
        "a refresh interval change must reset the fractional accumulator");

    RunCadence(cadence, 3);
    // TargetFPS change.
    cadence.Configure(refresh120, refresh120, Interval60Fps / 2);
    Require(cadence.GetAccumulatorNs() == 0,
        "a frame interval change must reset the fractional accumulator");

    RunCadence(cadence, 3);
    cadence.Reset();
    Require(cadence.GetAccumulatorNs() == 0 && cadence.GetPendingQuanta() == 0,
        "an explicit reset must clear the cadence entirely");

    // Re-configuring with identical values is not a change and must not reset.
    cadence.Configure(refresh120, refresh120, Interval60Fps / 2);
    RunCadence(cadence, 3);
    const u64 accumulated = cadence.GetAccumulatorNs();
    cadence.Configure(refresh120, refresh120, Interval60Fps / 2);
    Require(cadence.GetAccumulatorNs() == accumulated,
        "re-configuring with the same values must preserve the phase");
}


// An A/B row must describe what the accepted present actually carried, not what
// the resolver allowed. The resolver can permit target scheduling for a frame
// that ends up requesting nothing -- and counting those as hits would inflate
// the "target scheduling active" ratio the Phase 3 acceptance criterion reads.
void TestAppliedTargetReflectsThePresent()
{
    // Steady state: metadata carries a real relative target.
    const VulkanAppliedTarget relative =
        ResolveVulkanAppliedTarget(true, TargetMode::Relative, 14'885'600);
    Require(relative.Applied && relative.Mode == TargetMode::Relative
            && relative.ValueNs == 14'885'600,
        "a present carrying a relative target must be recorded as applied");

    const VulkanAppliedTarget absolute =
        ResolveVulkanAppliedTarget(true, TargetMode::Absolute, 1'016'666'667);
    Require(absolute.Applied && absolute.Mode == TargetMode::Absolute,
        "a present carrying an absolute target must be recorded as applied");

    // Bootstrap: timing metadata is attached for telemetry, but no target was
    // requested -- absolute has no baseline yet, relative has no first present.
    const VulkanAppliedTarget bootstrap =
        ResolveVulkanAppliedTarget(true, TargetMode::None, 0);
    Require(!bootstrap.Applied && bootstrap.Mode == TargetMode::None
            && bootstrap.ValueNs == 0,
        "a bootstrap present requests no target and must not count as applied");

    // Queue-full retry: the present was re-issued with its timing metadata
    // stripped. The frame was displayed, but with no target at all.
    const VulkanAppliedTarget retried =
        ResolveVulkanAppliedTarget(false, TargetMode::None, 0);
    Require(!retried.Applied && retried.Mode == TargetMode::None
            && retried.ValueNs == 0,
        "a present retried without timing metadata must not count as applied");

    // Defensive: an inconsistent pair must resolve to "not applied" rather than
    // reporting a mode with no value or a value with no metadata.
    Require(!ResolveVulkanAppliedTarget(false, TargetMode::Relative, 14'885'600).Applied,
        "a target value without attached metadata was never sent");
    Require(!ResolveVulkanAppliedTarget(true, TargetMode::Relative, 0).Applied,
        "a mode without a value is not an applied target");
    const VulkanAppliedTarget contradiction =
        ResolveVulkanAppliedTarget(false, TargetMode::Relative, 14'885'600);
    Require(contradiction.Mode == TargetMode::None && contradiction.ValueNs == 0,
        "a rejected pair must be cleared, not passed through");
}


// A lost device and a stale swapchain are different failure classes. They once
// shared a single `true` return, which routed device loss into the swapchain
// rebuild loop -- where it would fail again on every following frame.
void TestBeginResultRouting()
{
    const VulkanPacerBeginAction cont =
        VulkanPacerActionFor(VulkanPacerBeginResult::Continue);
    Require(!cont.RebuildSwapchain && !cont.FailRenderer,
        "a normal frame must neither rebuild the swapchain nor fail the renderer");

    const VulkanPacerBeginAction outOfDate =
        VulkanPacerActionFor(VulkanPacerBeginResult::SwapchainOutOfDate);
    Require(outOfDate.RebuildSwapchain && !outOfDate.FailRenderer,
        "an out-of-date swapchain must be rebuilt, not treated as a failure");

    const VulkanPacerBeginAction suboptimal =
        VulkanPacerActionFor(VulkanPacerBeginResult::SwapchainSuboptimal);
    Require(suboptimal.RebuildSwapchain && !suboptimal.FailRenderer,
        "a suboptimal swapchain must be rebuilt through the dirty path");

    const VulkanPacerBeginAction deviceLost =
        VulkanPacerActionFor(VulkanPacerBeginResult::DeviceLost);
    Require(deviceLost.FailRenderer,
        "device loss must reach the renderer's runtime-failure path");
    Require(!deviceLost.RebuildSwapchain,
        "device loss must never be answered with a swapchain rebuild");

    const VulkanPacerBeginAction surfaceLost =
        VulkanPacerActionFor(VulkanPacerBeginResult::SurfaceLost);
    Require(surfaceLost.FailRenderer && !surfaceLost.RebuildSwapchain,
        "surface loss must leave the swapchain-only rebuild loop");
}

void TestSwapchainRecreationInvalidatesFrameDecision()
{
    Require(VulkanFrameDecisionMatchesSwapchain(10, 10),
        "a decision from the current swapchain generation is usable");
    Require(!VulkanFrameDecisionMatchesSwapchain(10, 11),
        "an old-generation decision must not control a recreated swapchain");
    Require(!VulkanFrameDecisionMatchesSwapchain(0, 11),
        "an unstamped decision must not authorize any swapchain");
    Require(!VulkanFrameDecisionMatchesSwapchain(11, 0),
        "a live decision must not authorize an absent swapchain generation");
}

void TestSwapchainRecreationFallbackReason()
{
    const Policy policies[] = {
        Policy::JustInTime,
        Policy::JustInTimeFifoLatestReady,
        Policy::PresentWait,
    };
    for (const Policy policy : policies)
    {
        const VulkanPacingDecision decision = Resolve(policy, FullCapabilities());
        const bool decisionCurrent = VulkanFrameDecisionMatchesSwapchain(41, 42);
        const Reason captured = VulkanFrameDecisionFallbackReason(
            decisionCurrent, decision.Reason);
        Require(captured == Reason::FrameDecisionInvalidatedBySwapchainRecreation,
            "same-frame swapchain recreation must report an explicit lifecycle reason");
        Require(captured != Reason::TelemetryOnlyPolicy,
            "generation invalidation must not masquerade as TelemetryOnlyPolicy");
    }

    Require(VulkanFrameDecisionFallbackReason(
                VulkanFrameDecisionMatchesSwapchain(42, 42),
                Reason::PresentWaitPolicyNoTarget)
                == Reason::PresentWaitPolicyNoTarget,
        "a current decision must preserve its policy-specific fallback reason");
}

// Query fault injection uses the production VkResult classifier. The mapping
// is pure, so these lifecycle classes can be tested without a Vulkan device or
// a window while the source audit verifies each API keeps its own success set.
void TestPresentTimingLifecycleResultClassification()
{
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_SUCCESS)
                == VulkanPacerBeginResult::Continue,
        "successful timing query must continue");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_INCOMPLETE)
                == VulkanPacerBeginResult::Continue,
        "incomplete timing query must continue with partial results");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_NOT_READY)
                == VulkanPacerBeginResult::Continue,
        "VK_NOT_READY must not become a fatal lifecycle result");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_ERROR_OUT_OF_DATE_KHR)
                == VulkanPacerBeginResult::SwapchainOutOfDate,
        "out-of-date timing result must request swapchain rebuild");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_ERROR_DEVICE_LOST)
                == VulkanPacerBeginResult::DeviceLost,
        "device-lost timing result must fail the renderer");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_ERROR_SURFACE_LOST_KHR)
                == VulkanPacerBeginResult::SurfaceLost,
        "surface-lost timing result must fail the surface/runtime");
    Require(VulkanPresentPacer::ClassifyPresentLifecycleResult(VK_ERROR_UNKNOWN)
                == VulkanPacerBeginResult::Continue,
        "unknown optional timing failure must remain a timing-only downgrade");
    Require(VulkanLatchBeginResult(
                VulkanPacerBeginResult::Continue, VulkanPacerBeginResult::DeviceLost)
                == VulkanPacerBeginResult::DeviceLost,
        "a Google eager DeviceLost must be held for the next routing point");
    Require(VulkanLatchBeginResult(
                VulkanPacerBeginResult::Continue, VulkanPacerBeginResult::SurfaceLost)
                == VulkanPacerBeginResult::SurfaceLost,
        "a Google eager SurfaceLost must be held for the next routing point");
    Require(VulkanLatchBeginResult(
                VulkanPacerBeginResult::DeviceLost, VulkanPacerBeginResult::SurfaceLost)
                == VulkanPacerBeginResult::DeviceLost,
        "the first fatal lifecycle class must not be overwritten");
}

// These API-specific contracts complement the shared lifecycle classifier:
// each Vulkan entry point keeps its own success and pending result set while
// still converging on the same presenter-facing fatal classes.
void TestPresentTimingQueryContractClassification()
{
    Require(ClassifyVulkanPastTimingResult(VK_SUCCESS)
                == VulkanPresentTimingQueryAction::Continue
                && ClassifyVulkanPastTimingResult(VK_INCOMPLETE)
                    == VulkanPresentTimingQueryAction::Continue,
        "EXT past-timing success and partial results must continue");
    Require(ClassifyVulkanPastTimingResult(VK_ERROR_OUT_OF_DATE_KHR)
                == VulkanPresentTimingQueryAction::SwapchainOutOfDate
                && ClassifyVulkanPastTimingResult(VK_ERROR_DEVICE_LOST)
                    == VulkanPresentTimingQueryAction::DeviceLost
                && ClassifyVulkanPastTimingResult(VK_ERROR_SURFACE_LOST_KHR)
                    == VulkanPresentTimingQueryAction::SurfaceLost,
        "EXT past-timing lifecycle failures must stay typed");
    Require(ClassifyVulkanPastTimingResult(VK_ERROR_UNKNOWN)
                == VulkanPresentTimingQueryAction::DisableOptional,
        "EXT past-timing unknown failures must only disable optional timing");

    Require(ClassifyVulkanTimingPropertiesResult(VK_SUCCESS)
                == VulkanPresentTimingQueryAction::Continue
                && ClassifyVulkanTimingPropertiesResult(VK_NOT_READY)
                    == VulkanPresentTimingQueryAction::RetryAfterPresent,
        "timing properties VK_NOT_READY must remain a post-present retry");
    Require(ClassifyVulkanTimingPropertiesResult(VK_ERROR_SURFACE_LOST_KHR)
                == VulkanPresentTimingQueryAction::SurfaceLost
                && ClassifyVulkanTimingPropertiesResult(VK_ERROR_UNKNOWN)
                    == VulkanPresentTimingQueryAction::DisableTargetLifecycle,
        "timing properties must separate surface loss from optional failure");

    Require(ClassifyVulkanTimeDomainResult(VK_SUCCESS)
                == VulkanPresentTimingQueryAction::Continue
                && ClassifyVulkanTimeDomainResult(VK_INCOMPLETE)
                    == VulkanPresentTimingQueryAction::RetryEnumeration,
        "time-domain success and incomplete results must preserve enumeration state");
    Require(ClassifyVulkanTimeDomainResult(VK_ERROR_SURFACE_LOST_KHR)
                == VulkanPresentTimingQueryAction::SurfaceLost
                && ClassifyVulkanTimeDomainResult(VK_NOT_READY)
                    == VulkanPresentTimingQueryAction::DisableTargetLifecycle,
        "time-domain VK_NOT_READY must not become spec-expected pending");

    Require(ClassifyVulkanGooglePastTimingResult(VK_SUCCESS)
                == VulkanPresentTimingQueryAction::Continue
                && ClassifyVulkanGooglePastTimingResult(VK_INCOMPLETE)
                    == VulkanPresentTimingQueryAction::Continue,
        "GOOGLE feedback success and partial results must continue");
    Require(ClassifyVulkanGooglePastTimingResult(VK_ERROR_OUT_OF_DATE_KHR)
                == VulkanPresentTimingQueryAction::SwapchainOutOfDate
                && ClassifyVulkanGooglePastTimingResult(VK_ERROR_DEVICE_LOST)
                    == VulkanPresentTimingQueryAction::DeviceLost
                && ClassifyVulkanGooglePastTimingResult(VK_ERROR_SURFACE_LOST_KHR)
                    == VulkanPresentTimingQueryAction::SurfaceLost,
        "GOOGLE feedback lifecycle failures must stay typed");
    Require(ClassifyVulkanGooglePastTimingResult(VK_ERROR_UNKNOWN)
                == VulkanPresentTimingQueryAction::DisableOptional,
        "GOOGLE unknown failures must only disable optional timing");
    Require(ClassifyVulkanGoogleRefreshCycleResult(VK_SUCCESS)
                == VulkanPresentTimingQueryAction::Continue
                && ClassifyVulkanGoogleRefreshCycleResult(VK_INCOMPLETE)
                    == VulkanPresentTimingQueryAction::DisableOptional
                && ClassifyVulkanGoogleRefreshCycleResult(VK_ERROR_OUT_OF_DATE_KHR)
                    == VulkanPresentTimingQueryAction::DisableOptional,
        "GOOGLE refresh-cycle query must keep its narrower return contract");
    Require(ClassifyVulkanGoogleRefreshCycleResult(VK_ERROR_DEVICE_LOST)
                == VulkanPresentTimingQueryAction::DeviceLost
                && ClassifyVulkanGoogleRefreshCycleResult(VK_ERROR_SURFACE_LOST_KHR)
                    == VulkanPresentTimingQueryAction::SurfaceLost,
        "GOOGLE refresh-cycle lifecycle failures must stay typed");
}

// vkWaitForPresent2KHR has its own success/status contract. Keep this test
// separate from the generic fatal classifier so VK_SUBOPTIMAL_KHR cannot be
// accidentally treated as an optional wait failure in a future refactor.
void TestPresentWait2ResultClassification()
{
    Require(ClassifyVulkanPresentWait2Result(VK_SUCCESS)
                == VulkanPresentWait2ResultAction::Continue,
        "present wait success must continue");
    Require(ClassifyVulkanPresentWait2Result(VK_TIMEOUT)
                == VulkanPresentWait2ResultAction::Timeout,
        "present wait timeout must remain non-fatal");
    Require(ClassifyVulkanPresentWait2Result(VK_SUBOPTIMAL_KHR)
                == VulkanPresentWait2ResultAction::SwapchainSuboptimal,
        "present wait suboptimal must request swapchain recreation");
    Require(ClassifyVulkanPresentWait2Result(VK_ERROR_OUT_OF_DATE_KHR)
                == VulkanPresentWait2ResultAction::SwapchainOutOfDate,
        "present wait out-of-date must request swapchain recreation");
    Require(ClassifyVulkanPresentWait2Result(VK_ERROR_DEVICE_LOST)
                == VulkanPresentWait2ResultAction::DeviceLost,
        "present wait device loss must fail the renderer");
    Require(ClassifyVulkanPresentWait2Result(VK_ERROR_SURFACE_LOST_KHR)
                == VulkanPresentWait2ResultAction::SurfaceLost,
        "present wait surface loss must fail the renderer");
    Require(ClassifyVulkanPresentWait2Result(VK_ERROR_UNKNOWN)
                == VulkanPresentWait2ResultAction::DisableWait,
        "unknown present wait failure must only disable the optional wait");
    Require(ClassifyVulkanPresentWait2Result(
                VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
                == VulkanPresentWait2ResultAction::DisableWait,
        "fullscreen-exclusive loss remains an optional wait downgrade until that extension is used");
}

void TestPresenterFrameBudgetContract()
{
    // F1 is submitted to slot 0 and F2 to slot 1. The next reusable slot is
    // slot 0, but the strict fallback must target the latest submitted F2
    // slot 1. This is the regression that the old NextFrameSlot logic missed.
    u64 absoluteFrame = 1;
    const u32 firstSlot = VulkanFrameRingIndexForAbsoluteFrame(absoluteFrame++, 2);
    const u32 secondSlot = VulkanFrameRingIndexForAbsoluteFrame(absoluteFrame++, 2);
    const u32 nextReusableSlot = VulkanFrameRingIndexForAbsoluteFrame(absoluteFrame, 2);
    Require(firstSlot == 0 && secondSlot == 1 && nextReusableSlot == 0,
        "two-slot frame-ring index model must distinguish F2 from the next reusable F1 slot");
    Require(secondSlot != nextReusableSlot,
        "latest submitted presenter wait target must not be the next reusable slot");

    Require(VulkanPresenterOneFrameBudgetTimeoutNs(16'666'667) <= 16'666'667,
        "strict presenter timeout must fit within one 60 Hz frame budget");
    Require(VulkanPresenterOneFrameBudgetTimeoutNs(1'000) == 250'000,
        "strict presenter timeout must retain the minimum driver-safe bound");
    Require(VulkanPresenterOneFrameBudgetTimeoutNs(0) == 16'000'000,
        "unknown frame interval must use the bounded strict timeout cap");
    Require(VulkanPresenterOneFrameBudgetTimeoutNs(100'000'000) == 16'000'000,
        "strict presenter timeout must clamp long intervals instead of retaining one second");

    VulkanPacingCapabilities legacy = FullCapabilities();
    legacy.PresentId2Surface = false;
    legacy.PresentWait2Surface = false;
    legacy.PresentWaitRuntimeEnabled = false;
    legacy.PresentWaitLegacySurface = true;
    legacy.PresentWaitLegacyRuntimeEnabled = true;
    const VulkanPacingDecision legacyDecision = Resolve(
        Policy::PresenterOneFrameBudget, legacy);
    Require(legacyDecision.BoundedPresentWait
                && legacyDecision.Authority == Authority::GenericPresentTiming,
        "legacy present-wait must own strict presenter pacing when wait2 is absent");
    legacy.PresentWaitLegacyRuntimeEnabled = false;
    Require(!Resolve(Policy::PresenterOneFrameBudget, legacy).BoundedPresentWait,
        "retired legacy present-wait must fall back to the host/fence ladder");
}


// VSync off means IMMEDIATE or MAILBOX, where "present at this time" has no
// defined meaning -- but telemetry and the bounded wait are still fine.
void TestNonFifoKeepsWaitButNotTarget()
{
    VulkanPacingCapabilities caps = FullCapabilities();
    caps.FifoPresentMode = false;
    const VulkanPacingDecision decision = Resolve(Policy::JustInTime, caps);
    Require(!decision.TargetTimeScheduling,
        "target presentation time must stay off outside the FIFO family");
    Require(decision.BoundedPresentWait,
        "the bounded wait does not depend on the present mode");
    Require(decision.Authority == Authority::GenericPresentTiming,
        "the surviving wait must keep the generic authority");
}

} // namespace

int main()
{
    TestSteadyStateTargets();
    TestLogicalIdGapIsNotASequenceGap();
    TestRetryReusesSequence();
    TestRejectedPresentReleasesSequence();
    TestBaselineRebase();
    TestDomainMismatchAndChange();
    TestResetOnSwapchainRecreation();
    TestGuards();
    TestHistoryWraparound();
    TestBeginResultRouting();
    TestSwapchainRecreationInvalidatesFrameDecision();
    TestSwapchainRecreationFallbackReason();
    TestPresentTimingLifecycleResultClassification();
    TestPresentTimingQueryContractClassification();
    TestPresentWait2ResultClassification();
    TestPresenterFrameBudgetContract();
    TestGoogleTimingTransactions();

    TestTimingBackendSelection();
    TestFifoLatestReadyBackendCompatibility();
    TestPolicyAwareGooglePolling();
    TestTargetTimeDoesNotRequirePresentWait2();
    TestBoundedWaitWithoutPresentTiming();
    TestVendorLatencyApisWin();
    TestSpeedAndPolicyGates();
    TestFallbackReasonsAreSpecific();
    TestNonFifoKeepsWaitButNotTarget();

    TestRelativeFallbackWhenSurfaceLacksAbsolute();
    TestAbsolutePreferredOverRelative();
    TestNoTimingModeKeepsBoundedWait();
    TestRelativeSuppressedByVendorAndSpeed();
    TestRelativeCadence60On144();
    TestRelativeCadenceIntegerRatios();
    TestRelativeCadenceDisplaySlowerThanTarget();
    TestRelativeCadenceVariableAndUnknownRefresh();
    TestRelativeCadenceReportsItsInputs();
    TestRelativeCadenceRejectsAbsurdRefreshInterval();
    TestRelativeCadenceTransactions();
    TestRelativeCadenceResets();
    TestAppliedTargetReflectsThePresent();

    if (Failures != 0)
    {
        std::fprintf(stderr, "Vulkan present timing model tests FAILED (%d)\n", Failures);
        return 1;
    }
    std::puts("Vulkan present timing model tests PASS");
    return 0;
}
