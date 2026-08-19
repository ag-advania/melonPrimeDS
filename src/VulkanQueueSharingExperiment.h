/*
    Copyright 2016-2026 melonDS team

    Pure policy model for the split graphics/present queue A/B. The runtime
    keeps the production-safe CONCURRENT path unless the experiment is
    explicitly requested and the SyncVal gate has been recorded clean.
*/

#ifndef VULKAN_QUEUE_SHARING_EXPERIMENT_H
#define VULKAN_QUEUE_SHARING_EXPERIMENT_H

namespace melonDS::Vk
{

enum class VulkanQueueSharingMode
{
    Exclusive,
    Concurrent,
};

struct VulkanQueueSharingPlan
{
    bool SplitQueue = false;
    bool Requested = false;
    bool SyncValGate = false;
    VulkanQueueSharingMode Mode = VulkanQueueSharingMode::Exclusive;

    [[nodiscard]] bool UseOwnershipTransfer() const noexcept
    {
        return SplitQueue && Mode == VulkanQueueSharingMode::Exclusive;
    }
};

[[nodiscard]] constexpr VulkanQueueSharingPlan MakeVulkanQueueSharingPlan(
    bool splitQueue, bool requested, bool syncValGate) noexcept
{
    VulkanQueueSharingPlan plan;
    plan.SplitQueue = splitQueue;
    plan.Requested = requested;
    plan.SyncValGate = syncValGate;
    plan.Mode = splitQueue && requested && syncValGate
        ? VulkanQueueSharingMode::Exclusive
        : VulkanQueueSharingMode::Concurrent;
    if (!splitQueue)
        plan.Mode = VulkanQueueSharingMode::Exclusive;
    return plan;
}

} // namespace melonDS::Vk

#endif // VULKAN_QUEUE_SHARING_EXPERIMENT_H
