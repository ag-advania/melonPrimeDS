/*
    Copyright 2016-2026 melonDS team

    Vendor-neutral Vulkan WSI pacing and presentation telemetry.
*/

#ifndef VULKAN_PRESENT_PACER_H
#define VULKAN_PRESENT_PACER_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <atomic>
#include <string>

#include "VulkanDevice.h"
#include "VulkanPresentPacingPolicy.h"
#include "VulkanPresentTimingModel.h"

namespace melonDS
{

// Result of one vkGetSwapchainTimingPropertiesEXT call.
//
// VK_NOT_READY is a documented, non-fatal answer: a swapchain may not know its
// refresh timing until it has presented at least once. Distinguishing it from a
// real failure is the difference between "retry after the first present" and
// "this driver will never answer, stop asking".
enum class VulkanTimingRefreshResult : int
{
    Updated = 0,
    NotReady,
    Unavailable,
    Failed,
};

// How the display's refresh cadence behaves, derived from the refreshInterval
// the swapchain reports. Diagnostics only: the pacer never rewrites the user's
// VSync setting from this.
enum class VulkanRefreshDynamics : int
{
    Unknown = 0,
    VariableRefresh,
    FixedRefresh,
    DynamicRefresh,
};

// Vendor-neutral presentation pacing for one swapchain.
//
// Ownership: every method is called from the presenting thread (the emulation
// thread, through VulkanPresenter). VK_EXT_present_timing host queries take the
// swapchain as an externally synchronized parameter, and this single-owner rule
// is what provides that synchronization -- no separate lock is introduced.
class VulkanPresentPacer
{
public:
    struct PresentMetadata
    {
        VkPresentId2KHR Id2{};
        VkPresentTimingInfoEXT Timing{};
        VkPresentTimingsInfoEXT Timings{};
        u64 LogicalId = 0;
        u64 Sequence = 0;
        // Meaning depends on TargetMode: an absolute presentation timestamp for
        // Absolute, a minimum previous-image visible duration for Relative.
        // Named "value" rather than "time" precisely so the two are not read as
        // the same quantity.
        u64 TargetValueNs = 0;
        VulkanTargetSchedulingMode TargetMode = VulkanTargetSchedulingMode::None;
        // The cadence inputs this present's relative duration came from, so the
        // capture can re-derive it from the same present rather than from
        // whatever the pacer happened to compute last. Zero unless this present
        // actually carried a relative target.
        VulkanRelativeCadence::Request RelativeRequest{};
        bool TimingAttached = false;
    };

    // One frame's answer to "what target may this present request".
    struct TargetTimingRequest
    {
        VulkanTargetSchedulingMode Mode = VulkanTargetSchedulingMode::None;
        u64 ValueNs = 0;
        // Relative durations that land on whole refresh intervals may also ask
        // for the nearest refresh cycle; unquantized ones must not.
        bool Quantized = false;
        // Empty unless Mode is Relative.
        VulkanRelativeCadence::Request Cadence{};
    };

    bool Initialize(const VulkanDevice& device, VkSurfaceKHR surface);
    void Shutdown() noexcept;

    void SetPolicy(int value) noexcept;
    [[nodiscard]] VulkanPresentPacingPolicy GetPolicy() const noexcept;

    // Queries the real surface and refreshes all surface-scoped support bits.
    // Falls back to the legacy query if modern capability discovery fails.
    bool QuerySurfaceCapabilities(VkSurfaceCapabilitiesKHR& capabilities);
    [[nodiscard]] VkSwapchainCreateFlagsKHR GetSwapchainCreateFlags() const noexcept;
    [[nodiscard]] bool ShouldUseFifoLatestReady() const noexcept;

    // `imageCount` sizes the optional timing-results queue: a report only frees
    // its slot once the presentation engine has completed it, which can take
    // several refreshes, so a swapchain with more images in flight needs more
    // slots before it starts rejecting presents.
    void OnSwapchainCreated(
        VkSwapchainKHR swapchain, VkPresentModeKHR presentMode, u32 imageCount);
    void OnSwapchainDestroyed() noexcept;

