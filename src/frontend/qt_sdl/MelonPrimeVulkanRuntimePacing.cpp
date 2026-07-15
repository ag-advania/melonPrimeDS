#include "MelonPrimeVulkanRuntimePacing.h"

#include <algorithm>

#include "VulkanContext.h"
#include "VulkanDesktopCompat.h"

namespace
{
VulkanRuntimePacingState gVulkanRuntimePacingState{};
std::atomic_bool gLimiterPhaseResetRequested{false};
} // namespace

VulkanRuntimePacingState& GetVulkanRuntimePacingState()
{
    return gVulkanRuntimePacingState;
}

void PublishVulkanRuntimePacingState(bool fastForward, bool unlimited)
{
    auto& state = GetVulkanRuntimePacingState();
    state.fastForward.store(fastForward, std::memory_order_release);
    state.unlimited.store(unlimited, std::memory_order_release);
    state.generation.fetch_add(1, std::memory_order_release);
    MelonDSAndroid::PublishVulkanDesktopPacingState(fastForward, unlimited);
}

void ResetVulkanLimiterPhase()
{
    gLimiterPhaseResetRequested.store(true, std::memory_order_release);
    GetVulkanRuntimePacingState().generation.fetch_add(1, std::memory_order_release);
}

bool ConsumeVulkanLimiterPhaseResetRequest()
{
    return gLimiterPhaseResetRequested.exchange(false, std::memory_order_acq_rel);
}

FrameQueuePolicy MakeVulkanRealtimeFrameQueuePolicy(int renderScale)
{
    FrameQueuePolicy policy{};
    policy.MaxBacklogDepth = renderScale > 1 ? 2 : 1;
    policy.AllowStealPending = false;
    policy.AllowPreviousFrameReuse = true;
    policy.AllowDropForDeadline = false;
    policy.PreferOldestFrame = false;
    policy.PreserveBacklogOnPresent = false;
    return policy;
}

FrameQueuePolicy MakeVulkanLateRealtimeFrameQueuePolicy(int renderScale)
{
    FrameQueuePolicy policy = MakeVulkanRealtimeFrameQueuePolicy(renderScale);
    policy.AllowDropForDeadline = true;
    return policy;
}

FrameQueuePolicy MakeVulkanFastForwardFrameQueuePolicy(int renderScale)
{
    FrameQueuePolicy policy{};
    const bool highResolutionFastForward = renderScale > 1;
    policy.MaxBacklogDepth = highResolutionFastForward ? 2 : 1;
    policy.AllowStealPending = true;
    policy.AllowPreviousFrameReuse = false;
    policy.AllowDropForDeadline = false;
    policy.PreferOldestFrame = false;
    policy.PreserveBacklogOnPresent = false;
    policy.TreatBacklogTrimAsFastForwardSkip = true;
    return policy;
}

FrameQueuePolicy ConstrainGraphicsHardwareFrameQueuePolicy(
    FrameQueuePolicy policy,
    bool graphicsHardwareActive,
    bool temporal3dHistoryRequired)
{
    if (!graphicsHardwareActive)
        return policy;

    if (MelonDSAndroid::isFastForwardActive())
        return policy;

    const auto& deviceProfile = melonDS::VulkanContext::Get().GetDeviceProfile();
    if (temporal3dHistoryRequired && (deviceProfile.IsAdreno || deviceProfile.IsArmMali))
        return policy;

    policy.MaxBacklogDepth = 1;
    policy.AllowStealPending = false;
    policy.AllowPreviousFrameReuse = false;
    policy.AllowDropForDeadline = false;
    policy.PreferOldestFrame = false;
    policy.PreserveBacklogOnPresent = false;
    return policy;
}

FrameQueuePolicy MakeVulkanFrameQueuePolicy(
    int renderScale,
    bool presentationLate,
    bool graphicsHardwareActive,
    bool temporal3dHistoryRequired)
{
    const int scale = std::max(renderScale, 1);
    FrameQueuePolicy policy = MelonDSAndroid::isFastForwardActive()
        ? MakeVulkanFastForwardFrameQueuePolicy(scale)
        : (presentationLate
            ? MakeVulkanLateRealtimeFrameQueuePolicy(scale)
            : MakeVulkanRealtimeFrameQueuePolicy(scale));
    return ConstrainGraphicsHardwareFrameQueuePolicy(
        policy, graphicsHardwareActive, temporal3dHistoryRequired);
}
