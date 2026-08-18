#ifndef MELONPRIME_PERF_PROBE_H
#define MELONPRIME_PERF_PROBE_H

// Frame-time, hot-path, and Custom HUD phase counters for measured optimization.
// Compile gate: MELONPRIME_ENABLE_DEVELOPER_FEATURES + MELONPRIME_DS
// Runtime gate: MELONPRIME_PERF=1
// Release builds (developer features off): zero symbols, zero hot-path cost.

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstddef>
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

enum class HudPhase : uint8_t {
    State = 0,
    ScoreboardPlan,
    ScoreboardRaster,
    QPainter,
    Clear,
    Hash,
    UploadPrepare,
    GpuUpload,
    Composite,
    TotalActive,
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
    Uint64 inputSampleTick = 0;
    bool inputSampleOpen = false;
    bool runFrameBeginRecorded = false;
    bool presentEndRecorded = false;

    static constexpr uint32_t kRingCap = 8192;
    static constexpr uint32_t kHistBuckets = 64;
    static constexpr double kHistBucketMs = 0.5;

    double frameMsRing[kRingCap]{};
    uint32_t ringWrite = 0;
    uint32_t ringCount = 0;

    static constexpr uint32_t kFrameWindowCap = 512;
    double windowFrameMs[kFrameWindowCap]{};
    uint32_t windowFrameCount = 0;

    static constexpr uint32_t kLatencyCap = 512;
    double inputToRunFrameUs[kLatencyCap]{};
    double inputToPresentEndUs[kLatencyCap]{};
    uint32_t inputToRunFrameCount = 0;
    uint32_t inputToPresentEndCount = 0;

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
    static constexpr uint32_t kHudPhaseCap = 512;
    Uint64 hudPhaseTicks[static_cast<uint32_t>(HudPhase::Count)][kHudPhaseCap]{};
    uint32_t hudPhaseCount[static_cast<uint32_t>(HudPhase::Count)]{};
    uint64_t hudPhaseCalls[static_cast<uint32_t>(HudPhase::Count)]{};
    Uint64 hudPhaseSumTicks[static_cast<uint32_t>(HudPhase::Count)]{};
    Uint64 hudPhaseMaxTicks[static_cast<uint32_t>(HudPhase::Count)]{};
    Uint64 currentHudPhaseTicks = 0;
    bool currentHudDrawn = false;
    uint64_t cntCustomHudCalls = 0;
    uint64_t cntCustomHudDrawn = 0;
    uint64_t cntHudVisualRenders = 0;
    uint64_t cntHudVisualReuses = 0;
    uint64_t cntHudVisualIdentityProbes = 0;
    uint64_t cntHudVisualStampChecks = 0;
    uint64_t cntHudVisualStampCommits = 0;
    uint64_t cntScoreboardPlanBuilds = 0;
    uint64_t cntScoreboardFullPlanRebuilds = 0;
    uint64_t cntScoreboardDynamicCellUpdates = 0;
    uint64_t cntScoreboardTimeVisualChanges = 0;
    uint64_t cntScoreboardStructureChecks = 0;
    uint64_t cntScoreboardOutlinePathHits = 0;
    uint64_t cntScoreboardOutlinePathMisses = 0;
    uint64_t cntHudRegionHashCalls = 0;
    uint64_t sumHudRegionHashBytes = 0;
    uint64_t cntHudUploadCalls = 0;

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

