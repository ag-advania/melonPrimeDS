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
    Polygons,
    Variants,
    YSpans,
    SetupIndices,
    StructuredPackBytes,
    HudUploadBytes,
    TextureUploadBytes,
    ScratchUploadCount,
    ScratchUploadBytes,
    DescriptorWriteCount,
    CompositorDropCount,
    PresentedScreenCopyBytes,
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
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    GetState().Cpu[static_cast<std::size_t>(metric)].Add(us);
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
        "[VulkanPerf] counters scale=%u frames=%llu polygons=%llu variants=%llu y_spans=%llu "
        "setup_indices=%llu structured_pack_B=%llu hud_upload_B=%llu texture_upload_B=%llu "
        "scratch_uploads=%llu scratch_upload_B=%llu descriptor_writes=%llu compose_drops=%llu "
        "screen_copy_B=%llu\n",
        state.Scale, count(Counter::Frames), count(Counter::Polygons), count(Counter::Variants),
        count(Counter::YSpans), count(Counter::SetupIndices), count(Counter::StructuredPackBytes),
        count(Counter::HudUploadBytes), count(Counter::TextureUploadBytes),
        count(Counter::ScratchUploadCount), count(Counter::ScratchUploadBytes),
        count(Counter::DescriptorWriteCount), count(Counter::CompositorDropCount),
        count(Counter::PresentedScreenCopyBytes));
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
    Polygons,
    Variants,
    YSpans,
    SetupIndices,
    StructuredPackBytes,
    HudUploadBytes,
    TextureUploadBytes,
    ScratchUploadCount,
    ScratchUploadBytes,
    DescriptorWriteCount,
    CompositorDropCount,
    PresentedScreenCopyBytes,
    Count,
};
inline bool IsEnabled() noexcept { return false; }
inline void SetScale(u32) noexcept {}
inline void AddCounter(Counter, u64 = 1) noexcept {}
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
