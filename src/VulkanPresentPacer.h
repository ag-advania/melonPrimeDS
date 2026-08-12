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

class VulkanPresentPacer
{
public:
    struct PresentMetadata
    {
        VkPresentId2KHR Id2{};
        VkPresentTimingInfoEXT Timing{};
        VkPresentTimingsInfoEXT Timings{};
        u64 LogicalId = 0;
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
    bool BeginFrame(bool reflexActive, bool antiLagActive, bool normalSpeed);

    // Adds present_id2 and timing telemetry to VkPresentInfoKHR. `preferredId`
    // is the Reflex correlation id when available, otherwise zero.
    u64 PreparePresent(VkPresentInfoKHR& present, u64 preferredId, PresentMetadata& metadata);
    // A full optional timing-results queue rejects the present itself. Drop
    // only timing metadata so the caller can retry the same image and ID.
    bool PrepareRetryWithoutTiming(VkResult result, PresentMetadata& metadata);
    void NotifyPresentResult(VkResult result, u64 logicalId) noexcept;

    void LogState(const char* context) const;
    [[nodiscard]] VulkanPacingAuthority GetAuthority() const noexcept;

private:
    void SelectAuthority(bool reflexActive, bool antiLagActive, bool normalSpeed) noexcept;
    void RefreshTimingProperties();
    void ReportPastTiming();
    void DisableWait(const char* reason);

    const VulkanDevice* Device = nullptr;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;

    std::atomic<int> Policy{static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly)};
    std::atomic<int> Authority{static_cast<int>(VulkanPacingAuthority::GenericHost)};

    bool Caps2Available = false;
    bool PresentId2Device = false;
    bool PresentWait2Device = false;
    bool PresentTimingDevice = false;
    bool LatestReadyDevice = false;
    bool PresentId2Surface = false;
    bool PresentWait2Surface = false;
    bool PresentTimingSurface = false;
    bool PresentTimingRuntimeEnabled = false;
    bool PresentTimingRelative = false;
    bool WaitRuntimeEnabled = false;
    VkPresentStageFlagsEXT PresentStageQueries = 0;

    u64 RefreshDurationNs = 0;
    u64 RefreshIntervalNs = 0;
    u64 LastSubmittedId = 0;
    u64 LastPresentedId = 0;
    u64 LastWaitedId = 0;
    u32 TimingReportCountdown = 0;
    u32 WaitTimeouts = 0;
    std::string WaitDisabledReason;
};

const char* VulkanPresentPacingPolicyName(VulkanPresentPacingPolicy policy) noexcept;
const char* VulkanPacingAuthorityName(VulkanPacingAuthority authority) noexcept;

} // namespace melonDS

#endif
#endif
