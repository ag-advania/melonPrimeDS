/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_DX12_PERF_H
#define MELONPRIME_DX12_PERF_H

#include "types.h"

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "MelonPrimePerfClock.h"
#endif

namespace melonDS::DX12Perf
{

enum class CpuMetric : u32
{
    RasterBeginWait = 0,
    RasterCpuPrepare,
    RasterReuseWait,
    RasterRecordSubmit,
    TexcacheUpdate,
    BuildPolygons,
    Soft2DTotal,
    Structured2DMetadata,
    TextureDecode,
    TexturePendingCpuCopy,
    TextureResourceCreate,
    TexturePendingStorageGrow,
    SpanStagingCopy,
    DescriptorUpdate,
    ComposePack,
    ComposeRecord,
    CaptureWait,
    CaptureMapCopy,
    PresentSlotWait,
    PresentBeginWait,
    HudPatchCopy,
    HudUpload,
    PresentRecord,
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
    IdenticalFrames,
    DX12IndirectArgsCopyBytes,
    DX12IndirectArgsCopyCount,
    DX12IndirectArgsDirectWriteCount,
    Polygons,
    Variants,
    YSpans,
    SetupIndices,
    SpanUploadBytes,
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
    NativeGPU2DFrames,
    NativeGPU2DInputPackBytes,
    NativeGPU2DVRAMUploadBytes,
    NativeGPU2DPaletteUploadBytes,
    NativeGPU2DOAMUploadBytes,
    NativeGPU2DDispatchCount,
    NativeGPU2DFallbackFrames,
    NativeGPU2DReadbackBytes,
    NativeGPU2DReadbackCount,
    NativeGPU2DMismatchCount,
    TextureUploadBytes,
    TextureMaterializeCount,
    TextureMaterializePreFenceFailCount,
    TextureMaterializeRetryAfterRetireCount,
    TextureMaterializeRetrySuccessCount,
    TextureMaterializeRetryFailCount,
    TextureMaterializeFailureReason,
    TexturePendingUploadBytes,
    TexturePendingUploadCount,
    TexturePendingStorageGrowCount,
    TexturePendingStorageGrowBytes,
    UploadOverflowCount,
    UploadSpillBytes,
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
    CaptureReadCount,
    CaptureValidLineCount,
    CaptureIndependentLineCount,
    CaptureLegacyOrderedLineCount,
    CaptureSidecarDispatchCount,
    CaptureSidecarBarrierCount,
    CaptureSidecarGpuTimeNs,
    RasterGpuTimeNs,
    StructuredCompositorGpuTimeNs,
    PresenterRenderPassGpuTimeNs,
    TotalQueueGpuSpanNs,
    NativeResolveCount,
    NativeReadbackCopyBytes,
    NativeReadbackDemandCount,
    NativeReadbackWaitNs,
    PresentedScreenCopyBytes,
    PresentedScreenCopyGpuTimeNs,
    DirectCompositorImageFrames,
    FallbackCompositorBufferFrames,
    HudUploadBytes,
    HudTextureRecreateCount,
    DX12VsyncEnabled,
    DX12PresentMode,
    DX12VendorPacingAuthority,
    DX12ReflexMode,
    DX12BackBufferCount,
    DX12PresenterLogicalDepth,
    Count,
};

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)

inline constexpr bool IsCompiledIn() noexcept
{
    return true;
}

inline constexpr std::array<const char*, static_cast<std::size_t>(CpuMetric::Count)> CpuMetricNames = {
    "raster_begin_wait",
    "raster_cpu_prepare_us",
    "raster_reuse_wait_us",
    "raster_record_submit_us",
    "texcache_update",
    "build_polygons",
    "soft2d_total_us",
    "structured2d_metadata_us",
    "texture_decode_us",
    "texture_pending_cpu_copy_us",
    "texture_resource_create_us",
    "texture_pending_storage_grow_us",
    "span_staging_copy",
    "descriptor_update",
    "structured_pack_us",
    "compose_record",
    "capture_wait",
    "capture_map_copy",
    "present_slot_wait_us",
    "present_begin_wait_us",
    "hud_patch_copy",
    "hud_upload",
    "present_record",
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
    if (IsEnabled())
        GetState().Counters[static_cast<std::size_t>(counter)] += value;
}

