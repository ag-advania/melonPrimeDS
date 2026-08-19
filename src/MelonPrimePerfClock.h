/*
    Copyright 2016-2026 melonDS team

    Shared monotonic clock used by renderer measurement artifacts.

    Windows' QueryPerformanceCounter is also the clock exposed by
    PowerShell's Stopwatch, so physical A/B manifests and application-side
    samples can be selected against one exact tick domain. Other platforms use
    a nanosecond steady-clock domain and record its frequency explicitly.
*/

#ifndef MELONPRIME_PERF_CLOCK_H
#define MELONPRIME_PERF_CLOCK_H

#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace melonDS::MelonPrimePerfClock
{

struct Stamp
{
    std::uint64_t Ticks = 0;
    std::uint64_t Frequency = 0;
};

inline std::uint64_t Frequency() noexcept
{
#if defined(_WIN32)
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0
            ? static_cast<std::uint64_t>(value.QuadPart)
            : 1'000'000'000ULL;
    }();
    return frequency;
#else
    return 1'000'000'000ULL;
#endif
}

inline std::uint64_t Ticks() noexcept
{
#if defined(_WIN32)
    LARGE_INTEGER value{};
    if (QueryPerformanceCounter(&value))
        return static_cast<std::uint64_t>(value.QuadPart);
    return 0;
#else
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

inline Stamp Now() noexcept
{
    return {Ticks(), Frequency()};
}

inline std::uint64_t ElapsedUs(Stamp start, Stamp end) noexcept
{
    if (end.Ticks < start.Ticks || start.Frequency == 0)
        return 0;
    const std::uint64_t delta = end.Ticks - start.Ticks;
    return static_cast<std::uint64_t>(
        static_cast<long double>(delta) * 1'000'000.0L /
        static_cast<long double>(start.Frequency));
}

} // namespace melonDS::MelonPrimePerfClock

#endif // MELONPRIME_PERF_CLOCK_H
