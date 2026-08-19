/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef VULKAN_MEMORY_TELEMETRY_H
#define VULKAN_MEMORY_TELEMETRY_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#if defined(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
#include <array>
#endif

#include "VulkanCommon.h"

namespace melonDS::Vk
{

#if defined(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)

// Detailed allocation sizes are recorded at the vkAllocateMemory/vkFreeMemory
// boundary, not from RenderFrame(). This diagnostic model is deliberately
// compiled out of shipping builds; the admission path owns only the minimal
// current reservation counters needed for safety.
inline constexpr u32 VulkanMemoryTelemetryBucketCount = 8;

struct VulkanMemoryTelemetrySnapshot
{
    u32 CurrentAllocationCount = 0;
    u32 PeakAllocationCount = 0;
    u64 TotalAllocationCount = 0;
    u64 TotalFreeCount = 0;

    VkDeviceSize CurrentBytes[VK_MAX_MEMORY_HEAPS]{};
    VkDeviceSize PeakBytes[VK_MAX_MEMORY_HEAPS]{};
    VkDeviceSize TotalAllocatedBytes = 0;
    VkDeviceSize TotalFreedBytes = 0;
    VkDeviceSize LargestAllocation = 0;

    std::array<u64, VulkanMemoryTelemetryBucketCount> AllocationSizeBuckets{};
};

class VulkanMemoryTelemetry
{
public:
    // Called while VulkanDevice::MemoryMutex is held. Keeping the model
    // lock-free makes allocation admission a single short critical section.
    void RecordAllocation(u32 heapIndex, VkDeviceSize size) noexcept
    {
        ++CurrentAllocationCount;
        ++TotalAllocationCount;
        TotalAllocatedBytes += size;
        if (size > LargestAllocation)
            LargestAllocation = size;

        if (heapIndex < VK_MAX_MEMORY_HEAPS)
        {
            CurrentBytes[heapIndex] += size;
            if (CurrentBytes[heapIndex] > PeakBytes[heapIndex])
                PeakBytes[heapIndex] = CurrentBytes[heapIndex];
        }
        if (CurrentAllocationCount > PeakAllocationCount)
            PeakAllocationCount = CurrentAllocationCount;
        ++AllocationSizeBuckets[BucketFor(size)];
    }

    void RecordFree(u32 heapIndex, VkDeviceSize size) noexcept
    {
        if (CurrentAllocationCount != 0)
            --CurrentAllocationCount;
        ++TotalFreeCount;
        TotalFreedBytes += size;

        if (heapIndex < VK_MAX_MEMORY_HEAPS)
        {
            const VkDeviceSize current = CurrentBytes[heapIndex];
            CurrentBytes[heapIndex] = current > size ? current - size : 0;
        }
    }

    [[nodiscard]] VulkanMemoryTelemetrySnapshot GetSnapshot() const noexcept
    {
        VulkanMemoryTelemetrySnapshot result;
        result.CurrentAllocationCount = CurrentAllocationCount;
        result.PeakAllocationCount = PeakAllocationCount;
        result.TotalAllocationCount = TotalAllocationCount;
        result.TotalFreeCount = TotalFreeCount;
        for (u32 i = 0; i < VK_MAX_MEMORY_HEAPS; ++i)
        {
            result.CurrentBytes[i] = CurrentBytes[i];
            result.PeakBytes[i] = PeakBytes[i];
        }
        result.TotalAllocatedBytes = TotalAllocatedBytes;
        result.TotalFreedBytes = TotalFreedBytes;
        result.LargestAllocation = LargestAllocation;
        result.AllocationSizeBuckets = AllocationSizeBuckets;
        return result;
    }

private:
    static u32 BucketFor(VkDeviceSize size) noexcept
    {
        constexpr VkDeviceSize MiB = 1024ull * 1024ull;
        if (size <= MiB)
            return 0;
        if (size <= 4ull * MiB)
            return 1;
        if (size <= 16ull * MiB)
            return 2;
        if (size <= 64ull * MiB)
            return 3;
        if (size <= 256ull * MiB)
            return 4;
        if (size <= 1024ull * MiB)
            return 5;
        if (size <= 4096ull * MiB)
            return 6;
        return 7;
    }

    u32 CurrentAllocationCount = 0;
    u32 PeakAllocationCount = 0;
    u64 TotalAllocationCount = 0;
    u64 TotalFreeCount = 0;
    VkDeviceSize CurrentBytes[VK_MAX_MEMORY_HEAPS]{};
    VkDeviceSize PeakBytes[VK_MAX_MEMORY_HEAPS]{};
    VkDeviceSize TotalAllocatedBytes = 0;
    VkDeviceSize TotalFreedBytes = 0;
    VkDeviceSize LargestAllocation = 0;
    std::array<u64, VulkanMemoryTelemetryBucketCount> AllocationSizeBuckets{};
};

#else

// Shipping facade: detailed allocation telemetry has no fields, no locks, and
// no counter updates. VulkanDevice keeps the separate minimal reservation
// counters used by the memory-safety admission policy.
struct VulkanMemoryTelemetrySnapshot
{
};

class VulkanMemoryTelemetry
{
public:
    constexpr VulkanMemoryTelemetry() noexcept = default;
    constexpr void RecordAllocation(u32, VkDeviceSize) noexcept {}
    constexpr void RecordFree(u32, VkDeviceSize) noexcept {}
    [[nodiscard]] constexpr VulkanMemoryTelemetrySnapshot GetSnapshot() const noexcept
    {
        return {};
    }
};

#endif

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_MEMORY_TELEMETRY_H
