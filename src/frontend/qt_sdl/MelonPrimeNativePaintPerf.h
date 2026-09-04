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

    void RecordScoreboardRasterComposite(
        std::uint64_t bytes, std::uint64_t pixels) noexcept
    {
        if (!Enabled() || (!bytes && !pixels))
            return;
        ++scoreboardRasterCompositeCalls_;
        scoreboardRasterCompositeBytes_ += bytes;
        scoreboardRasterCompositePixels_ += pixels;
    }

    void RecordScoreboardRasterDirectDraw(std::uint64_t pixels) noexcept
    {
        if (!Enabled() || !pixels)
            return;
        ++scoreboardRasterDirectDraws_;
        scoreboardRasterDirectPixels_ += pixels;
    }

    void RecordScoreboardDirtyCellPixels(std::uint64_t pixels) noexcept
    {
        if (!Enabled() || !pixels)
            return;
        scoreboardDirtyCellPixels_ += pixels;
    }

    void RecordHudOverlayComposite(
        std::uint64_t bytes, std::uint64_t pixels) noexcept
    {
        if (!Enabled() || (!bytes && !pixels))
            return;
        ++hudOverlayCompositeCalls_;
        hudOverlayCompositeBytes_ += bytes;
        hudOverlayCompositePixels_ += pixels;
    }

    void MaybeReport(int instanceId) noexcept
    {
        if (!Enabled())
            return;
        const Tick now = SDL_GetPerformanceCounter();
        const Tick frequency = SDL_GetPerformanceFrequency();
        if (!lastReport_)
            lastReport_ = now;
        const Tick reportTicks = now - lastReport_;
        if (!frequency || reportTicks < frequency)
            return;
        lastReport_ = now;

        // Build the complete record before touching stderr. Renderer and emu
        // telemetry are emitted from other threads; several fprintf calls
        // let their records splice into this one and make both unparsable.
        std::array<char, 2048> line{};
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
        const std::size_t frameCount = count_[static_cast<std::size_t>(
            NativePaintMetric::HudSoftware)];
        const double reportSeconds = static_cast<double>(reportTicks)
            / static_cast<double>(frequency);
        const double copyMbps = reportSeconds > 0.0
            ? static_cast<double>(hudOverlayCompositeBytes_)
                / reportSeconds / 1000000.0 : 0.0;
        const double scoreboardCompositeMbps = reportSeconds > 0.0
            ? static_cast<double>(scoreboardRasterCompositeBytes_)
                / reportSeconds / 1000000.0 : 0.0;
        const double scoreboardCompositePixelsPerFrame = frameCount
            ? static_cast<double>(scoreboardRasterCompositePixels_)
                / static_cast<double>(frameCount) : 0.0;
        const double scoreboardCompositeCallsPerFrame = frameCount
            ? static_cast<double>(scoreboardRasterCompositeCalls_)
                / static_cast<double>(frameCount) : 0.0;
        const double scoreboardDirectPixelsPerFrame = frameCount
            ? static_cast<double>(scoreboardRasterDirectPixels_)
                / static_cast<double>(frameCount) : 0.0;
        const double scoreboardDirtyCellPixelsPerFrame = frameCount
            ? static_cast<double>(scoreboardDirtyCellPixels_)
                / static_cast<double>(frameCount) : 0.0;
        const double pixelsPerFrame = frameCount
            ? static_cast<double>(hudOverlayCompositePixels_)
                / static_cast<double>(frameCount) : 0.0;
        const double callsPerFrame = frameCount
            ? static_cast<double>(hudOverlayCompositeCalls_)
                / static_cast<double>(frameCount) : 0.0;
        used = Append(line, used,
            "hud_overlay_composite_calls=%llu "
            "hud_overlay_composite_bytes=%llu "
            "hud_overlay_composite_pixels=%llu "
            "hud_overlay_composite_MBps=%.1f "
            "hud_overlay_composite_pixels_per_frame=%.1f "
            "hud_overlay_composite_calls_per_frame=%.2f ",
            static_cast<unsigned long long>(hudOverlayCompositeCalls_),
            static_cast<unsigned long long>(hudOverlayCompositeBytes_),
            static_cast<unsigned long long>(hudOverlayCompositePixels_),
            copyMbps, pixelsPerFrame, callsPerFrame);
        used = Append(line, used,
            "scoreboard_raster_composite_calls=%llu "
            "scoreboard_raster_composite_bytes=%llu "
            "scoreboard_raster_composite_pixels=%llu "
            "scoreboard_raster_composite_MBps=%.1f "
            "scoreboard_raster_composite_pixels_per_frame=%.1f "
            "scoreboard_raster_composite_calls_per_frame=%.2f "
            "scoreboard_raster_direct_draws=%llu "
            "scoreboard_raster_direct_pixels=%llu "
            "scoreboard_raster_direct_pixels_per_frame=%.1f "
            "scoreboard_dirty_cell_pixels=%llu "
            "scoreboard_dirty_cell_pixels_per_frame=%.1f ",
            static_cast<unsigned long long>(scoreboardRasterCompositeCalls_),
            static_cast<unsigned long long>(scoreboardRasterCompositeBytes_),
            static_cast<unsigned long long>(scoreboardRasterCompositePixels_),
            scoreboardCompositeMbps, scoreboardCompositePixelsPerFrame,
            scoreboardCompositeCallsPerFrame,
            static_cast<unsigned long long>(scoreboardRasterDirectDraws_),
            static_cast<unsigned long long>(scoreboardRasterDirectPixels_),
            scoreboardDirectPixelsPerFrame,
            static_cast<unsigned long long>(scoreboardDirtyCellPixels_),
            scoreboardDirtyCellPixelsPerFrame);
        used = Append(line, used, "\n");
        std::fwrite(line.data(), 1, used, stderr);

        count_.fill(0);
        max_.fill(0);
        hudOverlayCompositeCalls_ = 0;
        hudOverlayCompositeBytes_ = 0;
        hudOverlayCompositePixels_ = 0;
        scoreboardRasterCompositeCalls_ = 0;
        scoreboardRasterCompositeBytes_ = 0;
        scoreboardRasterCompositePixels_ = 0;
        scoreboardRasterDirectDraws_ = 0;
        scoreboardRasterDirectPixels_ = 0;
        scoreboardDirtyCellPixels_ = 0;
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
    std::uint64_t hudOverlayCompositeBytes_ = 0;
    std::uint64_t hudOverlayCompositePixels_ = 0;
    std::uint64_t hudOverlayCompositeCalls_ = 0;
    std::uint64_t scoreboardRasterCompositeBytes_ = 0;
    std::uint64_t scoreboardRasterCompositePixels_ = 0;
    std::uint64_t scoreboardRasterCompositeCalls_ = 0;
    std::uint64_t scoreboardRasterDirectPixels_ = 0;
    std::uint64_t scoreboardRasterDirectDraws_ = 0;
    std::uint64_t scoreboardDirtyCellPixels_ = 0;
    Tick lastReport_ = 0;
};

#else

class NativePaintPerf
{
public:
    using Tick = std::uint64_t;
    [[nodiscard]] constexpr Tick Now() const noexcept { return 0; }
    constexpr void Record(NativePaintMetric, Tick, Tick) const noexcept {}
    constexpr void RecordScoreboardRasterComposite(
        std::uint64_t, std::uint64_t) const noexcept {}
    constexpr void RecordScoreboardRasterDirectDraw(
        std::uint64_t) const noexcept {}
    constexpr void RecordScoreboardDirtyCellPixels(
        std::uint64_t) const noexcept {}
    constexpr void RecordHudOverlayComposite(std::uint64_t, std::uint64_t) const noexcept {}
    constexpr void MaybeReport(int) const noexcept {}
};

#endif

} // namespace MelonPrime

#endif // MELONPRIME_NATIVE_PAINT_PERF_H
