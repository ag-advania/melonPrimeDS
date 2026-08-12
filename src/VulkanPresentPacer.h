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
#include "VulkanPresentTimingModel.h"

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

// Why a target presentation time was not requested this frame. Reported in the
// developer summary so an A/B session can tell "JIT is off because the policy
// says so" apart from "JIT is off because this driver never answered".
enum class VulkanJitFallbackReason : int
{
    None = 0,
    TelemetryOnlyPolicy,
    VendorLatencyApiOwnsPacing,
    NotNormalSpeed,
    PresentId2Unsupported,
    PresentWait2Unsupported,
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

    void OnSwapchainCreated(VkSwapchainKHR swapchain, VkPresentModeKHR presentMode);
    void OnSwapchainDestroyed() noexcept;

    // Called immediately before late input sampling. Returns true when the
    // swapchain was reported out of date and should be rebuilt.
    //
    // `targetFrameIntervalNs` is the emulator's own frame interval, or 0 when
    // the host is not running at a fixed rate. It is never derived from the
    // display refresh rate: the DS frame rate is a property of the emulated
    // machine and its configured TargetFPS, not of the monitor.
    bool BeginFrame(
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
    void SelectAuthority(bool reflexActive, bool antiLagActive, bool normalSpeed) noexcept;
    VulkanTimingRefreshResult RefreshTimingProperties();
    bool RefreshTimeDomains();
    void SelectTargetPresentStage() noexcept;
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
    bool PresentTimingRuntimeEnabled = false;
    bool PresentTimingRelative = false;
    bool PresentTimingAbsolute = false;
    bool WaitRuntimeEnabled = false;
    VkPresentStageFlagsEXT PresentStageQueries = 0;

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
    u32 TimingQueueFullCount = 0;
    std::string WaitDisabledReason;
};

const char* VulkanPresentPacingPolicyName(VulkanPresentPacingPolicy policy) noexcept;
const char* VulkanPacingAuthorityName(VulkanPacingAuthority authority) noexcept;
const char* VulkanRefreshDynamicsName(VulkanRefreshDynamics dynamics) noexcept;
const char* VulkanJitFallbackReasonName(VulkanJitFallbackReason reason) noexcept;

} // namespace melonDS

#endif
#endif
