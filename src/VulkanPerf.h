/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_PERF_H
#define MELONPRIME_VULKAN_PERF_H

#include "types.h"

// Vulkan stage telemetry. The runtime gate is MELONPRIME_PERF=1, shared with
// MelonPrimePerfProbe. Keeping this header in core avoids making the renderer
// depend on the Qt/SDL frontend. The gate intentionally does not use the
// frontend-only developer-feature define: core and frontend must see the same
// inline definitions, and a disabled runtime costs only one predictable branch.
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace melonDS::VulkanPerf
{

enum class CpuMetric : u32
{
    RasterBeginWait = 0,
    TexcacheUpdate,
    BuildPolygons,
    DescriptorUpdate,
    ComposePack,
    Structured2D,
    PresentBeginTotal,
    PresentAcquire,
    PresentImageFence,
    HudUpload,
    QueueSubmit,
    Count,
};

enum class Counter : u32
{
    Frames = 0,
    RasterBeginWaitNs,
    RasterBeginWaitCount,
    RasterBeginNoWaitCount,
    RasterBeginFenceTimeoutCount,
    Polygons,
    Variants,
    YSpans,
    SetupIndices,
    StructuredPackBytes,
    StructuredInputBytesPacked,
    StructuredInputBytesUploaded,
    StructuredInputCopyRegionCount,
    StructuredInputFullUploadCount,
    StructuredInputPartialUploadCount,
    StructuredScreenRouteCopyBytes,
    StructuredScreenRouteCopyNanoseconds,
    StructuredRegularLines,
    StructuredFallbackLines,
    StructuredRouteRuns,
    HudUploadBytes,
    TextureUploadBytes,
    ScratchUploadCount,
    ScratchUploadBytes,
    DescriptorWriteCount,
    DescriptorCreateCount,
    DescriptorUpdateCount,
    DescriptorCopyCount,
    DescriptorCpuTimeNs,
    PresenterSrvCreateCount,
    PresenterDescriptorCopyCount,
    PresenterDescriptorCpuTimeNs,
    PresenterDescriptorUpdateCount,
    PresenterDescriptorCacheHitCount,
    PresenterDescriptorCacheMissCount,
    PresenterDescriptorCacheInvalidateCount,
    PresenterDescriptorFallbackCount,
    PresenterDescriptorPersistentCreateCount,
    CompositorDescriptorUpdateCount,
    CompositorDropCount,
    CaptureValidLineCount,
    CaptureIndependentLineCount,
    CaptureLegacyOrderedLineCount,
    CaptureSidecarDispatchCount,
    CaptureSidecarBarrierCount,
    // Reserved for a future calibrated Vulkan timestamp query. Keep it
    // explicit rather than deriving GPU time from the CPU recording timer.
    CaptureSidecarGpuTimeNs,
    PresentedScreenCopyBytes,
    // Reserved for a future calibrated Vulkan timestamp query. Keep it
    // explicit rather than deriving GPU time from the CPU recording timer.
    PresentedScreenCopyGpuTimeNs,
    DirectCompositorImageFrames,
    FallbackCompositorBufferFrames,
    NativeResolveCount,
    NativeReadbackCopyBytes,
    NativeReadbackDemandCount,
    NativeReadbackWaitNs,
    Count,
};

inline constexpr std::array<const char*, static_cast<std::size_t>(CpuMetric::Count)> CpuMetricNames = {
    "raster_begin_wait",
    "texcache_update",
    "build_polygons",
    "descriptor_update",
    "compose_pack",
    "structured_2d",
    "present_begin_total",
    "present_acquire",
    "present_image_fence",
    "hud_upload",
    "queue_submit",
};

using Clock = std::chrono::steady_clock;

inline bool IsEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_PERF");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

struct SampleWindow
{
    // Busy scenes can update more than 512 descriptor sets per second. Keep a
    // full representative window instead of biasing percentiles toward the
    // first few frames; this storage is touched only when telemetry is enabled.
    static constexpr std::size_t Capacity = 2048;
    std::array<double, Capacity> Microseconds{};
    std::size_t Count = 0;
    double Max = 0.0;

    void Add(double us) noexcept
    {
        if (Count < Capacity)
            Microseconds[Count++] = us;
        Max = std::max(Max, us);
    }

    void Reset() noexcept
    {
        Count = 0;
        Max = 0.0;
    }
};

struct State
{
    std::array<SampleWindow, static_cast<std::size_t>(CpuMetric::Count)> Cpu{};
    std::array<u64, static_cast<std::size_t>(Counter::Count)> Counters{};
    Clock::time_point LastReport{};
    Clock::time_point Structured2DStart{};
    bool Structured2DOpen = false;
    u32 Scale = 0;
};

inline State& GetState() noexcept
{
    static State state;
    return state;
}

inline void SetScale(u32 scale) noexcept
{
    if (IsEnabled())
        GetState().Scale = scale;
}

inline void AddCounter(Counter counter, u64 value = 1) noexcept
{
    if (!IsEnabled())
        return;
    GetState().Counters[static_cast<std::size_t>(counter)] += value;
}

inline void RecordNativeReadbackWait(u64 nanoseconds) noexcept
{
    AddCounter(Counter::NativeReadbackWaitNs, nanoseconds);
}

class ScopedRasterBeginWait
{
public:
    explicit ScopedRasterBeginWait(bool enabled = true) noexcept
        : Enabled(enabled && IsEnabled())
    {
        if (Enabled)
            Start = Clock::now();
    }

    ~ScopedRasterBeginWait()
    {
        if (!Enabled)
            return;
        const u64 ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - Start).count());
        State& state = GetState();
        state.Cpu[static_cast<std::size_t>(CpuMetric::RasterBeginWait)].Add(
            static_cast<double>(ns) / 1000.0);
        state.Counters[static_cast<std::size_t>(Counter::RasterBeginWaitNs)] += ns;
        state.Counters[static_cast<std::size_t>(Counter::RasterBeginWaitCount)]++;
    }

    ScopedRasterBeginWait(const ScopedRasterBeginWait&) = delete;
    ScopedRasterBeginWait& operator=(const ScopedRasterBeginWait&) = delete;

