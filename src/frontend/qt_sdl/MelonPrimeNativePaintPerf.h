#ifndef MELONPRIME_NATIVE_PAINT_PERF_H
#define MELONPRIME_NATIVE_PAINT_PERF_H

// Developer-only Software presenter measurement for SCR-LOCK-001.
// Runtime gate: MELONPRIME_PERF=1. Shipping builds retain only inline no-ops.

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

enum class NativePaintMetric : std::uint8_t
{
    RenderLockWait = 0,
    RenderLockHold,
    FramebufferCopy,
    QPaintGame,
    HudSoftware,
    Count
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

class NativePaintPerf
{
public:
    using Tick = std::uint64_t;

    [[nodiscard]] Tick Now() const noexcept
    {
        return Enabled() ? SDL_GetPerformanceCounter() : 0;
    }

    void Record(NativePaintMetric metric, Tick start, Tick end) noexcept
    {
        if (!start || end <= start)
            return;
        const std::size_t index = static_cast<std::size_t>(metric);
        const Tick elapsed = end - start;
        samples_[index][write_[index]] = elapsed;
        write_[index] = (write_[index] + 1) % kCapacity;
        if (count_[index] < kCapacity)
            ++count_[index];
        max_[index] = std::max(max_[index], elapsed);
    }

    void MaybeReport(int instanceId) noexcept
    {
        if (!Enabled())
            return;
        const Tick now = SDL_GetPerformanceCounter();
        const Tick frequency = SDL_GetPerformanceFrequency();
        if (!lastReport_)
            lastReport_ = now;
        if (!frequency || now - lastReport_ < frequency)
            return;
        lastReport_ = now;

        // Build the complete record before touching stderr. Renderer and emu
        // telemetry are emitted from other threads; several fprintf calls
        // let their records splice into this one and make both unparsable.
        std::array<char, 1024> line{};
        std::size_t used = Append(line, 0,
            "[MelonPrimePerf] native_paint_us instance_id=%d ", instanceId);
        used = ReportOne(line, used, "render_lock_wait",
            NativePaintMetric::RenderLockWait, frequency);
        used = ReportOne(line, used, "render_lock_hold",
            NativePaintMetric::RenderLockHold, frequency);
        used = ReportOne(line, used, "framebuffer_copy",
            NativePaintMetric::FramebufferCopy, frequency);
        used = ReportOne(line, used, "qpaint_game",
            NativePaintMetric::QPaintGame, frequency);
        used = ReportOne(line, used, "hud_software",
            NativePaintMetric::HudSoftware, frequency);
        used = Append(line, used, "\n");
        std::fwrite(line.data(), 1, used, stderr);

        count_.fill(0);
        max_.fill(0);
    }

private:
    static constexpr std::size_t kMetricCount =
        static_cast<std::size_t>(NativePaintMetric::Count);
    static constexpr std::size_t kCapacity = 2048;

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
        NativePaintMetric metric,
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
            name,
            static_cast<unsigned long long>(count),
            static_cast<double>(percentile(0.50)) * toUs,
            static_cast<double>(percentile(0.95)) * toUs,
            static_cast<double>(percentile(0.99)) * toUs,
            static_cast<double>(max_[index]) * toUs);
    }

    std::array<std::array<Tick, kCapacity>, kMetricCount> samples_{};
    std::array<std::size_t, kMetricCount> write_{};
    std::array<std::size_t, kMetricCount> count_{};
    std::array<Tick, kMetricCount> max_{};
    Tick lastReport_ = 0;
};

#else

class NativePaintPerf
{
public:
    using Tick = std::uint64_t;
    [[nodiscard]] constexpr Tick Now() const noexcept { return 0; }
    constexpr void Record(NativePaintMetric, Tick, Tick) const noexcept {}
    constexpr void MaybeReport(int) const noexcept {}
};

#endif

} // namespace MelonPrime

#endif // MELONPRIME_NATIVE_PAINT_PERF_H
