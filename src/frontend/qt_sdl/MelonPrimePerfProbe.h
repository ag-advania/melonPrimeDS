#ifndef MELONPRIME_PERF_PROBE_H
#define MELONPRIME_PERF_PROBE_H

// V5 Phase 0: frame-time + hot-path counters for measured optimization.
// Compile gate: MELONPRIME_ENABLE_DEVELOPER_FEATURES + MELONPRIME_DS
// Runtime gate: MELONPRIME_PERF=1
// Release builds (developer features off): zero symbols, zero hot-path cost.

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MelonPrimePerf {

enum class InputSource : uint8_t {
    WinRaw = 0,
    MacRaw,
    LinuxRaw,
    PanelDelta,
    QCursorFallback,
    Count
};

enum class Section : uint8_t {
    LimiterSleep = 0,
    LimiterSpin,
    Input,
    RunFrame,
    Draw,
    DeferredDrain,
    Count
};

inline bool IsEnabled()
{
    static const bool kEnabled = [] {
        const char* v = std::getenv("MELONPRIME_PERF");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return kEnabled;
}

struct State {
    bool frameOpen = false;
    Uint64 freq = 0;
    Uint64 frameStartTick = 0;
    Uint64 sectionStartTick = 0;
    bool sectionOpen = false;
    Section openSection = Section::LimiterSleep;

    static constexpr uint32_t kRingCap = 8192;
    static constexpr uint32_t kHistBuckets = 64;
    static constexpr double kHistBucketMs = 0.5;

    double frameMsRing[kRingCap]{};
    uint32_t ringWrite = 0;
    uint32_t ringCount = 0;

    double windowFrameMs[120]{};
    uint32_t windowFrameCount = 0;

    double secSumMs[static_cast<uint32_t>(Section::Count)]{};
    double secMaxMs[static_cast<uint32_t>(Section::Count)]{};
    uint64_t secSamples[static_cast<uint32_t>(Section::Count)]{};

    uint64_t cntInputSource[static_cast<uint32_t>(InputSource::Count)]{};
    uint64_t cntWarp = 0;
    uint64_t cntOutOfGamePatch = 0;
    uint64_t cntOsdColorApply = 0;
    uint64_t cntOsdColorWrite = 0;
    uint64_t sumHudDirtyArea = 0;
    uint64_t sumGlUploadBytes = 0;
    uint64_t cntDr3HashSkip = 0;
    uint64_t sumCustomHudTicks = 0;
    uint64_t cntCustomHudFrames = 0;

    Uint64 lastReportTick = 0;
    uint32_t histTotal[kHistBuckets]{};
    uint32_t histOverflow = 0;
};

inline State& S()
{
    static State s;
    return s;
}

inline bool IsFrameActive()
{
    return S().frameOpen;
}

inline Uint64 ReadTicksIfActive()
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)
    return IsEnabled() ? SDL_GetPerformanceCounter() : 0;
#else
    return 0;
#endif
}

inline double TicksToMs(Uint64 ticks)
{
    const State& st = S();
    if (!st.freq)
        return 0.0;
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(st.freq);
}

inline void RecordFrameMs(double frameMs)
{
    State& st = S();
    st.frameMsRing[st.ringWrite] = frameMs;
    st.ringWrite = (st.ringWrite + 1) % State::kRingCap;
    if (st.ringCount < State::kRingCap)
        ++st.ringCount;

    if (st.windowFrameCount < 120)
        st.windowFrameMs[st.windowFrameCount++] = frameMs;

    const int bucket = static_cast<int>(frameMs / State::kHistBucketMs);
    if (bucket >= 0 && bucket < static_cast<int>(State::kHistBuckets))
        ++st.histTotal[bucket];
    else if (frameMs >= 0.0)
        ++st.histOverflow;
}

inline double PercentileSorted(const double* data, uint32_t count, double p)
{
    if (count == 0)
        return 0.0;
    const double idx = p * static_cast<double>(count - 1);
    const uint32_t lo = static_cast<uint32_t>(idx);
    const uint32_t hi = lo + 1 < count ? lo + 1 : lo;
    const double frac = idx - static_cast<double>(lo);
    return data[lo] * (1.0 - frac) + data[hi] * frac;
}

inline void ResetWindowStats()
{
    State& st = S();
    st.windowFrameCount = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(Section::Count); ++i) {
        st.secSumMs[i] = 0.0;
        st.secMaxMs[i] = 0.0;
        st.secSamples[i] = 0;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(InputSource::Count); ++i)
        st.cntInputSource[i] = 0;
    st.cntWarp = 0;
    st.cntOutOfGamePatch = 0;
    st.cntOsdColorApply = 0;
    st.cntOsdColorWrite = 0;
    st.sumHudDirtyArea = 0;
    st.sumGlUploadBytes = 0;
    st.cntDr3HashSkip = 0;
    st.sumCustomHudTicks = 0;
    st.cntCustomHudFrames = 0;
}

