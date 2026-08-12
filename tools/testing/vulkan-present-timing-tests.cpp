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

#include "VulkanPresentPacingPolicy.h"
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
    caps.FifoPresentMode = true;
    caps.TimingPropertiesReady = true;
    caps.TimeDomainsReady = true;
    caps.TargetStageValid = true;
    caps.FrameIntervalKnown = true;
    return caps;
}

VulkanPacingDecision Resolve(Policy policy, const VulkanPacingCapabilities& caps)
{
    return ResolveVulkanPresentPacing(policy, false, false, true, caps);
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
         Reason::PresentId2Unsupported},
        {"absolute timing device",
         [](VulkanPacingCapabilities& c) { c.AbsoluteTimingDevice = false; },
         Reason::AbsoluteTimingUnsupportedDevice},
        {"absolute timing surface",
         [](VulkanPacingCapabilities& c) { c.AbsoluteTimingSurface = false; },
         Reason::AbsoluteTimingUnsupportedSurface},
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

    VulkanPacingCapabilities absent = FullCapabilities();
    absent.PresentTimingSurface = false;
    absent.TimingMetadataEnabled = false;
    Require(Resolve(Policy::JustInTime, absent).Reason == Reason::PresentTimingUnsupported,
        "a surface without present timing must be unsupported, not failed");
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

    TestTargetTimeDoesNotRequirePresentWait2();
    TestBoundedWaitWithoutPresentTiming();
    TestVendorLatencyApisWin();
    TestSpeedAndPolicyGates();
    TestFallbackReasonsAreSpecific();
    TestNonFifoKeepsWaitButNotTarget();

    if (Failures != 0)
    {
        std::fprintf(stderr, "Vulkan present timing model tests FAILED (%d)\n", Failures);
        return 1;
    }
    std::puts("Vulkan present timing model tests PASS");
    return 0;
}
