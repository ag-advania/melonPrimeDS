#pragma once

// First-frame Vulkan/GPU trace budget (S77-1, S80-1/5).
// State lives in MelonPrimeFirstVulkanFrameTrace.cpp (not inline statics in headers).

#include <cstdint>
#include <cstdio>

namespace MelonPrime::FirstVulkanFrameTrace
{

enum class Phase : std::uint8_t
{
    Disabled = 0,
    Boot,
    FirstRunFrame,
};

bool budgetActive() noexcept;
Phase currentPhase() noexcept;

void log(const char* fmt, ...) noexcept;
void rawLog(const char* fmt, ...) noexcept;
void consumeBudget() noexcept;

struct TraceRecord
{
    std::uint64_t sequence;
    std::uint32_t threadId;
    std::uint32_t eventId;
    std::uint32_t line;
    std::uint32_t vcount;
    std::uint64_t cpuTimestamp;
    const char* marker;
};

void record(std::uint32_t eventId, std::uint32_t line, std::uint32_t vcount, const char* marker) noexcept;
void recordEvent(
    std::uint32_t eventId,
    std::uint32_t param,
    const void* that,
    const char* phaseMarker) noexcept;
void dumpRing(FILE* out) noexcept;
void dumpRingToCrashReport(const char* reportPath) noexcept;

} // namespace MelonPrime::FirstVulkanFrameTrace
