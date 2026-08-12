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
        u64 TargetTimeNs = 0;
        bool TimingAttached = false;
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
    // logical ID and the same presentation sequence.
    bool PrepareRetryWithoutTiming(VkResult result, PresentMetadata& metadata);
    void NotifyPresentResult(VkResult result, const PresentMetadata& metadata) noexcept;

    void LogState(const char* context) const;
    [[nodiscard]] VulkanPacingAuthority GetAuthority() const noexcept;
    [[nodiscard]] bool IsTargetSchedulingActive() const noexcept;

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
    u64 EvaluateTargetTime(u64 sequence) noexcept;
    void DisableWait(const char* reason);
    void LogTargetSchedulingIfChanged();

    const VulkanDevice* Device = nullptr;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;

    std::atomic<int> Policy{static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly)};
    std::atomic<int> Authority{static_cast<int>(VulkanPacingAuthority::GenericHost)};
    std::atomic<bool> TargetSchedulingActive{false};

    bool Caps2Available = false;
    bool PresentId2Device = false;
    bool PresentWait2Device = false;
    bool PresentTimingDevice = false;
    bool AbsoluteTimingDevice = false;
    bool LatestReadyDevice = false;
    bool TimeDomainQueryAvailable = false;
    bool PresentId2Surface = false;
    bool PresentWait2Surface = false;
    bool PresentTimingSurface = false;
    bool PresentTimingRelative = false;
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
    // Resolved once per frame in BeginFrame() and reused by PreparePresent(),
    // so the wait decision and the scheduling decision can never disagree.
    VulkanPacingDecision LastDecision{};
    u64 TargetFrameIntervalNs = 0;
    u64 LastTargetTimeNs = 0;
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
    u32 TimingQueueFullCount = 0;
    u32 TimingQueueRecoveries = 0;
    bool TimingQueueRecoveryPending = false;
    std::string WaitDisabledReason;
};

const char* VulkanPresentPacingPolicyName(VulkanPresentPacingPolicy policy) noexcept;
const char* VulkanPacingAuthorityName(VulkanPacingAuthority authority) noexcept;
const char* VulkanRefreshDynamicsName(VulkanRefreshDynamics dynamics) noexcept;
const char* VulkanJitFallbackReasonName(VulkanJitFallbackReason reason) noexcept;

} // namespace melonDS

#endif
#endif