private:
    bool Enabled = false;
    Clock::time_point Start{};
};

inline void RecordRasterBeginNoWait() noexcept
{
    AddCounter(Counter::RasterBeginNoWaitCount);
}

inline void RecordRasterBeginFenceTimeout() noexcept
{
    AddCounter(Counter::RasterBeginFenceTimeoutCount);
}

inline void RecordGeometry(u32 polygons, u32 variants, u32 ySpans, u32 setupIndices) noexcept
{
    if (!IsEnabled())
        return;
    AddCounter(Counter::Polygons, polygons);
    AddCounter(Counter::Variants, variants);
    AddCounter(Counter::YSpans, ySpans);
    AddCounter(Counter::SetupIndices, setupIndices);
}

inline void AddDuration(CpuMetric metric, Clock::time_point start) noexcept
{
    if (!IsEnabled())
        return;
    const auto elapsed = Clock::now() - start;
    const u64 ns = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    const double us = static_cast<double>(ns) / 1000.0;
    GetState().Cpu[static_cast<std::size_t>(metric)].Add(us);
    if (metric == CpuMetric::DescriptorUpdate)
        AddCounter(Counter::DescriptorCpuTimeNs, ns);
}

inline void BeginStructured2DFrame() noexcept
{
    if (!IsEnabled())
        return;
    State& state = GetState();
    state.Structured2DStart = Clock::now();
    state.Structured2DOpen = true;
}

inline void EndStructured2DFrame() noexcept
{
    if (!IsEnabled())
        return;
    State& state = GetState();
    if (!state.Structured2DOpen)
        return;
    AddDuration(CpuMetric::Structured2D, state.Structured2DStart);
    state.Structured2DOpen = false;
}

class ScopedCpuTimer
{
public:
    explicit ScopedCpuTimer(CpuMetric metric) noexcept
        : Metric(metric), Enabled(IsEnabled())
    {
        if (Enabled)
            Start = Clock::now();
    }

    ~ScopedCpuTimer()
    {
        if (Enabled)
            AddDuration(Metric, Start);
    }

    ScopedCpuTimer(const ScopedCpuTimer&) = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;

private:
    CpuMetric Metric;
    bool Enabled = false;
    Clock::time_point Start{};
};

