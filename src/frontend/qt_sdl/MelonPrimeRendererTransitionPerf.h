#ifndef MELONPRIME_RENDERER_TRANSITION_PERF_H
#define MELONPRIME_RENDERER_TRANSITION_PERF_H

// Cold-path renderer transition telemetry for SCR-LOCK-002.  The process-wide
// registry is intentionally measured without changing the steady frame path:
// only a renderer transition samples registry-lock wait, presenter Quiesce,
// and total transition time.

#include <cstdint>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace MelonPrime {

enum class RendererTransitionMetric : std::uint8_t
{
    RegistryLockWait = 0,
    QuiesceDuration,
    TransitionTotal,
    Count
};

class RendererTransitionPerf
{
public:
    using Tick = std::uint64_t;

    [[nodiscard]] Tick Now() const noexcept
    {
        return Enabled() ? SDL_GetPerformanceCounter() : 0;
    }

    void Record(RendererTransitionMetric metric, Tick start, Tick end) noexcept
    {
        if (!Enabled() || !start || end <= start)
            return;
        const std::size_t index = static_cast<std::size_t>(metric);
        const Tick elapsed = end - start;
        std::lock_guard<std::mutex> lock(mutex_);
        samples_[index][write_[index]] = elapsed;
        write_[index] = (write_[index] + 1) % kCapacity;
        if (count_[index] < kCapacity)
            ++count_[index];
        max_[index] = std::max(max_[index], elapsed);
    }

    void Report(int instanceId, const char* backend) noexcept
    {
        if (!Enabled())
            return;
        const Tick frequency = SDL_GetPerformanceFrequency();
        if (!frequency)
            return;
        std::array<char, 1200> line{};
        std::size_t used = Append(line, 0,
            "[MelonPrimePerf] renderer_transition backend=%s instance_id=%d ",
            backend ? backend : "unknown", instanceId);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            used = ReportOne(line, used, "registry_lock_wait",
                RendererTransitionMetric::RegistryLockWait, frequency);
            used = ReportOne(line, used, "quiesce_duration",
                RendererTransitionMetric::QuiesceDuration, frequency);
            used = ReportOne(line, used, "transition_total",
                RendererTransitionMetric::TransitionTotal, frequency);
            count_.fill(0);
            max_.fill(0);
        }
        used = Append(line, used, "\n");
        std::fwrite(line.data(), 1, used, stderr);
    }

private:
    static constexpr std::size_t kMetricCount =
        static_cast<std::size_t>(RendererTransitionMetric::Count);
    static constexpr std::size_t kCapacity = 64;

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
    std::size_t ReportOne(
        std::array<char, N>& line,
        std::size_t used,
        const char* name,
        RendererTransitionMetric metric,
        Tick frequency) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(metric);
        const std::size_t count = count_[index];
        std::array<Tick, kCapacity> sorted{};
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t source =
                (write_[index] + kCapacity - count + i) % kCapacity;
            sorted[i] = samples_[index][source];
        }
        std::sort(sorted.begin(), sorted.begin() + count);
        const auto percentile = [&](double p) -> Tick {
            if (!count)
                return 0;
            const std::size_t position = static_cast<std::size_t>(
                p * static_cast<double>(count - 1));
            return sorted[position];
        };
        const double toUs = 1000000.0 / static_cast<double>(frequency);
        return Append(line, used,
            "%s[n=%llu p50=%.1f p95=%.1f p99=%.1f max=%.1f] ",
            name, static_cast<unsigned long long>(count),
            static_cast<double>(percentile(0.50)) * toUs,
            static_cast<double>(percentile(0.95)) * toUs,
            static_cast<double>(percentile(0.99)) * toUs,
            static_cast<double>(max_[index]) * toUs);
    }

    mutable std::mutex mutex_;
    std::array<std::array<Tick, kCapacity>, kMetricCount> samples_{};
    std::array<std::size_t, kMetricCount> write_{};
    std::array<std::size_t, kMetricCount> count_{};
    std::array<Tick, kMetricCount> max_{};
};

inline RendererTransitionPerf g_rendererTransitionPerf;

} // namespace MelonPrime

#else

namespace MelonPrime {

enum class RendererTransitionMetric : std::uint8_t
{
    RegistryLockWait = 0,
    QuiesceDuration,
    TransitionTotal,
    Count
};

class RendererTransitionPerf
{
public:
    using Tick = std::uint64_t;
    [[nodiscard]] constexpr Tick Now() const noexcept { return 0; }
    constexpr void Record(RendererTransitionMetric, Tick, Tick) const noexcept {}
    constexpr void Report(int, const char*) const noexcept {}
};

inline RendererTransitionPerf g_rendererTransitionPerf;

} // namespace MelonPrime

#endif

#endif // MELONPRIME_RENDERER_TRANSITION_PERF_H
