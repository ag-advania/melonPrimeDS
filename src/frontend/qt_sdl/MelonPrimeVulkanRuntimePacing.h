#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanRuntimePacing requires the Vulkan build gate"
#endif

#include <atomic>
#include <cstdint>

#include "VulkanReference/FrameQueue.h"
#include "types.h"

// MELONPRIME_DESKTOP_ADAPTER_BEGIN
struct VulkanRuntimePacingState
{
    std::atomic_bool fastForward{false};
    std::atomic_bool unlimited{false};
    std::atomic_bool presentationLate{false};
    std::atomic<uint64_t> generation{0};
};

VulkanRuntimePacingState& GetVulkanRuntimePacingState();
void PublishVulkanRuntimePacingState(bool fastForward, bool unlimited);
void ResetVulkanLimiterPhase();
bool ConsumeVulkanLimiterPhaseResetRequest();

FrameQueuePolicy MakeVulkanRealtimeFrameQueuePolicy(int renderScale);
FrameQueuePolicy MakeVulkanLateRealtimeFrameQueuePolicy(int renderScale);
FrameQueuePolicy MakeVulkanFastForwardFrameQueuePolicy(int renderScale);
FrameQueuePolicy ConstrainGraphicsHardwareFrameQueuePolicy(
    FrameQueuePolicy policy,
    bool graphicsHardwareActive,
    bool temporal3dHistoryRequired);
FrameQueuePolicy MakeVulkanFrameQueuePolicy(
    int renderScale,
    bool presentationLate,
    bool graphicsHardwareActive,
    bool temporal3dHistoryRequired);
// MELONPRIME_DESKTOP_ADAPTER_END