inline double Percentile(const SampleWindow& window, double percentile)
{
    if (window.Count == 0)
        return 0.0;
    std::array<double, SampleWindow::Capacity> sorted{};
    std::copy_n(window.Microseconds.begin(), window.Count, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + window.Count);
    const double index = percentile * static_cast<double>(window.Count - 1);
    const std::size_t lower = static_cast<std::size_t>(index);
    const std::size_t upper = std::min(lower + 1, window.Count - 1);
    const double fraction = index - static_cast<double>(lower);
    return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

inline void MaybeReport()
{
    if (!IsEnabled())
        return;

    State& state = GetState();
    const Clock::time_point now = Clock::now();
    if (state.LastReport == Clock::time_point{})
    {
        state.LastReport = now;
        return;
    }
    if (now - state.LastReport < std::chrono::seconds(1))
        return;

    for (std::size_t index = 0; index < state.Cpu.size(); ++index)
    {
        const SampleWindow& window = state.Cpu[index];
        if (window.Count == 0)
            continue;
        std::fprintf(stderr,
            "[VulkanPerf] cpu scale=%u name=%s p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f n=%zu\n",
            state.Scale, CpuMetricNames[index],
            Percentile(window, 0.50), Percentile(window, 0.95),
            Percentile(window, 0.99), window.Max, window.Count);
    }

    const auto count = [&](Counter counter) -> unsigned long long {
        return static_cast<unsigned long long>(state.Counters[static_cast<std::size_t>(counter)]);
    };
    std::fprintf(stderr,
        "[VulkanPerf] counters scale=%u frames=%llu raster_wait_ns=%llu raster_wait_count=%llu "
        "raster_no_wait_count=%llu raster_timeout_count=%llu polygons=%llu variants=%llu y_spans=%llu "
        "setup_indices=%llu structured_pack_B=%llu structured_input_packed_B=%llu "
        "structured_input_uploaded_B=%llu structured_input_regions=%llu structured_input_full=%llu "
        "structured_input_partial=%llu route_copy_B=%llu route_copy_ns=%llu "
        "regular_lines=%llu fallback_lines=%llu route_runs=%llu hud_upload_B=%llu texture_upload_B=%llu "
        "scratch_uploads=%llu scratch_upload_B=%llu descriptor_writes=%llu descriptor_creates=%llu "
        "descriptor_updates=%llu descriptor_copies=%llu descriptor_cpu_ns=%llu "
        "presenter_srv_creates=%llu presenter_descriptor_copies=%llu "
        "presenter_descriptor_cpu_ns=%llu presenter_descriptor_updates=%llu "
        "presenter_descriptor_cache_hits=%llu presenter_descriptor_cache_misses=%llu "
        "presenter_descriptor_cache_invalidates=%llu presenter_descriptor_fallbacks=%llu "
        "presenter_descriptor_persistent_creates=%llu "
        "compositor_descriptor_updates=%llu compose_drops=%llu "
        "capture_valid_lines=%llu capture_independent_lines=%llu capture_legacy_lines=%llu "
        "capture_sidecar_dispatches=%llu capture_sidecar_barriers=%llu capture_sidecar_gpu_ns=%llu "
        "screen_copy_B=%llu screen_copy_gpu_ns=%llu direct_image_frames=%llu "
        "fallback_buffer_frames=%llu native_resolves=%llu native_readback_copy_B=%llu "
        "native_readback_demands=%llu native_readback_wait_ns=%llu\n",
        state.Scale, count(Counter::Frames), count(Counter::RasterBeginWaitNs),
        count(Counter::RasterBeginWaitCount), count(Counter::RasterBeginNoWaitCount),
        count(Counter::RasterBeginFenceTimeoutCount), count(Counter::Polygons), count(Counter::Variants),
        count(Counter::YSpans), count(Counter::SetupIndices), count(Counter::StructuredPackBytes),
        count(Counter::StructuredInputBytesPacked),
        count(Counter::StructuredInputBytesUploaded),
        count(Counter::StructuredInputCopyRegionCount),
        count(Counter::StructuredInputFullUploadCount),
        count(Counter::StructuredInputPartialUploadCount),
        count(Counter::StructuredScreenRouteCopyBytes),
        count(Counter::StructuredScreenRouteCopyNanoseconds),
        count(Counter::StructuredRegularLines), count(Counter::StructuredFallbackLines),
        count(Counter::StructuredRouteRuns),
        count(Counter::HudUploadBytes), count(Counter::TextureUploadBytes),
        count(Counter::ScratchUploadCount), count(Counter::ScratchUploadBytes),
        count(Counter::DescriptorWriteCount), count(Counter::DescriptorCreateCount),
        count(Counter::DescriptorUpdateCount), count(Counter::DescriptorCopyCount),
        count(Counter::DescriptorCpuTimeNs), count(Counter::PresenterSrvCreateCount),
        count(Counter::PresenterDescriptorCopyCount),
        count(Counter::PresenterDescriptorCpuTimeNs),
        count(Counter::PresenterDescriptorUpdateCount),
        count(Counter::PresenterDescriptorCacheHitCount),
        count(Counter::PresenterDescriptorCacheMissCount),
        count(Counter::PresenterDescriptorCacheInvalidateCount),
        count(Counter::PresenterDescriptorFallbackCount),
        count(Counter::PresenterDescriptorPersistentCreateCount),
        count(Counter::CompositorDescriptorUpdateCount), count(Counter::CompositorDropCount),
        count(Counter::CaptureValidLineCount), count(Counter::CaptureIndependentLineCount),
        count(Counter::CaptureLegacyOrderedLineCount),
        count(Counter::CaptureSidecarDispatchCount), count(Counter::CaptureSidecarBarrierCount),
        count(Counter::CaptureSidecarGpuTimeNs),
        count(Counter::PresentedScreenCopyBytes), count(Counter::PresentedScreenCopyGpuTimeNs),
        count(Counter::DirectCompositorImageFrames), count(Counter::FallbackCompositorBufferFrames),
        count(Counter::NativeResolveCount),
        count(Counter::NativeReadbackCopyBytes), count(Counter::NativeReadbackDemandCount),
        count(Counter::NativeReadbackWaitNs));
    for (SampleWindow& window : state.Cpu)
        window.Reset();
    state.Counters.fill(0);
    state.LastReport = now;
}

} // namespace melonDS::VulkanPerf

