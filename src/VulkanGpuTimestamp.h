/*
    Copyright 2016-2026 melonDS team

    Small helper for optional Vulkan timestamp-query telemetry. It deliberately
    lives above FrameRing: synchronization owns query lifetime, while each
    renderer owns the meaning of a pair of markers.
*/

#ifndef MELONPRIME_VULKAN_GPU_TIMESTAMP_H
#define MELONPRIME_VULKAN_GPU_TIMESTAMP_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GpuStageMetrics.h"
#include "VulkanPerf.h"
#include "VulkanSync.h"

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <limits>
#endif

namespace melonDS
{

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)

inline u64 ReadVulkanGpuMetricNanoseconds(
    const Vk::FrameRing& frames, GpuMetric metric) noexcept
{
    u64 values[2]{};
    const u32 first = GpuMetricQueryIndex(metric, false);
    if (!frames.ReadCurrentFrameTimestamps(first, 2, values)
        || values[1] < values[0])
    {
        return 0;
    }

    const long double nanoseconds = static_cast<long double>(values[1] - values[0])
        * static_cast<long double>(frames.GetTimestampPeriodNs());
    if (!(nanoseconds > 0.0L)
        || nanoseconds >= static_cast<long double>(std::numeric_limits<u64>::max()))
    {
        return 0;
    }
    return static_cast<u64>(nanoseconds + 0.5L);
}

inline void RecordVulkanGpuMetric(
    const Vk::FrameRing& frames, GpuMetric metric,
    VulkanPerf::Counter counter) noexcept
{
    if (!VulkanPerf::IsEnabled() || !frames.HasTimestampQueries())
        return;
    const u64 nanoseconds = ReadVulkanGpuMetricNanoseconds(frames, metric);
    if (nanoseconds != 0)
        VulkanPerf::AddCounter(counter, nanoseconds);
}

#else

inline constexpr void RecordVulkanGpuMetric(
    const Vk::FrameRing&, GpuMetric, VulkanPerf::Counter) noexcept
{
}

#endif

} // namespace melonDS

#endif

#endif // MELONPRIME_VULKAN_GPU_TIMESTAMP_H
