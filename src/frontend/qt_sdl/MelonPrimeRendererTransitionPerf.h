#ifndef MELONPRIME_RENDERER_TRANSITION_PERF_H
#define MELONPRIME_RENDERER_TRANSITION_PERF_H

// Cold-path renderer transition telemetry for SCR-LOCK-002. Each transition
// owns one fixed POD sample, so concurrent instances and backends cannot mix
// measurements or race a process-wide reset. Formatting is still restricted
// to the transition-completion path; the steady frame path never sees this.

#include <cstdint>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#endif

namespace MelonPrime {

struct RendererTransitionSample
{
    using Tick = std::uint64_t;

    Tick registryLockWait = 0;
    Tick quiesceDuration = 0;
    Tick transitionTotal = 0;
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

class RendererTransitionPerf
{
public:
    using Tick = RendererTransitionSample::Tick;

    [[nodiscard]] static Tick Now() noexcept
    {
        return Enabled() ? SDL_GetPerformanceCounter() : 0;
    }

    static void Report(
        int instanceId,
        const char* backend,
        const RendererTransitionSample& sample) noexcept
    {
        if (!Enabled())
            return;
        const Tick frequency = SDL_GetPerformanceFrequency();
        if (!frequency)
            return;

        std::array<char, 1000> line{};
        std::size_t used = Append(line, 0,
            "[MelonPrimePerf] renderer_transition backend=%s instance_id=%d ",
            backend ? backend : "unknown", instanceId);
        used = AppendMetric(line, used, "registry_lock_wait",
            sample.registryLockWait, frequency);
        used = AppendMetric(line, used, "quiesce_duration",
            sample.quiesceDuration, frequency);
        used = AppendMetric(line, used, "transition_total",
            sample.transitionTotal, frequency);
        used = Append(line, used, "\n");
        std::fwrite(line.data(), 1, used, stderr);
    }

private:
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
    static std::size_t AppendMetric(
        std::array<char, N>& line,
        std::size_t used,
        const char* name,
        Tick sample,
        Tick frequency) noexcept
    {
        const int count = sample ? 1 : 0;
        const double toUs = 1000000.0 / static_cast<double>(frequency);
        const double valueUs = static_cast<double>(sample) * toUs;
        return Append(line, used,
            "%s[n=%d p50=%.1f p95=%.1f p99=%.1f max=%.1f] ",
            name, count, valueUs, valueUs, valueUs, valueUs);
    }
};

#else

class RendererTransitionPerf
{
public:
    using Tick = RendererTransitionSample::Tick;

    [[nodiscard]] static constexpr Tick Now() noexcept { return 0; }
    static constexpr void Report(
        int, const char*, const RendererTransitionSample&) noexcept {}
};

#endif

} // namespace MelonPrime

#endif // MELONPRIME_RENDERER_TRANSITION_PERF_H