#else

namespace melonDS::VulkanPerf
{
enum class CpuMetric : u32
{
    RasterBeginWait,
    TexcacheUpdate,
    BuildPolygons,
    DescriptorUpdate,
    ComposePack,
    Structured2D,
    PresentBeginTotal,
    PresentAcquire,
    PresentImageFence,
    HudUpload,
    QueueSubmit,
    Count,
};
enum class Counter : u32
{
    Frames,
    RasterBeginWaitNs,
    RasterBeginWaitCount,
    RasterBeginNoWaitCount,
    RasterBeginFenceTimeoutCount,
    Polygons,
    Variants,
    YSpans,
    SetupIndices,
    StructuredPackBytes,
    StructuredInputBytesPacked,
    StructuredInputBytesUploaded,
    StructuredInputCopyRegionCount,
    StructuredInputFullUploadCount,
    StructuredInputPartialUploadCount,
    StructuredScreenRouteCopyBytes,
    StructuredScreenRouteCopyNanoseconds,
    StructuredRegularLines,
    StructuredFallbackLines,
    StructuredRouteRuns,
    HudUploadBytes,
    TextureUploadBytes,
    ScratchUploadCount,
    ScratchUploadBytes,
    DescriptorWriteCount,
    DescriptorCreateCount,
    DescriptorUpdateCount,
    DescriptorCopyCount,
    DescriptorCpuTimeNs,
    PresenterSrvCreateCount,
    PresenterDescriptorCopyCount,
    PresenterDescriptorCpuTimeNs,
    PresenterDescriptorUpdateCount,
    PresenterDescriptorCacheHitCount,
    PresenterDescriptorCacheMissCount,
    PresenterDescriptorCacheInvalidateCount,
    PresenterDescriptorFallbackCount,
    PresenterDescriptorPersistentCreateCount,
    CompositorDescriptorUpdateCount,
    CompositorDropCount,
    CaptureValidLineCount,
    CaptureIndependentLineCount,
    CaptureLegacyOrderedLineCount,
    CaptureSidecarDispatchCount,
    CaptureSidecarBarrierCount,
    CaptureSidecarGpuTimeNs,
    PresentedScreenCopyBytes,
    PresentedScreenCopyGpuTimeNs,
    DirectCompositorImageFrames,
    FallbackCompositorBufferFrames,
    NativeResolveCount,
    NativeReadbackCopyBytes,
    NativeReadbackDemandCount,
    NativeReadbackWaitNs,
    Count,
};
inline bool IsEnabled() noexcept { return false; }
inline void SetScale(u32) noexcept {}
inline void AddCounter(Counter, u64 = 1) noexcept {}
inline void RecordNativeReadbackWait(u64) noexcept {}
class ScopedRasterBeginWait
{
public:
    explicit ScopedRasterBeginWait(bool = true) noexcept {}
};
inline void RecordRasterBeginNoWait() noexcept {}
inline void RecordRasterBeginFenceTimeout() noexcept {}
inline void RecordGeometry(u32, u32, u32, u32) noexcept {}
inline void BeginStructured2DFrame() noexcept {}
inline void EndStructured2DFrame() noexcept {}
inline void MaybeReport() {}
class ScopedCpuTimer
{
public:
    explicit ScopedCpuTimer(CpuMetric) noexcept {}
};
} // namespace melonDS::VulkanPerf

#endif

#endif // MELONPRIME_VULKAN_PERF_H
