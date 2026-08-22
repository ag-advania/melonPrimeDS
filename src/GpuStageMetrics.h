/*
    Copyright 2016-2026 melonDS team

    Stable stage names shared by the optional Vulkan and DX12 GPU timestamp
    telemetry. The indices are intentionally backend-neutral so reports from
    both renderers can be compared without translating a private query layout.
*/

#ifndef MELONPRIME_GPU_STAGE_METRICS_H
#define MELONPRIME_GPU_STAGE_METRICS_H

#include "types.h"

namespace melonDS
{

enum class GpuMetric : u32
{
    Raster = 0,
    NativeGPU2DRaw,
    NativeGPU2DLogical,
    NativeGPU2DCapture,
    NativeGPU2DResolve,
    CaptureSidecar,
    StructuredCompositor,
    PresenterRenderPass,
    TotalQueueSpan,
    Count,
};

constexpr u32 GpuMetricQueryIndex(GpuMetric metric, bool end) noexcept
{
    return static_cast<u32>(metric) * 2u + (end ? 1u : 0u);
}

// Every metric owns a start/end query pair. Keep the backend query pools in
// lockstep with this enum so adding a stage cannot silently truncate later
// metrics or make their query indices overlap an unrelated backend field.
constexpr u32 GpuMetricQueryCount = static_cast<u32>(GpuMetric::Count) * 2u;

} // namespace melonDS

#endif // MELONPRIME_GPU_STAGE_METRICS_H