inline void SetCounter(Counter counter, u64 value) noexcept
{
    if (IsEnabled())
        GetState().Counters[static_cast<std::size_t>(counter)] = value;
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
        state.Cpu[static_cast<std::size_t>(CpuMetric::RasterReuseWait)].Add(
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
    AddDuration(CpuMetric::Soft2DTotal, state.Structured2DStart);
    state.Structured2DOpen = false;
}

class ScopedCpuTimer
{
public:
    explicit ScopedCpuTimer(CpuMetric metric, bool enabled = true) noexcept
        : Metric(metric), Enabled(enabled && IsEnabled())
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

    const auto reportClock = MelonPrimePerfClock::Now();
    std::fprintf(stderr,
        "[MelonPrimePerfPhase] report_qpc_ticks=%llu qpc_frequency=%llu\n",
        static_cast<unsigned long long>(reportClock.Ticks),
        static_cast<unsigned long long>(reportClock.Frequency));

    for (std::size_t index = 0; index < state.Cpu.size(); ++index)
    {
        const SampleWindow& window = state.Cpu[index];
        if (window.Count == 0)
            continue;
        std::fprintf(stderr,
            "[DX12Perf] cpu scale=%u name=%s p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f n=%zu\n",
            state.Scale, CpuMetricNames[index], Percentile(window, 0.50),
            Percentile(window, 0.95), Percentile(window, 0.99), window.Max, window.Count);
    }

    const auto count = [&](Counter counter) -> unsigned long long {
        return static_cast<unsigned long long>(state.Counters[static_cast<std::size_t>(counter)]);
    };
    std::fprintf(stderr,
        "[DX12Perf] counters scale=%u frames=%llu raster_wait_ns=%llu raster_wait_count=%llu "
        "raster_no_wait_count=%llu raster_timeout_count=%llu identical=%llu indirect_args_copy_B=%llu "
        "indirect_args_copy_count=%llu indirect_args_direct_writes=%llu polygons=%llu variants=%llu "
        "y_spans=%llu setup_indices=%llu span_upload_B=%llu structured_pack_B=%llu "
        "structured_input_packed_B=%llu structured_input_uploaded_B=%llu "
        "structured_input_regions=%llu structured_input_full=%llu structured_input_partial=%llu "
        "route_copy_B=%llu route_copy_ns=%llu regular_lines=%llu fallback_lines=%llu "
        "route_runs=%llu "
        "native_gpu2d_frames=%llu native_gpu2d_input_pack_B=%llu native_gpu2d_vram_B=%llu "
        "native_gpu2d_palette_B=%llu native_gpu2d_oam_B=%llu native_gpu2d_dispatches=%llu "
        "native_gpu2d_fallback_frames=%llu native_gpu2d_readback_B=%llu "
        "native_gpu2d_readbacks=%llu native_gpu2d_mismatches=%llu "
        "texture_upload_B=%llu texture_materialize_count=%llu "
        "texture_materialize_pre_fence_fail_count=%llu "
        "texture_materialize_retry_after_retire_count=%llu "
        "texture_materialize_retry_success_count=%llu "
        "texture_materialize_retry_fail_count=%llu "
        "texture_materialize_failure_reason=%llu "
        "texture_pending_upload_bytes=%llu texture_pending_upload_count=%llu "
        "texture_pending_storage_grow_count=%llu "
        "texture_pending_storage_grow_bytes=%llu "
        "upload_overflows=%llu spill_B=%llu descriptor_writes=%llu "
        "descriptor_creates=%llu descriptor_updates=%llu descriptor_copies=%llu "
        "descriptor_cpu_ns=%llu presenter_srv_creates=%llu "
        "presenter_descriptor_copies=%llu presenter_descriptor_cpu_ns=%llu "
        "presenter_descriptor_updates=%llu presenter_descriptor_cache_hits=%llu "
        "presenter_descriptor_cache_misses=%llu presenter_descriptor_cache_invalidates=%llu "
        "presenter_descriptor_fallbacks=%llu presenter_descriptor_persistent_creates=%llu "
        "compositor_descriptor_updates=%llu compose_drops=%llu capture_reads=%llu "
        "capture_valid_lines=%llu capture_independent_lines=%llu capture_legacy_lines=%llu "
        "capture_sidecar_dispatches=%llu capture_sidecar_barriers=%llu capture_sidecar_gpu_ns=%llu "
        "raster_gpu_ns=%llu structured_compositor_gpu_ns=%llu presenter_render_pass_gpu_ns=%llu "
        "total_queue_gpu_span_ns=%llu native_resolves=%llu native_readback_copy_B=%llu native_readback_demands=%llu "
        "native_readback_wait_ns=%llu screen_copy_B=%llu screen_copy_gpu_ns=%llu "
        "direct_image_frames=%llu fallback_buffer_frames=%llu hud_upload_B=%llu hud_recreates=%llu\n",
        state.Scale, count(Counter::Frames), count(Counter::RasterBeginWaitNs),
        count(Counter::RasterBeginWaitCount), count(Counter::RasterBeginNoWaitCount),
        count(Counter::RasterBeginFenceTimeoutCount), count(Counter::IdenticalFrames),
        count(Counter::DX12IndirectArgsCopyBytes), count(Counter::DX12IndirectArgsCopyCount),
        count(Counter::DX12IndirectArgsDirectWriteCount),
        count(Counter::Polygons), count(Counter::Variants), count(Counter::YSpans),
        count(Counter::SetupIndices), count(Counter::SpanUploadBytes),
        count(Counter::StructuredPackBytes),
        count(Counter::StructuredInputBytesPacked),
        count(Counter::StructuredInputBytesUploaded),
        count(Counter::StructuredInputCopyRegionCount),
        count(Counter::StructuredInputFullUploadCount),
        count(Counter::StructuredInputPartialUploadCount),
        count(Counter::StructuredScreenRouteCopyBytes),
        count(Counter::StructuredScreenRouteCopyNanoseconds),
        count(Counter::StructuredRegularLines), count(Counter::StructuredFallbackLines),
        count(Counter::StructuredRouteRuns), count(Counter::NativeGPU2DFrames),
        count(Counter::NativeGPU2DInputPackBytes), count(Counter::NativeGPU2DVRAMUploadBytes),
        count(Counter::NativeGPU2DPaletteUploadBytes), count(Counter::NativeGPU2DOAMUploadBytes),
        count(Counter::NativeGPU2DDispatchCount), count(Counter::NativeGPU2DFallbackFrames),
        count(Counter::NativeGPU2DReadbackBytes), count(Counter::NativeGPU2DReadbackCount),
        count(Counter::NativeGPU2DMismatchCount), count(Counter::TextureUploadBytes),
        count(Counter::TextureMaterializeCount),
        count(Counter::TextureMaterializePreFenceFailCount),
        count(Counter::TextureMaterializeRetryAfterRetireCount),
        count(Counter::TextureMaterializeRetrySuccessCount),
        count(Counter::TextureMaterializeRetryFailCount),
        count(Counter::TextureMaterializeFailureReason),
        count(Counter::TexturePendingUploadBytes),
        count(Counter::TexturePendingUploadCount),
        count(Counter::TexturePendingStorageGrowCount),
        count(Counter::TexturePendingStorageGrowBytes),
        count(Counter::UploadOverflowCount), count(Counter::UploadSpillBytes),
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
        count(Counter::CaptureReadCount),
        count(Counter::CaptureValidLineCount), count(Counter::CaptureIndependentLineCount),
        count(Counter::CaptureLegacyOrderedLineCount),
        count(Counter::CaptureSidecarDispatchCount), count(Counter::CaptureSidecarBarrierCount),
        count(Counter::CaptureSidecarGpuTimeNs), count(Counter::RasterGpuTimeNs),
        count(Counter::StructuredCompositorGpuTimeNs),
        count(Counter::PresenterRenderPassGpuTimeNs),
        count(Counter::TotalQueueGpuSpanNs), count(Counter::NativeResolveCount),
        count(Counter::NativeReadbackCopyBytes), count(Counter::NativeReadbackDemandCount),
        count(Counter::NativeReadbackWaitNs), count(Counter::PresentedScreenCopyBytes),
        count(Counter::PresentedScreenCopyGpuTimeNs),
        count(Counter::DirectCompositorImageFrames), count(Counter::FallbackCompositorBufferFrames),
        count(Counter::HudUploadBytes), count(Counter::HudTextureRecreateCount));

    const unsigned long long vsyncEnabled = count(Counter::DX12VsyncEnabled);
    const unsigned long long presentMode = count(Counter::DX12PresentMode);
    const unsigned long long pacingAuthority =
        count(Counter::DX12VendorPacingAuthority);
    const unsigned long long reflexMode = count(Counter::DX12ReflexMode);
    const unsigned long long backBufferCount = count(Counter::DX12BackBufferCount);
    const unsigned long long presenterLogicalDepth =
        count(Counter::DX12PresenterLogicalDepth);
    std::fprintf(stderr,
        "[DX12Perf] summary renderer=DX12 vsync=%llu present_mode=%llu "
        "vendor_pacing_authority=%llu reflex_mode=%llu "
        "swapchain_backbuffer_count=%llu presenter_logical_depth=%llu\n",
        vsyncEnabled, presentMode, pacingAuthority, reflexMode,
        backBufferCount, presenterLogicalDepth);

    for (SampleWindow& window : state.Cpu)
        window.Reset();
    state.Counters.fill(0);
    state.Counters[static_cast<std::size_t>(Counter::DX12VsyncEnabled)] = vsyncEnabled;
    state.Counters[static_cast<std::size_t>(Counter::DX12PresentMode)] = presentMode;
    state.Counters[static_cast<std::size_t>(Counter::DX12VendorPacingAuthority)] =
        pacingAuthority;
    state.Counters[static_cast<std::size_t>(Counter::DX12ReflexMode)] = reflexMode;
    state.Counters[static_cast<std::size_t>(Counter::DX12BackBufferCount)] = backBufferCount;
    state.Counters[static_cast<std::size_t>(Counter::DX12PresenterLogicalDepth)] =
        presenterLogicalDepth;
    state.LastReport = now;
}

#else

inline constexpr bool IsCompiledIn() noexcept
{
    return false;
}

inline constexpr bool IsEnabled() noexcept
{
    return false;
}

inline constexpr void SetScale(u32) noexcept {}
inline constexpr void AddCounter(Counter, u64 = 1) noexcept {}
inline constexpr void SetCounter(Counter, u64) noexcept {}
inline constexpr void RecordNativeReadbackWait(u64) noexcept {}
inline constexpr void RecordRasterBeginNoWait() noexcept {}
inline constexpr void RecordRasterBeginFenceTimeout() noexcept {}
inline constexpr void RecordGeometry(u32, u32, u32, u32) noexcept {}
inline constexpr void BeginStructured2DFrame() noexcept {}
inline constexpr void EndStructured2DFrame() noexcept {}
inline constexpr void MaybeReport() noexcept {}

template <typename TimePoint>
inline constexpr void AddDuration(CpuMetric, TimePoint) noexcept {}

class ScopedRasterBeginWait
{
public:
    constexpr explicit ScopedRasterBeginWait(bool = true) noexcept {}
};

class ScopedCpuTimer
{
public:
    constexpr explicit ScopedCpuTimer(CpuMetric, bool = true) noexcept {}
};

#endif

} // namespace melonDS::DX12Perf