    // Called immediately before late input sampling.
    //
    // The result distinguishes a swapchain that must be rebuilt from a device
    // that was lost; route it through VulkanPacerActionFor() rather than
    // treating any non-Continue value as "recreate the swapchain".
    //
    // `targetFrameIntervalNs` is the emulator's own frame interval, or 0 when
    // the host is not running at a fixed rate. It is never derived from the
    // display refresh rate: the DS frame rate is a property of the emulated
    // machine and its configured TargetFPS, not of the monitor.
    [[nodiscard]] VulkanPacerBeginResult BeginFrame(
        bool reflexActive, bool antiLagActive, bool normalSpeed, u64 targetFrameIntervalNs);

    // Adds present_id2 and timing metadata to VkPresentInfoKHR. `preferredId`
    // is the Reflex correlation id when available, otherwise zero.
    u64 PreparePresent(VkPresentInfoKHR& present, u64 preferredId, PresentMetadata& metadata);
    // A full optional timing-results queue rejects the present itself. Drop
    // only timing metadata so the caller can retry the same image, the same
    // logical ID and the same presentation sequence. The wait semaphores are
    // dropped too: see the definition for why the retry must not re-wait them.
    bool PrepareRetryWithoutTiming(
        VkResult result, VkPresentInfoKHR& present, PresentMetadata& metadata);
    void NotifyPresentResult(VkResult result, const PresentMetadata& metadata) noexcept;

    void LogState(const char* context) const;
    [[nodiscard]] VulkanPacingAuthority GetAuthority() const noexcept;
    [[nodiscard]] bool IsTargetSchedulingActive() const noexcept;

    // Everything the A/B latency capture records about pacing state for one
    // frame. Reading it must not change any of it, which is why this is a
    // by-value snapshot rather than a set of accessors: an A/B build and a
    // normal build must present identically.
    struct StateSnapshot
    {
        int Policy = 0;
        int Authority = 0;
        int PresentMode = 0;
        // Monotonic for the lifetime of this pacer. The timing counters below
        // are reset for every swapchain, so the capture must be able to reject
        // a measured window that crosses a recreation rather than treating a
        // reset counter as a run total.
        u64 SwapchainGeneration = 0;
        bool TargetTimeScheduling = false;
        // Allowed by the policy and capabilities this frame.
        bool BoundedPresentWait = false;
        // Actually called vkWaitForPresent2KHR. The two differ whenever there
        // is nothing to wait on -- no accepted present yet, or the previous one
        // was already waited for -- so an A/B cannot read "the wait ran" from
        // the permission alone.
        bool BoundedWaitAttempted = false;
        int FallbackReason = 0;
        // Without the mode, a capture cannot tell an A2 run that actually
        // scheduled from one that silently fell through to no target.
        int TargetMode = 0;
        u64 TargetValueNs = 0;
        // The relative-cadence inputs the target was generated from, so a
        // capture can verify TargetValueNs == RelativeQuanta x
        // TargetGenerationRefreshIntervalNs per present. The pacer's periodic
        // log cannot show that: its refresh interval is the current one, which
        // may already have moved on from the one the target was built against.
        u64 TargetGenerationRefreshIntervalNs = 0;
        u64 TargetGenerationRefreshDurationNs = 0;
        u64 RelativeQuanta = 0;
        u64 RelativeAccumulatorBeforeNs = 0;
        u64 RelativeAccumulatorAfterNs = 0;
        u64 FeedbackPresentId = 0;
        u64 FeedbackStageTimeNs = 0;
        u64 BaselineSequence = 0;
        u64 PresentSequence = 0;
        u64 FrameIntervalNs = 0;
        u32 WaitTimeouts = 0;
        u32 TimingQueueSize = 0;
        u32 TimingQueueFullCount = 0;
        u32 TimingQueueRecoveries = 0;
    };
    // Takes the present it describes: the target columns must report what that
    // accepted present actually carried, not what the resolver permitted or
    // what some earlier present happened to request.
    [[nodiscard]] StateSnapshot CaptureState(const PresentMetadata& metadata) const noexcept;

private:
    // Snapshots every capability the pure resolver needs. Building it is a few
    // bool copies; it is not a driver query.
    [[nodiscard]] VulkanPacingCapabilities BuildCapabilities() const noexcept;
    [[nodiscard]] VulkanPacingDecision ResolveDecision(
        bool reflexActive, bool antiLagActive, bool normalSpeed) const noexcept;
    VulkanTimingRefreshResult RefreshTimingProperties();
    bool RefreshTimeDomains();
    void SelectTargetPresentStage() noexcept;
    // Returns false when the driver refused the requested queue size. The
    // caller must distinguish the initial allocation (no queue exists, so no
    // present may carry timing metadata) from a growth attempt (the previous
    // queue is still valid and keeps being used).
    [[nodiscard]] bool ApplyTimingQueueSize(u32 size);
    [[nodiscard]] VkPresentStageFlagsEXT RequestedStageQueries() const noexcept;
    void ResetTimingLifecycle() noexcept;
    void ReportPastTiming();
    // Absolute and relative are deliberately separate functions: one returns a
    // point on a clock, the other a duration, and merging them behind one
    // return value is how the distinction gets lost.
    [[nodiscard]] TargetTimingRequest EvaluateTargetTiming(u64 sequence) noexcept;
    [[nodiscard]] u64 EvaluateAbsoluteTargetTime(u64 sequence) noexcept;
    [[nodiscard]] VulkanRelativeCadence::Request EvaluateRelativeTargetDuration() noexcept;
    void DisableWait(const char* reason);
    void LogTargetSchedulingIfChanged();

