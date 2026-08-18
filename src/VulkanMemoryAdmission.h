/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef VULKAN_MEMORY_ADMISSION_H
#define VULKAN_MEMORY_ADMISSION_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <limits>
#include <string>

#include "VulkanCommon.h"

namespace melonDS::Vk
{

inline constexpr VkDeviceSize MemoryMiB = 1024ull * 1024ull;
inline constexpr VkDeviceSize LiveBudgetFixedSafetyReserve = 128ull * MemoryMiB;
inline constexpr u32 InvalidMemoryHeap = ~0u;

// The physical-device query is deliberately kept separate from the renderer.
// A live budget is an estimate supplied by the driver/OS and may change while
// the process is running, so callers refresh this snapshot at device init and
// before a scale-dependent resource recreation rather than every frame.
struct VulkanMemoryAdmissionSnapshot
{
    bool HasLiveBudget = false;
    bool QueryAttempted = false;
    bool UsesHeuristicFallback = true;

    u32 HeapCount = 0;
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> HeapSize{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> HeapBudget{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> HeapUsage{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> CurrentReservedBytes{};
    std::array<bool, VK_MAX_MEMORY_HEAPS> HeapDeviceLocal{};

    u32 MemoryTypeCount = 0;
    std::array<u32, VK_MAX_MEMORY_TYPES> MemoryTypeHeapIndex{};
    u32 PreferredDeviceLocalMemoryType = ~0u;

    VkDeviceSize MaxMemoryAllocationSize = 0;
    u32 MaxMemoryAllocationCount = 0;
    u32 CurrentAllocationCount = 0;
};

struct VulkanMemoryAdmissionRequest
{
    VkDeviceSize ProjectedBytes = 0;
    VkDeviceSize AlreadyReservedBytes = 0;
    VkDeviceSize LargestAllocation = 0;
    u32 AdditionalAllocationCount = 0;
    u32 MemoryTypeIndex = ~0u;
};

struct VulkanMemoryAdmissionResult
{
    bool Accepted = false;
    u32 HeapIndex = InvalidMemoryHeap;
    VkDeviceSize HeapBudget = 0;
    VkDeviceSize HeapUsage = 0;
    VkDeviceSize AvailableBytes = 0;
    VkDeviceSize SafetyReserve = 0;
    std::string Reason;
};

[[nodiscard]] inline bool AddWouldOverflow(VkDeviceSize left, VkDeviceSize right) noexcept
{
    return right > std::numeric_limits<VkDeviceSize>::max() - left;
}

// Pure admission policy. This is intentionally usable by fixture tests with
// synthetic UMA, discrete and multi-heap snapshots; no Vulkan call or global
// state is hidden in the calculation.
[[nodiscard]] inline VulkanMemoryAdmissionResult EvaluateVulkanMemoryAdmission(
    const VulkanMemoryAdmissionSnapshot& snapshot,
    const VulkanMemoryAdmissionRequest& request)
{
    VulkanMemoryAdmissionResult result;

    if (request.MemoryTypeIndex >= snapshot.MemoryTypeCount)
    {
        result.Reason = "memory type index is not present in the capability snapshot";
        return result;
    }

    const u32 heapIndex = snapshot.MemoryTypeHeapIndex[request.MemoryTypeIndex];
    if (heapIndex >= snapshot.HeapCount)
    {
        result.Reason = "memory type maps to an invalid memory heap";
        return result;
    }
    result.HeapIndex = heapIndex;

    if (snapshot.MaxMemoryAllocationSize != 0
        && request.LargestAllocation > snapshot.MaxMemoryAllocationSize)
    {
        result.Reason = "largest allocation exceeds maxMemoryAllocationSize";
        return result;
    }

    if (snapshot.MaxMemoryAllocationCount != 0
        && (request.AdditionalAllocationCount > snapshot.MaxMemoryAllocationCount
            || snapshot.CurrentAllocationCount
                > snapshot.MaxMemoryAllocationCount - request.AdditionalAllocationCount))
    {
        result.Reason = "projected allocation count exceeds maxMemoryAllocationCount";
        return result;
    }

    result.HeapBudget = snapshot.HeapBudget[heapIndex];
    result.HeapUsage = snapshot.HeapUsage[heapIndex];

    if (!snapshot.HasLiveBudget)
    {
        // Preserve the existing fail-safe policy when the optional extension is
        // absent. It applies only to device-local heaps; a host-only heap has no
        // meaningful Vulkan device-local scale budget to compare here.
        if (!snapshot.HeapDeviceLocal[heapIndex])
        {
            result.Accepted = true;
            result.Reason = "no live budget; host heap left to vkAllocateMemory";
            return result;
        }

        if (AddWouldOverflow(request.AlreadyReservedBytes, request.ProjectedBytes)
            || request.AlreadyReservedBytes + request.ProjectedBytes > result.HeapBudget)
        {
            result.Reason = "projected bytes exceed the device-local heuristic budget";
            return result;
        }

        result.AvailableBytes = result.HeapBudget;
        result.Accepted = true;
        result.Reason = "75% device-local heuristic fallback";
        return result;
    }

    result.AvailableBytes = result.HeapBudget > result.HeapUsage
        ? result.HeapBudget - result.HeapUsage : 0;
    result.SafetyReserve = std::max(
        LiveBudgetFixedSafetyReserve,
        result.HeapBudget / 20u); // explicit 5% policy reserve

    if (result.AvailableBytes < result.SafetyReserve)
    {
        result.Reason = "live heap budget has no room after the safety reserve";
        return result;
    }

    const VkDeviceSize spendable = result.AvailableBytes - result.SafetyReserve;
    if (request.ProjectedBytes > spendable)
    {
        result.Reason = "projected bytes exceed live heap budget after the safety reserve";
        return result;
    }

    result.Accepted = true;
    result.Reason = "live heap budget admission";
    return result;
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_MEMORY_ADMISSION_H