inline void MaybeReport1Hz()
{
    State& st = S();
    if (!st.freq)
        return;

    const Uint64 now = SDL_GetPerformanceCounter();
    if (!st.lastReportTick)
        st.lastReportTick = now;

    const double sinceReportMs = TicksToMs(now - st.lastReportTick);
    if (sinceReportMs < 1000.0)
        return;

    double sorted[120];
    const uint32_t n = st.windowFrameCount;
    for (uint32_t i = 0; i < n; ++i)
        sorted[i] = st.windowFrameMs[i];
    std::sort(sorted, sorted + n);

    const double p50 = PercentileSorted(sorted, n, 0.50);
    const double p95 = PercentileSorted(sorted, n, 0.95);
    const double p99 = PercentileSorted(sorted, n, 0.99);
    double frameMax = 0.0;
    for (uint32_t i = 0; i < n; ++i)
        if (sorted[i] > frameMax)
            frameMax = sorted[i];

    const auto secAvg = [&](Section sec) -> double {
        const uint32_t idx = static_cast<uint32_t>(sec);
        return st.secSamples[idx] ? st.secSumMs[idx] / static_cast<double>(st.secSamples[idx]) : 0.0;
    };

    const uint64_t inputTotal =
        st.cntInputSource[static_cast<uint32_t>(InputSource::WinRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::MacRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::LinuxRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::PanelDelta)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::QCursorFallback)];

    fprintf(stderr,
        "[MelonPrimePerf] frame_ms p50=%.3f p95=%.3f p99=%.3f max=%.3f n=%u | "
        "sec_avg_ms sleep=%.3f spin=%.3f input=%.3f run=%.3f draw=%.3f drain=%.3f | "
        "input_src raw=%llu mac=%llu linux=%llu panel=%llu qcur=%llu (tot=%llu) | "
        "warp=%llu oog_patch=%llu osd_apply=%llu osd_write=%llu | "
        "hud_dirty_px=%llu gl_up_B=%llu dr3_skip=%llu hud_render_us=%.1f\n",
        p50, p95, p99, frameMax, n,
        secAvg(Section::LimiterSleep), secAvg(Section::LimiterSpin),
        secAvg(Section::Input), secAvg(Section::RunFrame),
        secAvg(Section::Draw), secAvg(Section::DeferredDrain),
        static_cast<unsigned long long>(st.cntInputSource[static_cast<uint32_t>(InputSource::WinRaw)]),
        static_cast<unsigned long long>(st.cntInputSource[static_cast<uint32_t>(InputSource::MacRaw)]),
        static_cast<unsigned long long>(st.cntInputSource[static_cast<uint32_t>(InputSource::LinuxRaw)]),
        static_cast<unsigned long long>(st.cntInputSource[static_cast<uint32_t>(InputSource::PanelDelta)]),
        static_cast<unsigned long long>(st.cntInputSource[static_cast<uint32_t>(InputSource::QCursorFallback)]),
        static_cast<unsigned long long>(inputTotal),
        static_cast<unsigned long long>(st.cntWarp),
        static_cast<unsigned long long>(st.cntOutOfGamePatch),
        static_cast<unsigned long long>(st.cntOsdColorApply),
        static_cast<unsigned long long>(st.cntOsdColorWrite),
        static_cast<unsigned long long>(st.sumHudDirtyArea),
        static_cast<unsigned long long>(st.sumGlUploadBytes),
        static_cast<unsigned long long>(st.cntDr3HashSkip),
        st.cntCustomHudFrames
            ? TicksToMs(st.sumCustomHudTicks) * 1000.0 / static_cast<double>(st.cntCustomHudFrames)
            : 0.0);

    st.lastReportTick = now;
    ResetWindowStats();
}

inline void FrameBegin()
{
    if (!IsEnabled())
        return;

    State& st = S();
    if (!st.freq)
        st.freq = SDL_GetPerformanceFrequency();

    st.frameOpen = true;
    st.frameStartTick = SDL_GetPerformanceCounter();
    st.sectionOpen = false;
}

inline void SectionBegin(Section sec)
{
    if (!S().frameOpen)
        return;

    State& st = S();
    st.sectionOpen = true;
    st.openSection = sec;
    st.sectionStartTick = SDL_GetPerformanceCounter();
}

inline void SectionEnd(Section sec)
{
    State& st = S();
    if (!st.frameOpen || !st.sectionOpen || st.openSection != sec)
        return;

    const Uint64 endTick = SDL_GetPerformanceCounter();
    const double ms = TicksToMs(endTick - st.sectionStartTick);
    const uint32_t idx = static_cast<uint32_t>(sec);
    st.secSumMs[idx] += ms;
    if (ms > st.secMaxMs[idx])
        st.secMaxMs[idx] = ms;
    ++st.secSamples[idx];
    st.sectionOpen = false;
}

