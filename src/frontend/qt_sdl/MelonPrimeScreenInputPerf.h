#ifndef MELONPRIME_SCREEN_INPUT_PERF_H
#define MELONPRIME_SCREEN_INPUT_PERF_H

// Developer-only GUI input-path telemetry for SCR-PERF-004/SCR-VALID-001.
// The collector is panel-owned, fixed-size, and runtime-gated by
// MELONPRIME_PERF=1. Shipping builds retain constexpr no-ops, so the normal
// Qt event path does not acquire a lock, allocate, or format a record.

#include <cstdint>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace MelonPrime {

enum class ScreenInputMetric : std::uint8_t
{
    HudEditFastRejected = 0,
    HudEditHelperEntered,
    UiSnapshotRead,
    StylusPointerPublish,
    Count
};

class ScreenInputPerf
{
public:
    using Tick = std::uint64_t;

    [[nodiscard]] Tick Now() const noexcept
    {
        return Enabled() ? SDL_GetPerformanceCounter() : 0;
    }

    void Count(ScreenInputMetric metric) noexcept
    {
        if (!Enabled())
            return;
        ++counters_[static_cast<std::size_t>(metric)];
    }

    void BeginMouseMove() noexcept
    {
        if (Enabled())
            ++mouseMoveEvents_;
    }

    void FinishMouseMove(Tick start, Tick end, int instanceId) noexcept
    {
        if (!Enabled())
            return;
        if (start && end > start) {
            ++eventSamples_;
            const Tick elapsed = end - start;
            if (!eventBucketWidthTicks_) {
                const Tick frequency = SDL_GetPerformanceFrequency();
                eventBucketWidthTicks_ = std::max<Tick>(
                    1, frequency / kTicksPerMicrosecondBase);
            }
            const std::size_t bucket = std::min<std::size_t>(
                static_cast<std::size_t>(elapsed / eventBucketWidthTicks_),
                kEventHistogramBucketCount - 1);
            ++eventHistogram_[bucket];
            if (bucket == kEventHistogramBucketCount - 1)
                ++eventHistogramSaturated_;
            eventMax_ = std::max(eventMax_, elapsed);
        }
        MaybeReport(instanceId, end ? end : SDL_GetPerformanceCounter());
    }

    class ScopedMouseMove final
    {
    public:
        ScopedMouseMove(ScreenInputPerf& owner, int instanceId) noexcept
            : owner_(owner), instanceId_(instanceId), start_(owner.Now())
        {
            owner_.BeginMouseMove();
        }

        ScopedMouseMove(const ScopedMouseMove&) = delete;
        ScopedMouseMove& operator=(const ScopedMouseMove&) = delete;

        ~ScopedMouseMove()
        {
            owner_.FinishMouseMove(start_, owner_.Now(), instanceId_);
        }

    private:
        ScreenInputPerf& owner_;
        int instanceId_;
        Tick start_;
    };

private:
    // One-microsecond buckets cover the normal GUI event-duration range with a
    // bounded 4 ms tail bucket. The event path performs one integer divide and
    // one fixed-array increment; the one-Hz report scans 4096 counters instead
    // of copying and sorting a 16,384-element sample ring.
    static constexpr std::size_t kEventHistogramBucketCount = 4096;
    static constexpr Tick kTicksPerMicrosecondBase = 1000000;
    static constexpr std::size_t kMetricCount =
        static_cast<std::size_t>(ScreenInputMetric::Count);

    static bool Enabled() noexcept
    {
        static const bool enabled = [] {
            const char* value = std::getenv("MELONPRIME_PERF");
            return value && value[0] == '1' && value[1] == '\0';
        }();
        return enabled;
    }

    template <std::size_t N>
    static std::size_t Append(
        std::array<char, N>& line,
        std::size_t used,
        const char* format,
        ...) noexcept
    {
        if (used >= N)
            return N;
        std::va_list args;
        va_start(args, format);
        const int written = std::vsnprintf(
            line.data() + used, N - used, format, args);
        va_end(args);
        if (written <= 0)
            return used;
        return std::min(N - 1, used + static_cast<std::size_t>(written));
    }

    template <std::size_t N>
    std::size_t AppendMetric(
        std::array<char, N>& line,
        std::size_t used,
        const char* name,
        ScreenInputMetric metric) const noexcept
    {
        return Append(line, used, "%s=%llu ", name,
            static_cast<unsigned long long>(
                counters_[static_cast<std::size_t>(metric)]));
    }

