#include "MelonPrimeFirstVulkanFrameTrace.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace MelonPrime::FirstVulkanFrameTrace
{

namespace
{

constexpr std::size_t kRingCapacity = 256;

std::atomic<Phase> g_phase {Phase::Boot};
std::atomic<std::uint64_t> g_sequence {0};

struct RingSlot
{
    std::atomic<std::uint64_t> sequence {0};
    std::uint32_t threadId = 0;
    std::uint32_t eventId = 0;
    std::uint32_t line = 0;
    std::uint32_t vcount = 0;
    std::uint64_t cpuTimestamp = 0;
    char marker[96] {};
};

RingSlot g_ring[kRingCapacity];
std::atomic<std::uint64_t> g_ringWriteIndex {0};

void emitFormatted(bool ignoreBudget, const char* fmt, va_list args) noexcept
{
    if (!ignoreBudget && !budgetActive())
        return;

    vprintf(fmt, args);
    fflush(stdout);
}

void pushRing(
    std::uint32_t eventId,
    std::uint32_t line,
    std::uint32_t vcount,
    const char* marker) noexcept
{
    if (marker == nullptr)
        marker = "";

    const std::uint64_t seq = g_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t slotIndex = g_ringWriteIndex.fetch_add(1, std::memory_order_relaxed);
    RingSlot& slot = g_ring[slotIndex % kRingCapacity];

    slot.threadId =
#ifdef _WIN32
        GetCurrentThreadId();
#else
        0u;
#endif
    slot.eventId = eventId;
    slot.line = line;
    slot.vcount = vcount;
    slot.cpuTimestamp = seq;
    std::snprintf(slot.marker, sizeof(slot.marker), "%s", marker);
    slot.sequence.store(seq, std::memory_order_release);
}

} // namespace

bool budgetActive() noexcept
{
    return g_phase.load(std::memory_order_acquire) != Phase::Disabled;
}

Phase currentPhase() noexcept
{
    return g_phase.load(std::memory_order_acquire);
}

void log(const char* fmt, ...) noexcept
{
    if (!budgetActive())
        return;

    va_list args;
    va_start(args, fmt);
    emitFormatted(false, fmt, args);
    va_end(args);
}

void rawLog(const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    emitFormatted(true, fmt, args);
    va_end(args);
}

void consumeBudget() noexcept
{
    g_phase.store(Phase::Disabled, std::memory_order_release);
}

void record(
    std::uint32_t eventId,
    std::uint32_t line,
    std::uint32_t vcount,
    const char* marker) noexcept
{
    if (!budgetActive())
        return;

    pushRing(eventId, line, vcount, marker);
}

void recordEvent(
    std::uint32_t eventId,
    std::uint32_t param,
    const void* that,
    const char* phaseMarker) noexcept
{
    if (!budgetActive())
        return;

    char marker[96];
    std::snprintf(
        marker,
        sizeof(marker),
        "%s id=%u param=%u that=%p",
        phaseMarker != nullptr ? phaseMarker : "event",
        eventId,
        param,
        that);
    pushRing(eventId, 0, 0, marker);
}

void dumpRing(FILE* out) noexcept
{
    if (out == nullptr)
        return;

    const std::uint64_t endSeq = g_sequence.load(std::memory_order_acquire);
    const std::uint64_t startSeq = endSeq > kRingCapacity ? endSeq - kRingCapacity + 1 : 1;

    std::fprintf(out, "traceRing.capacity=%zu traceRing.endSequence=%llu\n",
        kRingCapacity, static_cast<unsigned long long>(endSeq));

    for (std::uint64_t seq = startSeq; seq <= endSeq; ++seq)
    {
        for (const RingSlot& slot : g_ring)
        {
            if (slot.sequence.load(std::memory_order_acquire) != seq)
                continue;

            std::fprintf(
                out,
                "traceRing seq=%llu threadId=%u eventId=%u line=%u vcount=%u marker=%s\n",
                static_cast<unsigned long long>(seq),
                slot.threadId,
                slot.eventId,
                slot.line,
                slot.vcount,
                slot.marker);
            break;
        }
    }
}

void dumpRingToCrashReport(const char* reportPath) noexcept
{
    if (reportPath == nullptr)
        return;

    FILE* out = std::fopen(reportPath, "a");
    if (out == nullptr)
        return;

    dumpRing(out);
    std::fclose(out);
}

} // namespace MelonPrime::FirstVulkanFrameTrace