    if (st.windowFrameCount < State::kFrameWindowCap)
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

inline double LatencyPercentile(const double* samples, uint32_t count, double p)
{
    if (count == 0)
        return 0.0;
    double sorted[State::kLatencyCap];
    for (uint32_t i = 0; i < count; ++i)
        sorted[i] = samples[i];
    std::sort(sorted, sorted + count);
    return PercentileSorted(sorted, count, p);
}

inline double LatencyMax(const double* samples, uint32_t count)
{
    double max = 0.0;
    for (uint32_t i = 0; i < count; ++i)
        if (samples[i] > max)
            max = samples[i];
    return max;
}

inline void RecordLatencySample(
    double* samples, uint32_t& count, double microseconds)
{
    if (count < State::kLatencyCap)
        samples[count++] = microseconds;
}

inline void ResetWindowStats()
{
    State& st = S();
    st.windowFrameCount = 0;
    st.inputToRunFrameCount = 0;
    st.inputToPresentEndCount = 0;
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
    std::memset(st.hudPhaseTicks, 0, sizeof(st.hudPhaseTicks));
    std::memset(st.hudPhaseCount, 0, sizeof(st.hudPhaseCount));
    std::memset(st.hudPhaseCalls, 0, sizeof(st.hudPhaseCalls));
    std::memset(st.hudPhaseSumTicks, 0, sizeof(st.hudPhaseSumTicks));
    std::memset(st.hudPhaseMaxTicks, 0, sizeof(st.hudPhaseMaxTicks));
    st.cntCustomHudCalls = 0;
    st.cntCustomHudDrawn = 0;
    st.cntHudVisualRenders = 0;
    st.cntHudVisualReuses = 0;
    st.cntHudVisualIdentityProbes = 0;
    st.cntHudVisualStampChecks = 0;
    st.cntHudVisualStampCommits = 0;
    st.cntScoreboardPlanBuilds = 0;
    st.cntScoreboardFullPlanRebuilds = 0;
    st.cntScoreboardDynamicCellUpdates = 0;
    st.cntScoreboardTimeVisualChanges = 0;
    st.cntScoreboardStructureChecks = 0;
    st.cntScoreboardOutlinePathHits = 0;
    st.cntScoreboardOutlinePathMisses = 0;
    st.cntHudRegionHashCalls = 0;
    st.sumHudRegionHashBytes = 0;
    st.cntHudUploadCalls = 0;
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

    double sorted[State::kFrameWindowCap];
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

    fprintf(stderr,
        "[MelonPrimePerf] explicit_latency_us "
        "frame_input_sample_to_runframe_begin_us="
        "p50=%.1f p95=%.1f p99=%.1f max=%.1f n=%u "
        "input_sample_to_present_end_us="
        "p50=%.1f p95=%.1f p99=%.1f max=%.1f n=%u\n",
        LatencyPercentile(st.inputToRunFrameUs, st.inputToRunFrameCount, 0.50),
        LatencyPercentile(st.inputToRunFrameUs, st.inputToRunFrameCount, 0.95),
        LatencyPercentile(st.inputToRunFrameUs, st.inputToRunFrameCount, 0.99),
        LatencyMax(st.inputToRunFrameUs, st.inputToRunFrameCount),
        st.inputToRunFrameCount,
        LatencyPercentile(st.inputToPresentEndUs, st.inputToPresentEndCount, 0.50),
        LatencyPercentile(st.inputToPresentEndUs, st.inputToPresentEndCount, 0.95),
        LatencyPercentile(st.inputToPresentEndUs, st.inputToPresentEndCount, 0.99),
        LatencyMax(st.inputToPresentEndUs, st.inputToPresentEndCount),
        st.inputToPresentEndCount);

    const auto hudPercentileUs = [&](HudPhase phase, double percentile) -> double {
        const uint32_t index = static_cast<uint32_t>(phase);
        const uint32_t count = st.hudPhaseCount[index];
        if (!count)
            return 0.0;
        double samples[State::kHudPhaseCap];
        for (uint32_t i = 0; i < count; ++i)
            samples[i] = TicksToMs(st.hudPhaseTicks[index][i]) * 1000.0;
        std::sort(samples, samples + count);
        return PercentileSorted(samples, count, percentile);
    };
    const auto hudAverageUs = [&](HudPhase phase) -> double {
        const uint32_t index = static_cast<uint32_t>(phase);
        return st.hudPhaseCalls[index]
            ? TicksToMs(st.hudPhaseSumTicks[index]) * 1000.0
                / static_cast<double>(st.hudPhaseCalls[index])
            : 0.0;
    };
    const auto hudSumUs = [&](HudPhase phase) -> double {
        return TicksToMs(st.hudPhaseSumTicks[static_cast<uint32_t>(phase)]) * 1000.0;
    };
    const auto hudMaxUs = [&](HudPhase phase) -> double {
        return TicksToMs(st.hudPhaseMaxTicks[static_cast<uint32_t>(phase)]) * 1000.0;
    };
    fprintf(stderr,
        "[MelonPrimePerf] hud_phase_us "
        "state[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "scoreboard_plan[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "scoreboard_raster[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "painter_other[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "clear[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "hash[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "upload_prepare[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "gpu_upload[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "composite[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "total_active[c=%llu sum=%.1f avg=%.1f p50=%.1f p95=%.1f max=%.1f] "
        "calls=%llu drawn=%llu "
        "visual_render=%llu visual_reuse=%llu identity_probes=%llu "
        "stamp_checks=%llu stamp_commits=%llu plan_build=%llu full_rebuild=%llu "
        "structure_checks=%llu dynamic_cells=%llu time_changes=%llu "
        "outline_hit=%llu outline_miss=%llu hash_calls=%llu hash_B=%llu uploads=%llu\n",
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::State)]),
        hudSumUs(HudPhase::State), hudAverageUs(HudPhase::State),
        hudPercentileUs(HudPhase::State, 0.50), hudPercentileUs(HudPhase::State, 0.95),
        hudMaxUs(HudPhase::State),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::ScoreboardPlan)]),
        hudSumUs(HudPhase::ScoreboardPlan), hudAverageUs(HudPhase::ScoreboardPlan),
        hudPercentileUs(HudPhase::ScoreboardPlan, 0.50),
        hudPercentileUs(HudPhase::ScoreboardPlan, 0.95), hudMaxUs(HudPhase::ScoreboardPlan),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::ScoreboardRaster)]),
        hudSumUs(HudPhase::ScoreboardRaster), hudAverageUs(HudPhase::ScoreboardRaster),
        hudPercentileUs(HudPhase::ScoreboardRaster, 0.50),
        hudPercentileUs(HudPhase::ScoreboardRaster, 0.95), hudMaxUs(HudPhase::ScoreboardRaster),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::QPainter)]),
        hudSumUs(HudPhase::QPainter), hudAverageUs(HudPhase::QPainter),
        hudPercentileUs(HudPhase::QPainter, 0.50), hudPercentileUs(HudPhase::QPainter, 0.95),
        hudMaxUs(HudPhase::QPainter),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Clear)]),
        hudSumUs(HudPhase::Clear), hudAverageUs(HudPhase::Clear),
        hudPercentileUs(HudPhase::Clear, 0.50), hudPercentileUs(HudPhase::Clear, 0.95),
        hudMaxUs(HudPhase::Clear),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Hash)]),
        hudSumUs(HudPhase::Hash), hudAverageUs(HudPhase::Hash),
        hudPercentileUs(HudPhase::Hash, 0.50), hudPercentileUs(HudPhase::Hash, 0.95),
        hudMaxUs(HudPhase::Hash),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::UploadPrepare)]),
        hudSumUs(HudPhase::UploadPrepare), hudAverageUs(HudPhase::UploadPrepare),
        hudPercentileUs(HudPhase::UploadPrepare, 0.50),
        hudPercentileUs(HudPhase::UploadPrepare, 0.95),
        hudMaxUs(HudPhase::UploadPrepare),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::GpuUpload)]),
        hudSumUs(HudPhase::GpuUpload), hudAverageUs(HudPhase::GpuUpload),
        hudPercentileUs(HudPhase::GpuUpload, 0.50),
        hudPercentileUs(HudPhase::GpuUpload, 0.95),
        hudMaxUs(HudPhase::GpuUpload),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Composite)]),
        hudSumUs(HudPhase::Composite), hudAverageUs(HudPhase::Composite),
        hudPercentileUs(HudPhase::Composite, 0.50),
        hudPercentileUs(HudPhase::Composite, 0.95),
        hudMaxUs(HudPhase::Composite),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::TotalActive)]),
        hudSumUs(HudPhase::TotalActive), hudAverageUs(HudPhase::TotalActive),
        hudPercentileUs(HudPhase::TotalActive, 0.50),
        hudPercentileUs(HudPhase::TotalActive, 0.95),
        hudMaxUs(HudPhase::TotalActive),
        static_cast<unsigned long long>(st.cntCustomHudCalls),
        static_cast<unsigned long long>(st.cntCustomHudDrawn),
        static_cast<unsigned long long>(st.cntHudVisualRenders),
        static_cast<unsigned long long>(st.cntHudVisualReuses),
        static_cast<unsigned long long>(st.cntHudVisualIdentityProbes),
        static_cast<unsigned long long>(st.cntHudVisualStampChecks),
        static_cast<unsigned long long>(st.cntHudVisualStampCommits),
        static_cast<unsigned long long>(st.cntScoreboardPlanBuilds),
        static_cast<unsigned long long>(st.cntScoreboardFullPlanRebuilds),
        static_cast<unsigned long long>(st.cntScoreboardStructureChecks),
        static_cast<unsigned long long>(st.cntScoreboardDynamicCellUpdates),
        static_cast<unsigned long long>(st.cntScoreboardTimeVisualChanges),
        static_cast<unsigned long long>(st.cntScoreboardOutlinePathHits),
        static_cast<unsigned long long>(st.cntScoreboardOutlinePathMisses),
        static_cast<unsigned long long>(st.cntHudRegionHashCalls),
        static_cast<unsigned long long>(st.sumHudRegionHashBytes),
        static_cast<unsigned long long>(st.cntHudUploadCalls));

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
    st.inputSampleTick = 0;
    st.inputSampleOpen = false;
    st.runFrameBeginRecorded = false;
    st.presentEndRecorded = false;
    st.currentHudPhaseTicks = 0;
    st.currentHudDrawn = false;
}