    void MaybeReport(int instanceId, Tick now) noexcept
    {
        if (!Enabled())
            return;
        const Tick frequency = SDL_GetPerformanceFrequency();
        if (!frequency)
            return;
        if (!lastReport_)
            lastReport_ = now;
        if (now < lastReport_ || now - lastReport_ < frequency)
            return;
        lastReport_ = now;

        const auto percentile = [&](double p) -> Tick {
            if (!eventSamples_)
                return 0;
            const std::uint64_t position = static_cast<std::uint64_t>(
                p * static_cast<double>(eventSamples_ - 1));
            std::uint64_t cumulative = 0;
            for (std::size_t bucket = 0;
                 bucket < kEventHistogramBucketCount; ++bucket) {
                const std::uint64_t next =
                    cumulative + eventHistogram_[bucket];
                if (position < next) {
                    // Return the midpoint of the fixed bucket. The final
                    // bucket is saturated, so use its lower bound there.
                    const Tick lower = static_cast<Tick>(bucket)
                        * eventBucketWidthTicks_;
                    if (bucket == kEventHistogramBucketCount - 1)
                        return lower;
                    return lower + eventBucketWidthTicks_ / 2;
                }
                cumulative = next;
            }
            return eventMax_;
        };
        const double toNs = 1000000000.0 / static_cast<double>(frequency);

        // Assemble one complete line before touching stderr so concurrent
        // emulation/renderer records cannot splice into this measurement.
        std::array<char, 1400> line{};
        std::size_t used = Append(line, 0,
            "[MelonPrimePerf] screen_input instance_id=%d "
            "mouseMoveEvents=%llu eventSamples=%llu "
            "eventDroppedOrOverwritten=%llu eventHistogramSaturated=%llu "
            "event_ns[n=%llu p50=%.1f p95=%.1f "
            "p99=%.1f max=%.1f] ",
            instanceId,
            static_cast<unsigned long long>(mouseMoveEvents_),
            static_cast<unsigned long long>(eventSamples_),
            static_cast<unsigned long long>(eventDroppedOrOverwritten_),
            static_cast<unsigned long long>(eventHistogramSaturated_),
            static_cast<unsigned long long>(eventSamples_),
            static_cast<double>(percentile(0.50)) * toNs,
            static_cast<double>(percentile(0.95)) * toNs,
            static_cast<double>(percentile(0.99)) * toNs,
            static_cast<double>(eventMax_) * toNs);
        used = AppendMetric(line, used, "hudEditFastRejected",
            ScreenInputMetric::HudEditFastRejected);
        used = AppendMetric(line, used, "hudEditHelperEntered",
            ScreenInputMetric::HudEditHelperEntered);
        used = AppendMetric(line, used, "uiSnapshotRead",
            ScreenInputMetric::UiSnapshotRead);
        used = AppendMetric(line, used, "stylusPointerPublish",
            ScreenInputMetric::StylusPointerPublish);
        used = Append(line, used, "\n");
        std::fwrite(line.data(), 1, used, stderr);

        mouseMoveEvents_ = 0;
        eventSamples_ = 0;
        eventDroppedOrOverwritten_ = 0;
        eventHistogramSaturated_ = 0;
        counters_.fill(0);
        eventHistogram_.fill(0);
        eventMax_ = 0;
    }

    Tick eventBucketWidthTicks_ = 0;
    std::array<std::uint64_t, kEventHistogramBucketCount> eventHistogram_{};
    Tick eventMax_ = 0;
    std::uint64_t mouseMoveEvents_ = 0;
    std::uint64_t eventSamples_ = 0;
    // The histogram is full-window and never overwrites a sample. Keep this
    // field in the report for compatibility with the prior ring collector;
    // it remains zero unless a future bounded collector drops a sample.
    std::uint64_t eventDroppedOrOverwritten_ = 0;
    std::uint64_t eventHistogramSaturated_ = 0;
    std::array<std::uint64_t, kMetricCount> counters_{};
    Tick lastReport_ = 0;
};

} // namespace MelonPrime

#else

namespace MelonPrime {

enum class ScreenInputMetric : std::uint8_t
{
    HudEditFastRejected = 0,
    HudEditHelperEntered,
    UiSnapshotRead,
    StylusPointerPublish,
    Count
};

class ScreenInputPerf
{
public:
    using Tick = std::uint64_t;

    [[nodiscard]] constexpr Tick Now() const noexcept { return 0; }
    constexpr void Count(ScreenInputMetric) const noexcept {}
    constexpr void BeginMouseMove() const noexcept {}
    constexpr void FinishMouseMove(Tick, Tick, int) const noexcept {}

    class ScopedMouseMove final
    {
    public:
        constexpr ScopedMouseMove(ScreenInputPerf&, int) noexcept {}
    };
};

} // namespace MelonPrime

#endif

#endif // MELONPRIME_SCREEN_INPUT_PERF_H
