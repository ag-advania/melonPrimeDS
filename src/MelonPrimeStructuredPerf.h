/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_STRUCTURED_PERF_H
#define MELONPRIME_STRUCTURED_PERF_H

#include "types.h"

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
#include "DX12Perf.h"
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include "VulkanPerf.h"
#endif

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
#include <chrono>
#endif

namespace melonDS
{

// The software 2D producer is shared by the explicit DX12/Vulkan renderers.
// The renderer publishes this once per frame so hot pixel paths never need a
// dynamic_cast or a backend branch that depends on the current pixel.
enum class StructuredPerfBackend : u8
{
    None = 0,
    DX12,
    Vulkan,
};

enum class StructuredPerfMetric : u8
{
    Soft2DTotal = 0,
    Structured2DMetadata,
};

inline void BeginStructured2DPerfFrame(StructuredPerfBackend backend) noexcept
{
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    switch (backend)
    {
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    case StructuredPerfBackend::DX12:
        DX12Perf::BeginStructured2DFrame();
        break;
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    case StructuredPerfBackend::Vulkan:
        VulkanPerf::BeginStructured2DFrame();
        break;
#endif
    default:
        break;
    }
#else
    (void)backend;
#endif
}

inline void EndStructured2DPerfFrame(StructuredPerfBackend backend) noexcept
{
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    switch (backend)
    {
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    case StructuredPerfBackend::DX12:
        DX12Perf::EndStructured2DFrame();
        break;
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    case StructuredPerfBackend::Vulkan:
        VulkanPerf::EndStructured2DFrame();
        break;
#endif
    default:
        break;
    }
#else
    (void)backend;
#endif
}

class ScopedStructuredPerfTimer
{
public:
    ScopedStructuredPerfTimer(
        StructuredPerfBackend backend,
        StructuredPerfMetric metric,
        bool enabled = true) noexcept
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        : Backend(backend), Metric(metric), Enabled(enabled && BackendEnabled(backend))
#endif
    {
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        if (Enabled)
            Start = std::chrono::steady_clock::now();
#else
        (void)backend;
        (void)metric;
        (void)enabled;
#endif
    }

    ~ScopedStructuredPerfTimer()
    {
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        if (!Enabled)
            return;

        switch (Backend)
        {
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
        case StructuredPerfBackend::DX12:
            DX12Perf::AddDuration(ToDX12Metric(Metric), Start);
            break;
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        case StructuredPerfBackend::Vulkan:
            VulkanPerf::AddDuration(ToVulkanMetric(Metric), Start);
            break;
#endif
        default:
            break;
        }
#endif
    }

    ScopedStructuredPerfTimer(const ScopedStructuredPerfTimer&) = delete;
    ScopedStructuredPerfTimer& operator=(const ScopedStructuredPerfTimer&) = delete;

private:
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    static bool BackendEnabled(StructuredPerfBackend backend) noexcept
    {
        switch (backend)
        {
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
        case StructuredPerfBackend::DX12:
            return DX12Perf::IsEnabled();
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        case StructuredPerfBackend::Vulkan:
            return VulkanPerf::IsEnabled();
#endif
        default:
            return false;
        }
    }

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    static DX12Perf::CpuMetric ToDX12Metric(StructuredPerfMetric metric) noexcept
    {
        return metric == StructuredPerfMetric::Soft2DTotal
            ? DX12Perf::CpuMetric::Soft2DTotal
            : DX12Perf::CpuMetric::Structured2DMetadata;
    }
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    static VulkanPerf::CpuMetric ToVulkanMetric(StructuredPerfMetric metric) noexcept
    {
        return metric == StructuredPerfMetric::Soft2DTotal
            ? VulkanPerf::CpuMetric::Soft2DTotal
            : VulkanPerf::CpuMetric::Structured2DMetadata;
    }
#endif

    StructuredPerfBackend Backend = StructuredPerfBackend::None;
    StructuredPerfMetric Metric = StructuredPerfMetric::Structured2DMetadata;
    bool Enabled = false;
    std::chrono::steady_clock::time_point Start{};
#endif
};

} // namespace melonDS

#endif // MELONPRIME_STRUCTURED_PERF_H