    const VulkanDevice* Device = nullptr;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    // This is deliberately not part of ResetTimingLifecycle(): it identifies
    // the lifecycle that was reset and remains observable in the next capture
    // row. Initialize/Shutdown may reuse the pacer, so lifetime monotonicity is
    // more useful than a swapchain-local 0/1 flag.
    u64 SwapchainGeneration = 0;

    std::atomic<int> Policy{static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly)};
    std::atomic<int> Authority{static_cast<int>(VulkanPacingAuthority::GenericHost)};
    std::atomic<bool> TargetSchedulingActive{false};

    bool Caps2Available = false;
    bool PresentId2Device = false;
    bool PresentWait2Device = false;
    bool PresentTimingDevice = false;
    bool AbsoluteTimingDevice = false;
    bool RelativeTimingDevice = false;
    bool LatestReadyDevice = false;
    bool TimeDomainQueryAvailable = false;
    bool PresentId2Surface = false;
    bool PresentWait2Surface = false;
    bool PresentTimingSurface = false;
    bool PresentTimingRelativeSurface = false;
    bool PresentTimingAbsoluteSurface = false;
    bool WaitRuntimeEnabled = false;
    VkPresentStageFlagsEXT PresentStageQueries = 0;

    // Attaching timing metadata to presents and draining the results queue are
    // separate switches. A full results queue retires the former while the
    // latter must keep running -- draining is exactly what makes room again.
    bool TimingMetadataEnabled = false;
    bool TimingResultsQueryEnabled = false;

    // Sticky across swapchain recreation: once a surface has proved that the
    // timing lifecycle cannot complete, re-selecting FIFO_LATEST_READY for it
    // would just rebuild the swapchain into the same dead end.
    bool TargetSchedulingLifecycleFailed = false;

    // --- Phase 1: timing properties -----------------------------------------
    u64 TimingPropertiesCounter = 0;
    bool TimingPropertiesReady = false;
    bool TimingPropertiesRetryPending = false;
    VulkanRefreshDynamics RefreshDynamics = VulkanRefreshDynamics::Unknown;