inline void MarkInputSample()
{
    State& st = S();
    if (!st.frameOpen || st.inputSampleOpen)
        return;
    st.inputSampleTick = SDL_GetPerformanceCounter();
    st.inputSampleOpen = true;
}

inline void MarkRunFrameBegin()
{
    State& st = S();
    if (!st.frameOpen || !st.inputSampleOpen || st.runFrameBeginRecorded)
        return;
    const Uint64 now = SDL_GetPerformanceCounter();
    RecordLatencySample(
        st.inputToRunFrameUs, st.inputToRunFrameCount,
        TicksToMs(now - st.inputSampleTick) * 1000.0);
    st.runFrameBeginRecorded = true;
}

inline void MarkPresentEnd()
{
    State& st = S();
    if (!st.frameOpen || !st.inputSampleOpen || st.presentEndRecorded)
        return;
    const Uint64 now = SDL_GetPerformanceCounter();
    RecordLatencySample(
        st.inputToPresentEndUs, st.inputToPresentEndCount,
        TicksToMs(now - st.inputSampleTick) * 1000.0);
    st.presentEndRecorded = true;
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
    if (st.currentHudDrawn && st.currentHudPhaseTicks)
    {
        const uint32_t index = static_cast<uint32_t>(HudPhase::TotalActive);
        uint32_t& count = st.hudPhaseCount[index];
        if (count < State::kHudPhaseCap)
            st.hudPhaseTicks[index][count++] = st.currentHudPhaseTicks;
        ++st.hudPhaseCalls[index];
        st.hudPhaseSumTicks[index] += st.currentHudPhaseTicks;
        if (st.currentHudPhaseTicks > st.hudPhaseMaxTicks[index])
            st.hudPhaseMaxTicks[index] = st.currentHudPhaseTicks;
    }
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

inline void AddHudPhaseTicks(HudPhase phase, Uint64 ticks)
{
    if (!S().frameOpen || ticks == 0)
        return;
    State& st = S();
    const uint32_t index = static_cast<uint32_t>(phase);
    uint32_t& count = st.hudPhaseCount[index];
    if (count < State::kHudPhaseCap)
        st.hudPhaseTicks[index][count++] = ticks;
    ++st.hudPhaseCalls[index];
    st.hudPhaseSumTicks[index] += ticks;
    if (ticks > st.hudPhaseMaxTicks[index])
        st.hudPhaseMaxTicks[index] = ticks;
    if (phase != HudPhase::TotalActive)
        st.currentHudPhaseTicks += ticks;
}

inline void CountCustomHudCall()
{
    if (S().frameOpen)
        ++S().cntCustomHudCalls;
}

inline void CountCustomHudDrawn()
{
    if (!S().frameOpen)
        return;
    ++S().cntCustomHudDrawn;
    S().currentHudDrawn = true;
}

inline void CountHudVisualRender()
{
    if (S().frameOpen) {
        ++S().cntHudVisualRenders;
        S().currentHudDrawn = true;
    }
}

inline void CountHudVisualReuse()
{
    if (S().frameOpen) {
        ++S().cntHudVisualReuses;
        S().currentHudDrawn = true;
    }
}

inline void CountHudVisualIdentityProbe()
{
    if (S().frameOpen)
        ++S().cntHudVisualIdentityProbes;
}

inline void CountHudVisualStampCheck()
{
    if (S().frameOpen)
        ++S().cntHudVisualStampChecks;
}

inline void CountHudVisualStampCommit()
{
    if (S().frameOpen)
        ++S().cntHudVisualStampCommits;
}

inline void CountScoreboardPlanBuild()
{
    if (S().frameOpen)
        ++S().cntScoreboardPlanBuilds;
}

inline void CountScoreboardFullPlanRebuild()
{
    if (S().frameOpen)
        ++S().cntScoreboardFullPlanRebuilds;
}

inline void CountScoreboardDynamicCellUpdate(bool timeVisualChange)
{
    if (!S().frameOpen)
        return;
    ++S().cntScoreboardDynamicCellUpdates;
    if (timeVisualChange)
        ++S().cntScoreboardTimeVisualChanges;
}

inline void CountScoreboardStructureCheck()
{
    if (S().frameOpen)
        ++S().cntScoreboardStructureChecks;
}

inline void CountScoreboardOutlinePathHit()
{
    if (S().frameOpen)
        ++S().cntScoreboardOutlinePathHits;
}

inline void CountScoreboardOutlinePathMiss()
{
    if (S().frameOpen)
        ++S().cntScoreboardOutlinePathMisses;
}

inline void CountHudRegionHash(std::size_t bytes)
{
    if (!S().frameOpen)
        return;
    ++S().cntHudRegionHashCalls;
    S().sumHudRegionHashBytes += static_cast<uint64_t>(bytes);
}

inline void CountHudUploadCall()
{
    if (S().frameOpen)
        ++S().cntHudUploadCalls;
}

class ScopedHudPhase {
public:
    explicit ScopedHudPhase(HudPhase phase)
        : phase_(phase), start_(ReadTicksIfActive()) {}

    ~ScopedHudPhase() { Stop(); }

    void Stop()
    {
        if (!start_)
            return;
        AddHudPhaseTicks(phase_, ReadTicksIfActive() - start_);
        start_ = 0;
    }

    ScopedHudPhase(const ScopedHudPhase&) = delete;
    ScopedHudPhase& operator=(const ScopedHudPhase&) = delete;

private:
    HudPhase phase_;
    Uint64 start_ = 0;
};

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
enum class HudPhase : uint8_t {
    State, ScoreboardPlan, ScoreboardRaster, QPainter, Clear, Hash,
    UploadPrepare, GpuUpload, Composite, TotalActive, Count
};

inline bool IsEnabled() { return false; }
inline bool IsFrameActive() { return false; }
inline unsigned long long ReadTicksIfActive() { return 0; }
inline void FrameBegin() {}
inline void FrameEnd() {}
inline void MarkInputSample() {}
inline void MarkRunFrameBegin() {}
inline void MarkPresentEnd() {}
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
inline void AddHudPhaseTicks(HudPhase, unsigned long long) {}
inline void CountCustomHudCall() {}
inline void CountCustomHudDrawn() {}
inline void CountHudVisualRender() {}
inline void CountHudVisualReuse() {}
inline void CountHudVisualIdentityProbe() {}
inline void CountHudVisualStampCheck() {}
inline void CountHudVisualStampCommit() {}
inline void CountScoreboardPlanBuild() {}
inline void CountScoreboardFullPlanRebuild() {}
inline void CountScoreboardDynamicCellUpdate(bool) {}
inline void CountScoreboardStructureCheck() {}
inline void CountScoreboardOutlinePathHit() {}
inline void CountScoreboardOutlinePathMiss() {}
inline void CountHudRegionHash(std::size_t) {}
inline void CountHudUploadCall() {}
inline void ShutdownReport() {}

class ScopedHudPhase {
public:
    explicit ScopedHudPhase(HudPhase) {}
    void Stop() {}
};

struct ScopedSection {
    explicit ScopedSection(Section) {}
    ~ScopedSection() = default;
};

} // namespace MelonPrimePerf

#endif

#endif // MELONPRIME_PERF_PROBE_H