#else

namespace melonDS::DX12Perf
{
enum class CpuMetric : u32 { RasterBeginWait, RasterCpuPrepare, RasterReuseWait, RasterRecordSubmit,
    TexcacheUpdate, BuildPolygons, Soft2DTotal, Structured2DMetadata, TextureDecode,
    TexturePendingCpuCopy, TextureResourceCreate, TexturePendingStorageGrow,
    SpanStagingCopy,
    DescriptorUpdate, ComposePack, ComposeRecord, CaptureWait, CaptureMapCopy,
    PresentSlotWait, PresentBeginWait, HudPatchCopy, HudUpload, PresentRecord, QueueSubmit, Count };
enum class Counter : u32 { Frames, RasterBeginWaitNs, RasterBeginWaitCount,
    RasterBeginNoWaitCount, RasterBeginFenceTimeoutCount, IdenticalFrames,
    DX12IndirectArgsCopyBytes, DX12IndirectArgsCopyCount, DX12IndirectArgsDirectWriteCount,
    Polygons, Variants, YSpans, SetupIndices,
    SpanUploadBytes, StructuredPackBytes, StructuredInputBytesPacked,
    StructuredInputBytesUploaded, StructuredInputCopyRegionCount,
    StructuredInputFullUploadCount, StructuredInputPartialUploadCount,
    StructuredScreenRouteCopyBytes,
    StructuredScreenRouteCopyNanoseconds, StructuredRegularLines, StructuredFallbackLines,
    StructuredRouteRuns, NativeGPU2DFrames, NativeGPU2DInputPackBytes,
    NativeGPU2DVRAMUploadBytes, NativeGPU2DPaletteUploadBytes, NativeGPU2DOAMUploadBytes,
    NativeGPU2DDispatchCount, NativeGPU2DFallbackFrames, NativeGPU2DReadbackBytes,
    NativeGPU2DReadbackCount, NativeGPU2DMismatchCount, TextureUploadBytes, TextureMaterializeCount,
    TextureMaterializePreFenceFailCount, TextureMaterializeRetryAfterRetireCount,
    TextureMaterializeRetrySuccessCount, TextureMaterializeRetryFailCount,
    TextureMaterializeFailureReason, TexturePendingUploadBytes, TexturePendingUploadCount,
    TexturePendingStorageGrowCount, TexturePendingStorageGrowBytes, UploadOverflowCount,
    UploadSpillBytes, DescriptorWriteCount,
    DescriptorCreateCount, DescriptorUpdateCount, DescriptorCopyCount, DescriptorCpuTimeNs,
    PresenterSrvCreateCount, PresenterDescriptorCopyCount, PresenterDescriptorCpuTimeNs,
    PresenterDescriptorUpdateCount, PresenterDescriptorCacheHitCount,
    PresenterDescriptorCacheMissCount, PresenterDescriptorCacheInvalidateCount,
    PresenterDescriptorFallbackCount, PresenterDescriptorPersistentCreateCount,
    CompositorDescriptorUpdateCount,
    CompositorDropCount, CaptureReadCount,
    CaptureValidLineCount, CaptureIndependentLineCount, CaptureLegacyOrderedLineCount,
    CaptureSidecarDispatchCount, CaptureSidecarBarrierCount, CaptureSidecarGpuTimeNs,
    RasterGpuTimeNs, StructuredCompositorGpuTimeNs, PresenterRenderPassGpuTimeNs,
    TotalQueueGpuSpanNs,
    NativeResolveCount,
    NativeReadbackCopyBytes, NativeReadbackDemandCount, NativeReadbackWaitNs,
    PresentedScreenCopyBytes, PresentedScreenCopyGpuTimeNs,
    DirectCompositorImageFrames, FallbackCompositorBufferFrames,
     HudUploadBytes, HudTextureRecreateCount, DX12VsyncEnabled, DX12PresentMode,
     DX12VendorPacingAuthority, DX12ReflexMode, DX12BackBufferCount,
     DX12PresenterLogicalDepth, Count };
inline constexpr bool IsCompiledIn() noexcept { return false; }
inline constexpr bool IsEnabled() noexcept { return false; }
inline constexpr void SetScale(u32) noexcept {}
inline constexpr void AddCounter(Counter, u64 = 1) noexcept {}
inline constexpr void SetCounter(Counter, u64) noexcept {}
inline constexpr void RecordNativeReadbackWait(u64) noexcept {}
class ScopedRasterBeginWait { public: constexpr explicit ScopedRasterBeginWait(bool = true) noexcept {} };
inline constexpr void RecordRasterBeginNoWait() noexcept {}
inline constexpr void RecordRasterBeginFenceTimeout() noexcept {}
inline constexpr void RecordGeometry(u32, u32, u32, u32) noexcept {}
inline constexpr void MaybeReport() noexcept {}
class ScopedCpuTimer { public: constexpr explicit ScopedCpuTimer(CpuMetric, bool = true) noexcept {} };
} // namespace melonDS::DX12Perf

#endif
#endif