    // --- Phase 2: time domains ----------------------------------------------
    u64 TimeDomainsCounter = 0;
    bool TimeDomainsReady = false;
    bool TimeDomainsRetryPending = false;
    VkTimeDomainKHR TargetTimeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
    u64 TargetTimeDomainId = 0;
    VkPresentStageFlagsEXT TargetPresentStage = 0;

    // --- Phase 3/4: feedback and target scheduling ---------------------------
    VulkanPresentTimingModel TimingModel;
    VulkanRelativeCadence RelativeCadence;
    // Resolved once per frame in BeginFrame() and reused by PreparePresent(),
    // so the wait decision and the scheduling decision can never disagree.
    VulkanPacingDecision LastDecision{};
    // Switching between absolute and relative restarts the cadence: a fraction
    // accumulated while absolute was driving describes nothing relative needs.
    VulkanTargetSchedulingMode LastTargetMode = VulkanTargetSchedulingMode::None;
    u64 TargetFrameIntervalNs = 0;
    u64 LastTargetValueNs = 0;
    VulkanTargetSchedulingMode LastAppliedTargetMode = VulkanTargetSchedulingMode::None;
    VulkanRelativeCadence::Request LastRelativeRequest{};
    bool FifoFamilyPresentMode = true;
    VulkanJitFallbackReason FallbackReason = VulkanJitFallbackReason::TelemetryOnlyPolicy;
    VulkanJitFallbackReason LoggedFallbackReason = VulkanJitFallbackReason::None;
    bool LoggedTargetSchedulingActive = false;

    u64 RefreshDurationNs = 0;
    u64 RefreshIntervalNs = 0;
    u64 LastSubmittedId = 0;
    u64 LastPresentedId = 0;
    u64 LastWaitedId = 0;
    u64 LastFeedbackId = 0;
    u64 LastFeedbackStageTimeNs = 0;
    // Whether this frame actually issued the bounded wait, as opposed to being
    // allowed to. Reset at the top of every BeginFrame.
    bool WaitAttemptedThisFrame = false;
    u32 TimingReportCountdown = 0;
    u32 WaitTimeouts = 0;

    // --- optional timing-results queue --------------------------------------
    // Recovery is bounded. A queue that fills once is worth one attempt at a
    // larger queue; a queue that keeps filling means this driver reports too
    // slowly for the emulator's present rate, and retrying forever would just
    // pay the rejected-present cost every frame.
    static constexpr u32 MaxTimingQueueRecoveries = 3;
    // A present that requests timing needs a results-queue slot. Without a
    // queue there is nothing to attach metadata to, so this gates both.
    bool TimingQueueAllocated = false;
    u32 TimingQueueSize = 0;
    // Presents accepted with timing metadata whose report has not come back.
    //
    // Presentation timing feedback is asynchronous and the extension promises
    // only that a result becomes available in finite time -- never when. One
    // empty poll therefore does not prove the queue is drained, so the decision
    // to stop polling is made from this counter instead.
    u64 OutstandingTimedPresents = 0;
    u32 TimingQueueFullCount = 0;
    u32 TimingQueueRecoveries = 0;
    // Sticky while metadata is paused because the finite timing-results queue
    // is under pressure. Cleared only after a larger queue is installed.
    bool TimingQueuePressureActive = false;
    bool TimingQueueRecoveryPending = false;
    std::string WaitDisabledReason;
};

const char* VulkanPresentPacingPolicyName(VulkanPresentPacingPolicy policy) noexcept;
const char* VulkanPacingAuthorityName(VulkanPacingAuthority authority) noexcept;
const char* VulkanTargetSchedulingModeName(VulkanTargetSchedulingMode mode) noexcept;
const char* VulkanRefreshDynamicsName(VulkanRefreshDynamics dynamics) noexcept;
const char* VulkanJitFallbackReasonName(VulkanJitFallbackReason reason) noexcept;

} // namespace melonDS

#endif
#endif
