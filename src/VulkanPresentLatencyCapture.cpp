/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
*/

#include "VulkanPresentLatencyCapture.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) \
    && defined(MELONPRIME_VULKAN_LATENCY_CAPTURE)

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "Platform.h"

namespace melonDS
{

namespace
{

// Both are read once at construction. A measurement run is configured before it
// starts, never while it is running.
constexpr const char* RunIdEnv = "MELONPRIME_LATENCY_RUN_ID";
constexpr const char* OutputEnv = "MELONPRIME_LATENCY_CSV";

std::string EnvOr(const char* name, const std::string& fallback)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) == 0 && value)
    {
        std::string result(value);
        std::free(value);
        if (!result.empty())
            return result;
    }
    return fallback;
#else
    const char* value = std::getenv(name);
    if (value && *value)
        return std::string(value);
    return fallback;
#endif
}

} // namespace

VulkanPresentLatencyCapture::VulkanPresentLatencyCapture()
{
    RunId = EnvOr(RunIdEnv, "unnamed-run");
    OutputPath = EnvOr(OutputEnv, "melonprime-latency-" + RunId + ".csv");

    // Reserve the whole ring now. Growing it mid-run would allocate on the
    // frame path, which is exactly what this instrument must not do.
    Samples.reserve(Capacity);
    StartTicks = static_cast<u64>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    Enabled = true;

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] latency capture armed: run=%s output=%s capacity=%zu\n",
        RunId.c_str(), OutputPath.c_str(), Capacity);
}

VulkanPresentLatencyCapture::~VulkanPresentLatencyCapture()
{
    // A run that ends by closing the window must still produce its CSV.
    Flush();
}

u64 VulkanPresentLatencyCapture::NowUs() const noexcept
{
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    const auto elapsed = Clock::duration(
        static_cast<Clock::rep>(static_cast<u64>(now.count()) - StartTicks));
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

void VulkanPresentLatencyCapture::MarkInputSample() noexcept
{
    // The first marker of a frame also starts a fresh sample: anything left
    // over from a frame that never presented must not leak into this one.
    const u64 index = NextSampleIndex;
    const int reflexMode = Pending.ReflexMode;
    Pending = VulkanLatencySample{};
    Pending.SampleIndex = index;
    Pending.ReflexMode = reflexMode;
    Pending.InputSampleUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkSimulationStart() noexcept
{
    Pending.SimStartUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkSimulationEnd() noexcept
{
    Pending.SimEndUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkRenderSubmitStart() noexcept
{
    Pending.RenderSubmitStartUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkRenderSubmitEnd() noexcept
{
    Pending.RenderSubmitEndUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkPresentStart() noexcept
{
    Pending.PresentStartUs = NowUs();
}

void VulkanPresentLatencyCapture::MarkPresentEnd() noexcept
{
    Pending.PresentEndUs = NowUs();
}

void VulkanPresentLatencyCapture::SetGpuRenderBounds(u64 startUs, u64 endUs) noexcept
{
    Pending.GpuRenderStartUs = startUs;
    Pending.GpuRenderEndUs = endUs;
}

void VulkanPresentLatencyCapture::Commit(
    u64 presentId, const VulkanPresentPacer::StateSnapshot& pacing) noexcept
{
    if (Samples.size() >= Capacity)
        return; // Full: drop rather than allocate or overwrite measured data.

    Pending.PresentId = presentId;
    Pending.Pacing = pacing;
    Samples.push_back(Pending);
    ++NextSampleIndex;
}

bool VulkanPresentLatencyCapture::Flush()
{
    if (Samples.empty())
        return true;

    std::FILE* file = std::fopen(OutputPath.c_str(), "wb");
    if (!file)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] latency capture could not open %s for writing; "
            "%zu samples were lost\n",
            OutputPath.c_str(), Samples.size());
        Samples.clear();
        return false;
    }

    std::fprintf(file,
        "run_id,sample_index,present_id,"
        "input_sample_time_us,sim_start_time_us,sim_end_time_us,"
        "render_submit_start_time_us,render_submit_end_time_us,"
        "present_start_time_us,present_end_time_us,"
        "gpu_render_start_time_us,gpu_render_end_time_us,"
        "policy,authority,reflex_mode,target_scheduling,bounded_wait,"
        "present_mode,fallback_reason,"
        "target_time_ns,feedback_present_id,feedback_stage_time_ns,"
        "baseline_sequence,present_sequence,frame_interval_ns,"
        "wait_timeout_count,timing_queue_size,timing_queue_full_count,"
        "timing_queue_recovery_count\n");

    for (const VulkanLatencySample& sample : Samples)
    {
        const VulkanPresentPacer::StateSnapshot& p = sample.Pacing;
        std::fprintf(file,
            "%s,%llu,%llu,"
            "%llu,%llu,%llu,"
            "%llu,%llu,"
            "%llu,%llu,"
            "%llu,%llu,"
            "%d,%d,%d,%d,%d,"
            "%d,%d,"
            "%llu,%llu,%llu,"
            "%llu,%llu,%llu,"
            "%u,%u,%u,%u\n",
            RunId.c_str(),
            static_cast<unsigned long long>(sample.SampleIndex),
            static_cast<unsigned long long>(sample.PresentId),
            static_cast<unsigned long long>(sample.InputSampleUs),
            static_cast<unsigned long long>(sample.SimStartUs),
            static_cast<unsigned long long>(sample.SimEndUs),
            static_cast<unsigned long long>(sample.RenderSubmitStartUs),
            static_cast<unsigned long long>(sample.RenderSubmitEndUs),
            static_cast<unsigned long long>(sample.PresentStartUs),
            static_cast<unsigned long long>(sample.PresentEndUs),
            static_cast<unsigned long long>(sample.GpuRenderStartUs),
            static_cast<unsigned long long>(sample.GpuRenderEndUs),
            p.Policy,
            p.Authority,
            sample.ReflexMode,
            p.TargetTimeScheduling ? 1 : 0,
            p.BoundedPresentWait ? 1 : 0,
            p.PresentMode,
            p.FallbackReason,
            static_cast<unsigned long long>(p.TargetTimeNs),
            static_cast<unsigned long long>(p.FeedbackPresentId),
            static_cast<unsigned long long>(p.FeedbackStageTimeNs),
            static_cast<unsigned long long>(p.BaselineSequence),
            static_cast<unsigned long long>(p.PresentSequence),
            static_cast<unsigned long long>(p.FrameIntervalNs),
            p.WaitTimeouts,
            p.TimingQueueSize,
            p.TimingQueueFullCount,
            p.TimingQueueRecoveries);
    }

    std::fclose(file);
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] latency capture wrote %zu samples to %s\n",
        Samples.size(), OutputPath.c_str());
    Samples.clear();
    return true;
}

} // namespace melonDS

#endif
