/*
    Copyright 2016-2026 melonDS team

    Shared helper for optional D3D12 timestamp-query telemetry.
*/

#ifndef MELONPRIME_DX12_GPU_TIMESTAMP_H
#define MELONPRIME_DX12_GPU_TIMESTAMP_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Context.h"
#include "DX12Perf.h"
#include "GpuStageMetrics.h"

namespace melonDS
{

inline void RecordDX12GpuMetric(
    const DX12CommandContext& commands, GpuMetric metric,
    DX12Perf::Counter counter) noexcept
{
    if (!DX12Perf::IsEnabled() || !commands.HasTimestampQueries())
        return;
    // DX12CommandContext caches the complete readback snapshot for this
    // retired submission, so recording several metrics does not Map/Unmap the
    // readback resource once per metric.
    const u64 nanoseconds = commands.ReadTimestampSpanNanoseconds(
        GpuMetricQueryIndex(metric, false), GpuMetricQueryIndex(metric, true));
    if (nanoseconds != 0)
        DX12Perf::AddCounter(counter, nanoseconds);
}

} // namespace melonDS

#endif

#endif // MELONPRIME_DX12_GPU_TIMESTAMP_H
