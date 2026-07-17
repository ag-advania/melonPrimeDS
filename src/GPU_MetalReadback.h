// MelonPrimeDS - explicit Metal readback reasons / counters (PR-6)
//
// Normal Full-GPU frames must keep normalReadbackBytes == 0.
// Explicit reasons (CPU VRAM read, savestate, screenshot, …) are allowed
// and counted separately.

#ifndef GPU_METAL_READBACK_H
#define GPU_METAL_READBACK_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace melonDS
{

enum class MetalReadbackReason : uint32_t
{
    CpuVramRead = 0,
    Savestate,
    Screenshot,
    VideoCapture,
    RendererSwitch,
    DebugComparison,
    // Soft/CPU compositor still needs GetLine() until PR-7 Full-GPU-only.
    // Counted as "normal" so progress toward 0 is measurable.
    SoftCompositorGetLine,
    Count
};

inline const char* MetalReadbackReasonName(MetalReadbackReason reason) noexcept
{
    switch (reason)
    {
    case MetalReadbackReason::CpuVramRead: return "CpuVramRead";
    case MetalReadbackReason::Savestate: return "Savestate";
    case MetalReadbackReason::Screenshot: return "Screenshot";
    case MetalReadbackReason::VideoCapture: return "VideoCapture";
    case MetalReadbackReason::RendererSwitch: return "RendererSwitch";
    case MetalReadbackReason::DebugComparison: return "DebugComparison";
    case MetalReadbackReason::SoftCompositorGetLine: return "SoftCompositorGetLine";
    default: return "Unknown";
    }
}

struct MetalReadbackCounters
{
    std::atomic<uint64_t> NormalBytes{0};
    std::atomic<uint64_t> ExplicitBytes{
        0}; // sum of all explicit reasons
    std::atomic<uint64_t> ExplicitByReason[
        static_cast<size_t>(MetalReadbackReason::Count)]{};
    std::atomic<uint64_t> Frames{0};
};

inline MetalReadbackCounters& MetalReadbackStats() noexcept
{
    static MetalReadbackCounters stats;
    return stats;
}

inline void MetalRecordNormalReadbackBytes(uint64_t bytes) noexcept
{
    if (bytes == 0)
        return;
    MetalReadbackStats().NormalBytes.fetch_add(
        bytes, std::memory_order_relaxed);
}

inline void MetalRecordExplicitReadbackBytes(
    MetalReadbackReason reason,
    uint64_t bytes) noexcept
{
    if (bytes == 0)
        return;
    auto& stats = MetalReadbackStats();
    stats.ExplicitBytes.fetch_add(bytes, std::memory_order_relaxed);
    const size_t index = static_cast<size_t>(reason);
    if (index < static_cast<size_t>(MetalReadbackReason::Count))
    {
        stats.ExplicitByReason[index].fetch_add(
            bytes, std::memory_order_relaxed);
    }
}

inline void MetalReadbackStatsNoteFrame() noexcept
{
    auto& stats = MetalReadbackStats();
    const uint64_t frames =
        stats.Frames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (frames % 600u != 0u)
        return;

    std::fprintf(stderr,
        "[MelonPrime] metal readback: frames=%llu normalBytes=%llu "
        "explicitBytes=%llu softGetLine=%llu cpuVram=%llu\n",
        static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(
            stats.NormalBytes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            stats.ExplicitBytes.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            stats.ExplicitByReason[static_cast<size_t>(
                MetalReadbackReason::SoftCompositorGetLine)]
                .load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            stats.ExplicitByReason[static_cast<size_t>(
                MetalReadbackReason::CpuVramRead)]
                .load(std::memory_order_relaxed)));
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_READBACK_H