inline void FrameEnd()
{
    State& st = S();
    if (!st.frameOpen)
        return;

    const Uint64 endTick = SDL_GetPerformanceCounter();
    RecordFrameMs(TicksToMs(endTick - st.frameStartTick));
    st.frameOpen = false;
    MaybeReport1Hz();
}

struct ScopedSection {
    Section sec;
    explicit ScopedSection(Section s) : sec(s) { SectionBegin(s); }
    ~ScopedSection() { SectionEnd(sec); }
};

inline void CountInputSource(InputSource src)
{
    if (!S().frameOpen)
        return;
    ++S().cntInputSource[static_cast<uint32_t>(src)];
}

inline void CountWarp()
{
    if (!S().frameOpen)
        return;
    ++S().cntWarp;
}

inline void CountOutOfGamePatchApply()
{
    if (!S().frameOpen)
        return;
    ++S().cntOutOfGamePatch;
}

inline void CountOsdColorApply()
{
    if (!S().frameOpen)
        return;
    ++S().cntOsdColorApply;
}

inline void CountOsdColorWrite()
{
    if (!S().frameOpen)
        return;
    ++S().cntOsdColorWrite;
}

inline void AddHudDirtyArea(int pixels)
{
    if (!S().frameOpen || pixels <= 0)
        return;
    S().sumHudDirtyArea += static_cast<uint64_t>(pixels);
}

inline void AddGlUploadBytes(uint64_t bytes)
{
    if (!S().frameOpen || bytes == 0)
        return;
    S().sumGlUploadBytes += bytes;
}

inline void CountDr3HashSkip()
{
    if (!S().frameOpen)
        return;
    ++S().cntDr3HashSkip;
}

inline void AddCustomHudRenderTicks(Uint64 ticks)
{
    if (!S().frameOpen)
        return;
    S().sumCustomHudTicks += ticks;
    ++S().cntCustomHudFrames;
}

inline void ShutdownReport()
{
    if (!IsEnabled())
        return;

    State& st = S();
    if (st.ringCount == 0) {
        fprintf(stderr, "[MelonPrimePerf] shutdown: no frames recorded\n");
        return;
    }

    double sorted[State::kRingCap];
    const uint32_t n = st.ringCount;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t idx = (st.ringWrite + State::kRingCap - n + i) % State::kRingCap;
        sorted[i] = st.frameMsRing[idx];
    }
    std::sort(sorted, sorted + n);

    const double p50 = PercentileSorted(sorted, n, 0.50);
    const double p95 = PercentileSorted(sorted, n, 0.95);
    const double p99 = PercentileSorted(sorted, n, 0.99);
    const double maxMs = sorted[n - 1];

    fprintf(stderr,
        "[MelonPrimePerf] shutdown summary: frames=%u frame_ms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
        n, p50, p95, p99, maxMs);

    fprintf(stderr, "[MelonPrimePerf] histogram bucket_ms=%.1f:\n", State::kHistBucketMs);
    for (uint32_t b = 0; b < State::kHistBuckets; ++b) {
        if (st.histTotal[b])
            fprintf(stderr, "  %4.1f-%4.1f ms: %u\n",
                b * State::kHistBucketMs,
                (b + 1) * State::kHistBucketMs,
                st.histTotal[b]);
    }
    if (st.histOverflow)
        fprintf(stderr, "  >=%.1f ms: %u\n",
            State::kHistBuckets * State::kHistBucketMs,
            st.histOverflow);
}

} // namespace MelonPrimePerf

#else // !MELONPRIME_ENABLE_DEVELOPER_FEATURES || !MELONPRIME_DS

namespace MelonPrimePerf {

enum class InputSource : uint8_t {
    WinRaw,
    MacRaw,
    LinuxRaw,
    PanelDelta,
    QCursorFallback,
    Count
};

enum class Section : uint8_t {
    LimiterSleep,
    LimiterSpin,
    Input,
    RunFrame,
    Draw,
    DeferredDrain,
    Count
};

inline bool IsEnabled() { return false; }
inline bool IsFrameActive() { return false; }
inline unsigned long long ReadTicksIfActive() { return 0; }
inline void FrameBegin() {}
inline void FrameEnd() {}
inline void SectionBegin(Section) {}
inline void SectionEnd(Section) {}
inline void CountInputSource(InputSource) {}
inline void CountWarp() {}
inline void CountOutOfGamePatchApply() {}
inline void CountOsdColorApply() {}
inline void CountOsdColorWrite() {}
inline void AddHudDirtyArea(int) {}
inline void AddGlUploadBytes(uint64_t) {}
inline void CountDr3HashSkip() {}
inline void AddCustomHudRenderTicks(unsigned long long) {}
inline void ShutdownReport() {}

struct ScopedSection {
    explicit ScopedSection(Section) {}
    ~ScopedSection() = default;
};

} // namespace MelonPrimePerf

#endif

#endif // MELONPRIME_PERF_PROBE_H
