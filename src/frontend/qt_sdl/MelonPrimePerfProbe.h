#ifndef MELONPRIME_PERF_PROBE_H
#define MELONPRIME_PERF_PROBE_H

// Frame-time, hot-path, and Custom HUD phase counters for measured optimization.
// Compile gate: MELONPRIME_ENABLE_DEVELOPER_FEATURES + MELONPRIME_DS
// Runtime gate: MELONPRIME_PERF=1
// Release builds (developer features off): zero symbols, zero hot-path cost.

#include <cstddef>
#include <cstdint>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_DS)

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "MelonPrimePerfClock.h"
#include "MelonPrimePerfSession.h"

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
    FrameSetup,
    VulkanBegin,
    PreRun,
    PostDrawBookkeeping,
    UncappedYield,
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

// Fixed-size developer-only element breakdown for the Custom HUD painter.
// The collector is windowed and reports once per second; it never logs from a
// frame and has no release-build storage or work.
enum class HudElementMetric : uint8_t {
    Crosshair = 0,
    Hp,
    WeaponAmmo,
    WeaponInventory,
    Bomb,
    MatchStatus,
    RankTime,
    Scoreboard,
    EnemyTarget,
    RadarCpuOverlay,
    Text,
    Icon,
    Gauge,
    Outline,
    Count
};

// Input-specific timings are kept separate from the broad frame sections so
// the input budget can be compared across keyboard, joystick, and Raw Input
// runs without changing the production frame path.
enum class InputMetric : uint8_t {
    InputTotal = 0,
    JoystickLockWait,
    JoystickSample,
    JoystickProject,
    JoystickSDLUpdate,
    JoystickProcessMutexWait,
    JoystickProcessMutexHold,
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
    double currentInputToRunFrameUs = 0.0;
    double currentInputToPresentEndUs = 0.0;

    static constexpr uint32_t kRingCap = 8192;
    static constexpr uint32_t kHistBuckets = 64;
    static constexpr double kHistBucketMs = 0.5;

    double frameMsRing[kRingCap]{};
    uint32_t ringWrite = 0;
    uint32_t ringCount = 0;

    static constexpr uint32_t kFrameWindowCap = 512;
    double windowFrameMs[kFrameWindowCap]{};
    uint32_t windowFrameCount = 0;

    // Explicit latency is retained as a latest-N ring. Capture-only runs do
    // not reset this window until shutdown, so it must cover the benchmark
    // contract rather than freeze at the first few frames.
    static constexpr uint32_t kLatencyCap = 2048;
    double inputToRunFrameUs[kLatencyCap]{};
    double inputToPresentEndUs[kLatencyCap]{};
    uint32_t inputToRunFrameWrite = 0;
    uint32_t inputToPresentEndWrite = 0;
    uint32_t inputToRunFrameCount = 0;
    uint32_t inputToPresentEndCount = 0;
    uint64_t inputToRunFrameCalls = 0;
    uint64_t inputToPresentEndCalls = 0;
    double inputToRunFrameMaxUs = 0.0;
    double inputToPresentEndMaxUs = 0.0;

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
    uint64_t cntScoreboardRasterCacheHits = 0;
    uint64_t cntScoreboardRasterCacheMisses = 0;
    uint64_t cntScoreboardRasterBackingAllocations = 0;
    uint64_t cntScoreboardRasterBackingReuses = 0;
    uint64_t sumScoreboardRasterBackingBytes = 0;
    uint64_t sumScoreboardRasterCompositeBytes = 0;
    uint64_t sumScoreboardRasterCompositePixels = 0;
    uint64_t cntScoreboardRasterCompositeCalls = 0;
    uint64_t sumHudOverlayCompositeBytes = 0;
    uint64_t sumHudOverlayCompositePixels = 0;
    uint64_t cntHudOverlayCompositeCalls = 0;
    uint64_t sumScoreboardDirtyCellPixels = 0;
    uint64_t cntHudVisualGenerationChanges = 0;
    uint64_t cntHudConfigInvalidations = 0;
    uint64_t cntScoreboardPlanGenerationChanges = 0;
    uint64_t cntScoreboardRasterDirectDraws = 0;
    uint64_t sumScoreboardRasterDirectPixels = 0;
    uint64_t cntScoreboardDynamicCellUpdates = 0;
    uint64_t cntScoreboardTimeVisualChanges = 0;
    uint64_t cntScoreboardStructureChecks = 0;
    uint64_t cntScoreboardOutlinePathHits = 0;
    uint64_t cntScoreboardOutlinePathMisses = 0;
    uint64_t cntHudRegionHashCalls = 0;
    uint64_t sumHudRegionHashBytes = 0;
    uint64_t cntHudUploadCalls = 0;
    // Retained composer: how the per-frame HUD pixel work actually splits.
    // hud_visual_change_frames vs hud_visual_nochange_frames answers "did the
    // HUD change at all this frame"; the pixel sums answer "and how much of
    // the surface did that cost".
    uint64_t cntHudFullRecomposes = 0;
    uint64_t cntHudPartialRecomposes = 0;
    uint64_t cntHudComposeRepairPasses = 0;
    uint64_t cntHudComposeRepairFallbacks = 0;
    uint64_t cntHudElementVisualChanges = 0;
    uint64_t cntHudVisualChangeFrames = 0;
    uint64_t cntHudVisualNoChangeFrames = 0;
    uint64_t cntHudRetainedOnlyFrames = 0;
    uint64_t sumHudClearPixels = 0;
    uint64_t sumHudRecomposePixels = 0;
    uint64_t cntHudDirtyRegions = 0;
    uint64_t sumHudDirtyRegionPixels = 0;
    uint64_t sumHudDirtyBboxPixels = 0;
    uint64_t sumHudUploadPixels = 0;
    uint64_t sumHudUploadBytes = 0;
    // Scoreboard composite into the retained overlay, as opposed to the
    // scoreboard's own internal raster update.
    uint64_t cntScoreboardOuterCompositeCalls = 0;
    uint64_t sumScoreboardOuterCompositePixels = 0;
    uint64_t sumScoreboardPartialCompositePixels = 0;
    uint64_t cntScoreboardNoChangeFrames = 0;
    // CPU radar colour-key preprocessing (Software and Vulkan).
    uint64_t cntRadarCpuFilterCalls = 0;
    uint64_t sumRadarCpuFilterPixels = 0;
    Uint64 sumRadarCpuFilterTicks = 0;
    uint64_t cntHudRadarVboUploads = 0;
    uint64_t cntHudRadarTexParameterCalls = 0;
    uint64_t cntHudRestoreFastRejects = 0;
    uint64_t cntHudRestoreRuntimeReads = 0;
    uint64_t cntHudPatchWrites = 0;
    static constexpr uint32_t kHudElementCap = 256;
    Uint64 hudElementTicks[
        static_cast<uint32_t>(HudElementMetric::Count)][kHudElementCap]{};
    uint32_t hudElementCount[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    uint64_t hudElementCalls[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    Uint64 hudElementSumTicks[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    Uint64 hudElementMaxTicks[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    uint64_t hudElementAreaPixels[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    uint64_t hudElementDrawCalls[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    uint64_t hudElementGlyphs[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    uint64_t hudElementImageDraws[
        static_cast<uint32_t>(HudElementMetric::Count)]{};
    int activeHudElement = -1;
    uint64_t cntStageMatrixFullValidations = 0;
    uint64_t cntStageMatrixValidationRetries = 0;
    uint64_t cntSurfaceVisibilityStateChanges = 0;
    uint64_t cntRendererFastCacheRefreshes = 0;
    // Crosshair projection: how often the recomputed centre agreed with the
    // position the ROM published. A high reject rate means the shared
    // projection state is being sampled mid-change, and the crosshair is
    // spending those frames on the quantised fallback.
    uint64_t cntCrosshairProjectionAccepted = 0;
    uint64_t cntCrosshairProjectionRejected = 0;

    static constexpr uint32_t kInputMetricCap = 2048;
    Uint64 inputMetricTicks[
        static_cast<uint32_t>(InputMetric::Count)][kInputMetricCap]{};
    uint32_t inputMetricWrite[static_cast<uint32_t>(InputMetric::Count)]{};
    uint32_t inputMetricCount[static_cast<uint32_t>(InputMetric::Count)]{};
    uint64_t inputMetricCalls[static_cast<uint32_t>(InputMetric::Count)]{};
    Uint64 inputMetricSumTicks[static_cast<uint32_t>(InputMetric::Count)]{};
    Uint64 inputMetricMaxTicks[static_cast<uint32_t>(InputMetric::Count)]{};
    Uint64 inputTotalStartTick = 0;
    bool inputTotalOpen = false;

    Uint64 lastReportTick = 0;
    uint32_t histTotal[kHistBuckets]{};
    uint32_t histOverflow = 0;

    std::FILE* frameCsv = nullptr;
    bool frameCsvAttempted = false;
    uint64_t frameIndex = 0;

    uint64_t instanceId = 0;
    uint64_t reportSequence = 0;

    ~State()
    {
        if (frameCsv)
        {
            std::fclose(frameCsv);
            frameCsv = nullptr;
        }
    }
};

inline State& S()
{
    static thread_local State s;
    return s;
}

inline void BindInstance(uint64_t instanceId)
{
    if (!IsEnabled())
        return;
    S().instanceId = instanceId;
}

inline uint64_t NextReportSequence(State& st)
{
    return ++st.reportSequence;
}

inline bool IsCaptureOnly()
{
    static const bool captureOnly = [] {
        const char* v = std::getenv("MELONPRIME_PERF_CAPTURE_ONLY");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return captureOnly;
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

inline Uint64 ReadTicksIfEnabled()
{
    return IsEnabled() ? SDL_GetPerformanceCounter() : 0;
}

inline void CloseFrameCsv()
{
    State& st = S();
    if (st.frameCsv)
    {
        std::fclose(st.frameCsv);
        st.frameCsv = nullptr;
    }
}

inline void EnsureFrameCsv()
{
    State& st = S();
    if (st.frameCsvAttempted)
        return;
    st.frameCsvAttempted = true;

    const char* configuredPath = std::getenv("MELONPRIME_PERF_CSV");
    if (!configuredPath || !*configuredPath)
        return;

    std::string path(configuredPath);
    const std::string placeholder("%INSTANCE%");
    std::string::size_type placeholderPos = path.find(placeholder);
    if (placeholderPos != std::string::npos)
    {
        while (placeholderPos != std::string::npos)
        {
            path.replace(placeholderPos, placeholder.size(),
                std::to_string(st.instanceId));
            placeholderPos = path.find(placeholder,
                placeholderPos + 1);
        }
    }
    else
    {
        const std::string::size_type separator = path.find_last_of("/\\");
        const std::string::size_type extension = path.find_last_of('.');
        const bool hasExtension = extension != std::string::npos
            && (separator == std::string::npos || extension > separator);
        const std::string suffix = ".instance"
            + std::to_string(st.instanceId);
        if (hasExtension)
            path.insert(extension, suffix);
        else
            path += suffix;
    }

    st.frameCsv = std::fopen(path.c_str(), "wb");
    if (!st.frameCsv)
    {
        std::fprintf(stderr,
            "[MelonPrimePerf] frame CSV could not be opened: %s\n",
            path.c_str());
        return;
    }
    std::fprintf(st.frameCsv,
        "run_id,instance_id,frame_index,frame_start_ticks,frame_end_ticks,qpc_frequency,"
        "frame_time_us,input_sample_to_runframe_begin_us,"
        "input_sample_to_present_end_us\n");
}

inline void WriteFrameCsv(
    State& st, Uint64 endTick, double frameMs)
{
    if (!st.frameCsv)
        return;
    const char* runId = std::getenv("MELONPRIME_LATENCY_RUN_ID");
    std::fprintf(st.frameCsv,
        "%s,%llu,%llu,%llu,%llu,%llu,%.6f,%.6f,%.6f\n",
        runId ? runId : "unnamed-run",
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(st.frameIndex++),
        static_cast<unsigned long long>(st.frameStartTick),
        static_cast<unsigned long long>(endTick),
        static_cast<unsigned long long>(st.freq),
        frameMs,
        st.currentInputToRunFrameUs,
        st.currentInputToPresentEndUs);
}

struct PercentileSummary {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    // max is the whole-window maximum when the source tracks one separately;
    // retainedMax is always the maximum of the samples used for percentiles.
    double max = 0.0;
    double retainedMax = 0.0;
    uint32_t count = 0;
};

template <size_t Capacity>
inline PercentileSummary SummarizeDoubleSamples(
    const double* samples, uint32_t write, uint32_t count)
{
    PercentileSummary result;
    result.count = count < Capacity ? count : static_cast<uint32_t>(Capacity);
    if (!result.count)
        return result;

    double sorted[Capacity];
    const uint32_t capacity = static_cast<uint32_t>(Capacity);
    for (uint32_t i = 0; i < result.count; ++i) {
        const uint32_t index =
            (write + capacity - result.count + i) % capacity;
        sorted[i] = samples[index];
    }
    std::sort(sorted, sorted + result.count);
    result.p50 = PercentileSorted(sorted, result.count, 0.50);
    result.p95 = PercentileSorted(sorted, result.count, 0.95);
    result.p99 = PercentileSorted(sorted, result.count, 0.99);
    result.max = sorted[result.count - 1];
    result.retainedMax = result.max;
    return result;
}

inline PercentileSummary SummarizeInputMetric(
    const State& st, InputMetric metric)
{
    const uint32_t index = static_cast<uint32_t>(metric);
    PercentileSummary result;
    result.count = st.inputMetricCount[index];
    if (result.count)
    {
        Uint64 sorted[State::kInputMetricCap];
        const uint32_t write = st.inputMetricWrite[index];
        for (uint32_t i = 0; i < result.count; ++i) {
            const uint32_t sampleIndex =
                (write + State::kInputMetricCap - result.count + i)
                % State::kInputMetricCap;
            sorted[i] = st.inputMetricTicks[index][sampleIndex];
        }
        std::sort(sorted, sorted + result.count);
        const double frequency = static_cast<double>(st.freq);
        if (frequency > 0.0)
        {
            result.p50 = static_cast<double>(sorted[
                static_cast<uint32_t>(0.50 * (result.count - 1))])
                * 1000000.0 / frequency;
            result.p95 = static_cast<double>(sorted[
                static_cast<uint32_t>(0.95 * (result.count - 1))])
                * 1000000.0 / frequency;
            result.p99 = static_cast<double>(sorted[
                static_cast<uint32_t>(0.99 * (result.count - 1))])
                * 1000000.0 / frequency;
            result.retainedMax = static_cast<double>(sorted[result.count - 1])
                * 1000000.0 / frequency;
            result.max = static_cast<double>(st.inputMetricMaxTicks[index])
                * 1000000.0 / frequency;
        }
    }
    return result;
}

inline PercentileSummary SummarizeLatency(
    const double* samples, uint32_t write, uint32_t count)
{
    return SummarizeDoubleSamples<State::kLatencyCap>(samples, write, count);
}

inline PercentileSummary SummarizeHudPhase(
    const State& st, HudPhase phase)
{
    const uint32_t index = static_cast<uint32_t>(phase);
    const uint32_t count = st.hudPhaseCount[index];
    PercentileSummary result;
    result.count = count;
    if (!count || !st.freq)
        return result;

    double samples[State::kHudPhaseCap];
    for (uint32_t i = 0; i < count; ++i)
        samples[i] = static_cast<double>(st.hudPhaseTicks[index][i])
            * 1000000.0 / static_cast<double>(st.freq);
    // The temporary phase array is linear rather than a ring, so its next
    // write position is the current count.
    result = SummarizeDoubleSamples<State::kHudPhaseCap>(samples, count, count);
    result.max = static_cast<double>(st.hudPhaseMaxTicks[index])
        * 1000000.0 / static_cast<double>(st.freq);
    return result;
}

inline void RecordLatencySample(
    double* samples, uint32_t& write, uint32_t& count, double microseconds)
{
    samples[write] = microseconds;
    write = (write + 1) % State::kLatencyCap;
    if (count < State::kLatencyCap)
        ++count;
}

inline void RecordInputMetricTicks(InputMetric metric, Uint64 ticks)
{
    if (!IsEnabled())
        return;

    State& st = S();
    const uint32_t index = static_cast<uint32_t>(metric);
    uint32_t& write = st.inputMetricWrite[index];
    uint32_t& count = st.inputMetricCount[index];
    st.inputMetricTicks[index][write] = ticks;
    write = (write + 1) % State::kInputMetricCap;
    if (count < State::kInputMetricCap)
        ++count;
    ++st.inputMetricCalls[index];
    st.inputMetricSumTicks[index] += ticks;
    if (ticks > st.inputMetricMaxTicks[index])
        st.inputMetricMaxTicks[index] = ticks;
}

inline void BeginInputTotal()
{
    if (!IsEnabled())
        return;
    State& st = S();
    if (st.inputTotalOpen)
        return;
    st.inputTotalStartTick = SDL_GetPerformanceCounter();
    st.inputTotalOpen = true;
}

inline void EndInputTotal()
{
    State& st = S();
    if (!st.inputTotalOpen)
        return;
    const Uint64 endTick = SDL_GetPerformanceCounter();
    const Uint64 startTick = st.inputTotalStartTick;
    st.inputTotalOpen = false;
    st.inputTotalStartTick = 0;
    if (endTick >= startTick)
        RecordInputMetricTicks(InputMetric::InputTotal, endTick - startTick);
}

class ScopedInputMetric {
public:
    explicit ScopedInputMetric(InputMetric metric)
        : m_metric(metric), m_startTick(ReadTicksIfEnabled()) {}

    ~ScopedInputMetric() { Stop(); }

    void Stop()
    {
        if (!m_startTick)
            return;
        const Uint64 endTick = ReadTicksIfEnabled();
        if (endTick >= m_startTick)
            RecordInputMetricTicks(m_metric, endTick - m_startTick);
        m_startTick = 0;
    }

    ScopedInputMetric(const ScopedInputMetric&) = delete;
    ScopedInputMetric& operator=(const ScopedInputMetric&) = delete;

private:
    InputMetric m_metric;
    Uint64 m_startTick;
};

inline const char* HudElementMetricName(HudElementMetric metric)
{
    switch (metric) {
    case HudElementMetric::Crosshair:       return "crosshair";
    case HudElementMetric::Hp:              return "hp";
    case HudElementMetric::WeaponAmmo:      return "weapon_ammo";
    case HudElementMetric::WeaponInventory: return "weapon_inventory";
    case HudElementMetric::Bomb:             return "bomb";
    case HudElementMetric::MatchStatus:     return "match_status";
    case HudElementMetric::RankTime:        return "rank_time";
    case HudElementMetric::Scoreboard:      return "scoreboard";
    case HudElementMetric::EnemyTarget:     return "enemy_target";
    case HudElementMetric::RadarCpuOverlay: return "radar_cpu_overlay";
    case HudElementMetric::Text:            return "text";
    case HudElementMetric::Icon:            return "icon";
    case HudElementMetric::Gauge:           return "gauge";
    case HudElementMetric::Outline:         return "outline";
    case HudElementMetric::Count:           break;
    }
    return "unknown";
}

inline void RecordHudElementTicks(HudElementMetric metric, Uint64 ticks)
{
    if (!S().frameOpen || ticks == 0)
        return;
    State& st = S();
    const uint32_t index = static_cast<uint32_t>(metric);
    uint32_t& count = st.hudElementCount[index];
    if (count < State::kHudElementCap)
        st.hudElementTicks[index][count++] = ticks;
    ++st.hudElementCalls[index];
    st.hudElementSumTicks[index] += ticks;
    if (ticks > st.hudElementMaxTicks[index])
        st.hudElementMaxTicks[index] = ticks;
}

inline void AddHudElementDrawStats(
    uint64_t areaPixels, uint32_t drawCalls, uint32_t glyphs,
    uint32_t imageDraws)
{
    State& st = S();
    if (!st.frameOpen || st.activeHudElement < 0)
        return;
    const uint32_t index = static_cast<uint32_t>(st.activeHudElement);
    st.hudElementAreaPixels[index] += areaPixels;
    st.hudElementDrawCalls[index] += drawCalls;
    st.hudElementGlyphs[index] += glyphs;
    st.hudElementImageDraws[index] += imageDraws;
}

inline void CountHudElementText(const char* text)
{
    State& st = S();
    if (!st.frameOpen || st.activeHudElement < 0)
        return;
    uint32_t glyphs = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
         p && *p; ++p) {
        if ((*p & 0xC0u) != 0x80u)
            ++glyphs;
    }
    const uint32_t index = static_cast<uint32_t>(st.activeHudElement);
    st.hudElementGlyphs[index] += glyphs;
}

inline void CountHudElementGlyphs(uint32_t glyphs)
{
    State& st = S();
    if (st.frameOpen && st.activeHudElement >= 0)
        st.hudElementGlyphs[static_cast<uint32_t>(st.activeHudElement)] += glyphs;
}

inline void CountHudElementImageDraw()
{
    State& st = S();
    if (st.frameOpen && st.activeHudElement >= 0)
        ++st.hudElementImageDraws[static_cast<uint32_t>(st.activeHudElement)];
}

inline PercentileSummary SummarizeHudElement(
    const State& st, HudElementMetric metric)
{
    const uint32_t index = static_cast<uint32_t>(metric);
    const uint32_t count = st.hudElementCount[index];
    PercentileSummary result;
    result.count = count;
    if (!count || !st.freq)
        return result;
    double samples[State::kHudElementCap];
    for (uint32_t i = 0; i < count; ++i)
        samples[i] = static_cast<double>(st.hudElementTicks[index][i])
            * 1000000.0 / static_cast<double>(st.freq);
    result = SummarizeDoubleSamples<State::kHudElementCap>(samples, count, count);
    result.max = static_cast<double>(st.hudElementMaxTicks[index])
        * 1000000.0 / static_cast<double>(st.freq);
    return result;
}

class ScopedHudElement {
public:
    explicit ScopedHudElement(HudElementMetric metric)
        : m_metric(metric), m_startTick(ReadTicksIfActive()),
          m_previousActive(S().activeHudElement)
    {
        if (m_startTick)
            S().activeHudElement = static_cast<int>(metric);
    }

    ~ScopedHudElement() { Stop(); }

    void Stop()
    {
        if (!m_startTick)
            return;
        const Uint64 endTick = ReadTicksIfActive();
        if (endTick >= m_startTick)
            RecordHudElementTicks(m_metric, endTick - m_startTick);
        S().activeHudElement = m_previousActive;
        m_startTick = 0;
    }

    ScopedHudElement(const ScopedHudElement&) = delete;
    ScopedHudElement& operator=(const ScopedHudElement&) = delete;

private:
    HudElementMetric m_metric;
    Uint64 m_startTick = 0;
    int m_previousActive = -1;
};

inline void ResetWindowStats()
{
    State& st = S();
    st.windowFrameCount = 0;
    st.inputToRunFrameWrite = 0;
    st.inputToPresentEndWrite = 0;
    st.inputToRunFrameCount = 0;
    st.inputToPresentEndCount = 0;
    st.inputToRunFrameCalls = 0;
    st.inputToPresentEndCalls = 0;
    st.inputToRunFrameMaxUs = 0.0;
    st.inputToPresentEndMaxUs = 0.0;
    std::memset(st.inputMetricWrite, 0, sizeof(st.inputMetricWrite));
    std::memset(st.inputMetricCount, 0, sizeof(st.inputMetricCount));
    std::memset(st.inputMetricCalls, 0, sizeof(st.inputMetricCalls));
    std::memset(st.inputMetricSumTicks, 0, sizeof(st.inputMetricSumTicks));
    std::memset(st.inputMetricMaxTicks, 0, sizeof(st.inputMetricMaxTicks));
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
    st.cntScoreboardRasterCacheHits = 0;
    st.cntScoreboardRasterCacheMisses = 0;
    st.cntScoreboardRasterBackingAllocations = 0;
    st.cntScoreboardRasterBackingReuses = 0;
    st.sumScoreboardRasterBackingBytes = 0;
    st.sumScoreboardRasterCompositeBytes = 0;
    st.sumScoreboardRasterCompositePixels = 0;
    st.cntScoreboardRasterCompositeCalls = 0;
    st.sumHudOverlayCompositeBytes = 0;
    st.sumHudOverlayCompositePixels = 0;
    st.cntHudOverlayCompositeCalls = 0;
    st.sumScoreboardDirtyCellPixels = 0;
    st.cntHudVisualGenerationChanges = 0;
    st.cntHudConfigInvalidations = 0;
    st.cntScoreboardPlanGenerationChanges = 0;
    st.cntScoreboardRasterDirectDraws = 0;
    st.sumScoreboardRasterDirectPixels = 0;
    st.cntScoreboardDynamicCellUpdates = 0;
    st.cntScoreboardTimeVisualChanges = 0;
    st.cntScoreboardStructureChecks = 0;
    st.cntScoreboardOutlinePathHits = 0;
    st.cntScoreboardOutlinePathMisses = 0;
    st.cntHudRegionHashCalls = 0;
    st.sumHudRegionHashBytes = 0;
    st.cntHudUploadCalls = 0;
    st.cntHudRadarVboUploads = 0;
    st.cntHudRadarTexParameterCalls = 0;
    st.cntHudRestoreFastRejects = 0;
    st.cntHudRestoreRuntimeReads = 0;
    st.cntHudPatchWrites = 0;
    std::memset(st.hudElementTicks, 0, sizeof(st.hudElementTicks));
    std::memset(st.hudElementCount, 0, sizeof(st.hudElementCount));
    std::memset(st.hudElementCalls, 0, sizeof(st.hudElementCalls));
    std::memset(st.hudElementSumTicks, 0, sizeof(st.hudElementSumTicks));
    std::memset(st.hudElementMaxTicks, 0, sizeof(st.hudElementMaxTicks));
    std::memset(st.hudElementAreaPixels, 0, sizeof(st.hudElementAreaPixels));
    std::memset(st.hudElementDrawCalls, 0, sizeof(st.hudElementDrawCalls));
    std::memset(st.hudElementGlyphs, 0, sizeof(st.hudElementGlyphs));
    std::memset(st.hudElementImageDraws, 0, sizeof(st.hudElementImageDraws));
    st.activeHudElement = -1;
    st.cntStageMatrixFullValidations = 0;
    st.cntStageMatrixValidationRetries = 0;
    st.cntSurfaceVisibilityStateChanges = 0;
    st.cntRendererFastCacheRefreshes = 0;
    st.cntCrosshairProjectionAccepted = 0;
    st.cntCrosshairProjectionRejected = 0;
}

inline void ReportGenerationHeader(const State& st, uint64_t reportSeq)
{
    const char* sessionId = MelonPrimePerfSession::Text();
    const unsigned captureOnly = IsCaptureOnly() ? 1u : 0u;
    std::fprintf(stderr,
        "[MelonPrimePerf] report_begin session_id=%s instance_id=%llu "
        "report_seq=%llu capture_only=%u\n",
        sessionId,
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq), captureOnly);
    // Keep the older marker as a compatibility/readability line.  The
    // report_begin line above is the strict source of capture provenance.
    std::fprintf(stderr,
        "[MelonPrimePerf] capture_mode session_id=%s instance_id=%llu "
        "report_seq=%llu capture_only=%u\n",
        sessionId,
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq), captureOnly);
}

inline void ReportExplicitLatency(const State& st, uint64_t reportSeq)
{
    const PercentileSummary runFrame = SummarizeLatency(
        st.inputToRunFrameUs, st.inputToRunFrameWrite,
        st.inputToRunFrameCount);
    const PercentileSummary presentEnd = SummarizeLatency(
        st.inputToPresentEndUs, st.inputToPresentEndWrite,
        st.inputToPresentEndCount);
    std::fprintf(stderr,
        "[MelonPrimePerf] explicit_latency_us session_id=%s instance_id=%llu "
        "report_seq=%llu "
        "frame_input_sample_to_runframe_begin_us="
        "calls=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f "
        "max=%.1f retained_max=%.1f "
        "input_sample_to_present_end_us="
        "calls=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f "
        "max=%.1f retained_max=%.1f\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        static_cast<unsigned long long>(st.inputToRunFrameCalls),
        runFrame.count, runFrame.p50, runFrame.p95, runFrame.p99,
        st.inputToRunFrameMaxUs, runFrame.retainedMax,
        static_cast<unsigned long long>(st.inputToPresentEndCalls),
        presentEnd.count, presentEnd.p50, presentEnd.p95, presentEnd.p99,
        st.inputToPresentEndMaxUs, presentEnd.retainedMax);
}

inline void ReportInputMetricSummary(const State& st, uint64_t reportSeq)
{
    PercentileSummary metrics[static_cast<uint32_t>(InputMetric::Count)];
    for (uint32_t i = 0; i < static_cast<uint32_t>(InputMetric::Count); ++i)
        metrics[i] = SummarizeInputMetric(st, static_cast<InputMetric>(i));

    const auto metric = [&](InputMetric inputMetric) -> const PercentileSummary& {
        return metrics[static_cast<uint32_t>(inputMetric)];
    };
    const auto calls = [&](InputMetric inputMetric) -> unsigned long long {
        return static_cast<unsigned long long>(st.inputMetricCalls[
            static_cast<uint32_t>(inputMetric)]);
    };
    const auto retained = [&](InputMetric inputMetric) -> unsigned int {
        return metric(inputMetric).count;
    };
    std::fprintf(stderr,
        "[MelonPrimePerf] input_metric_us session_id=%s instance_id=%llu "
        "report_seq=%llu "
        "input_total[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_lock_wait[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_sample[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_project[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_sdl_update[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_process_mutex_wait[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f] "
        "joystick_process_mutex_hold[c=%llu retained=%u p50=%.1f p95=%.1f p99=%.1f max=%.1f retained_max=%.1f]\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        calls(InputMetric::InputTotal), retained(InputMetric::InputTotal),
        metric(InputMetric::InputTotal).p50,
        metric(InputMetric::InputTotal).p95, metric(InputMetric::InputTotal).p99,
        metric(InputMetric::InputTotal).max,
        metric(InputMetric::InputTotal).retainedMax,
        calls(InputMetric::JoystickLockWait), retained(InputMetric::JoystickLockWait),
        metric(InputMetric::JoystickLockWait).p50,
        metric(InputMetric::JoystickLockWait).p95, metric(InputMetric::JoystickLockWait).p99,
        metric(InputMetric::JoystickLockWait).max,
        metric(InputMetric::JoystickLockWait).retainedMax,
        calls(InputMetric::JoystickSample), retained(InputMetric::JoystickSample),
        metric(InputMetric::JoystickSample).p50,
        metric(InputMetric::JoystickSample).p95, metric(InputMetric::JoystickSample).p99,
        metric(InputMetric::JoystickSample).max,
        metric(InputMetric::JoystickSample).retainedMax,
        calls(InputMetric::JoystickProject), retained(InputMetric::JoystickProject),
        metric(InputMetric::JoystickProject).p50,
        metric(InputMetric::JoystickProject).p95, metric(InputMetric::JoystickProject).p99,
        metric(InputMetric::JoystickProject).max,
        metric(InputMetric::JoystickProject).retainedMax,
        calls(InputMetric::JoystickSDLUpdate), retained(InputMetric::JoystickSDLUpdate),
        metric(InputMetric::JoystickSDLUpdate).p50,
        metric(InputMetric::JoystickSDLUpdate).p95, metric(InputMetric::JoystickSDLUpdate).p99,
        metric(InputMetric::JoystickSDLUpdate).max,
        metric(InputMetric::JoystickSDLUpdate).retainedMax,
        calls(InputMetric::JoystickProcessMutexWait),
        retained(InputMetric::JoystickProcessMutexWait),
        metric(InputMetric::JoystickProcessMutexWait).p50,
        metric(InputMetric::JoystickProcessMutexWait).p95,
        metric(InputMetric::JoystickProcessMutexWait).p99,
        metric(InputMetric::JoystickProcessMutexWait).max,
        metric(InputMetric::JoystickProcessMutexWait).retainedMax,
        calls(InputMetric::JoystickProcessMutexHold),
        retained(InputMetric::JoystickProcessMutexHold),
        metric(InputMetric::JoystickProcessMutexHold).p50,
        metric(InputMetric::JoystickProcessMutexHold).p95,
        metric(InputMetric::JoystickProcessMutexHold).p99,
        metric(InputMetric::JoystickProcessMutexHold).max,
        metric(InputMetric::JoystickProcessMutexHold).retainedMax);
}

inline void ReportHudPhaseSummary(const State& st, uint64_t reportSeq)
{
    PercentileSummary phases[static_cast<uint32_t>(HudPhase::Count)];
    for (uint32_t i = 0; i < static_cast<uint32_t>(HudPhase::Count); ++i)
        phases[i] = SummarizeHudPhase(st, static_cast<HudPhase>(i));

    const double ticksToUs = st.freq
        ? 1000000.0 / static_cast<double>(st.freq) : 0.0;
    const auto phase = [&](HudPhase hudPhase) -> const PercentileSummary& {
        return phases[static_cast<uint32_t>(hudPhase)];
    };
    const auto sumUs = [&](HudPhase hudPhase) -> double {
        return static_cast<double>(st.hudPhaseSumTicks[
            static_cast<uint32_t>(hudPhase)]) * ticksToUs;
    };
    const auto averageUs = [&](HudPhase hudPhase) -> double {
        const uint32_t index = static_cast<uint32_t>(hudPhase);
        return st.hudPhaseCalls[index]
            ? sumUs(hudPhase) / static_cast<double>(st.hudPhaseCalls[index])
            : 0.0;
    };
    std::fprintf(stderr,
        "[MelonPrimePerf] hud_phase_us session_id=%s instance_id=%llu "
        "report_seq=%llu "
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
         "scoreboard_raster_cache_hit=%llu scoreboard_raster_cache_miss=%llu "
         "scoreboard_raster_alloc=%llu scoreboard_raster_reuse=%llu "
         "scoreboard_raster_bytes=%llu "
         "scoreboard_raster_composite_bytes=%llu "
         "scoreboard_raster_composite_pixels=%llu "
         "scoreboard_raster_composite_calls=%llu "
         "hud_overlay_composite_bytes=%llu "
         "hud_overlay_composite_pixels=%llu "
         "hud_overlay_composite_calls=%llu "
         "scoreboard_dirty_cell_pixels=%llu "
         "hud_visual_generation_changes=%llu "
         "hud_config_invalidation_count=%llu "
         "scoreboard_plan_generation_changes=%llu "
         "scoreboard_raster_direct_draws=%llu "
         "scoreboard_raster_direct_pixels=%llu "
         "structure_checks=%llu dynamic_cells=%llu time_changes=%llu "
        "outline_hit=%llu outline_miss=%llu hash_calls=%llu hash_B=%llu uploads=%llu "
        "radar_vbo_uploads=%llu radar_tex_parameter_calls=%llu "
        "restore_fast_reject=%llu restore_runtime_read=%llu "
        "patch_writes=%llu\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::State)]),
        sumUs(HudPhase::State), averageUs(HudPhase::State), phase(HudPhase::State).p50,
        phase(HudPhase::State).p95, phase(HudPhase::State).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::ScoreboardPlan)]),
        sumUs(HudPhase::ScoreboardPlan), averageUs(HudPhase::ScoreboardPlan),
        phase(HudPhase::ScoreboardPlan).p50, phase(HudPhase::ScoreboardPlan).p95,
        phase(HudPhase::ScoreboardPlan).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::ScoreboardRaster)]),
        sumUs(HudPhase::ScoreboardRaster), averageUs(HudPhase::ScoreboardRaster),
        phase(HudPhase::ScoreboardRaster).p50, phase(HudPhase::ScoreboardRaster).p95,
        phase(HudPhase::ScoreboardRaster).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::QPainter)]),
        sumUs(HudPhase::QPainter), averageUs(HudPhase::QPainter),
        phase(HudPhase::QPainter).p50, phase(HudPhase::QPainter).p95,
        phase(HudPhase::QPainter).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Clear)]),
        sumUs(HudPhase::Clear), averageUs(HudPhase::Clear), phase(HudPhase::Clear).p50,
        phase(HudPhase::Clear).p95, phase(HudPhase::Clear).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Hash)]),
        sumUs(HudPhase::Hash), averageUs(HudPhase::Hash), phase(HudPhase::Hash).p50,
        phase(HudPhase::Hash).p95, phase(HudPhase::Hash).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::UploadPrepare)]),
        sumUs(HudPhase::UploadPrepare), averageUs(HudPhase::UploadPrepare),
        phase(HudPhase::UploadPrepare).p50, phase(HudPhase::UploadPrepare).p95,
        phase(HudPhase::UploadPrepare).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::GpuUpload)]),
        sumUs(HudPhase::GpuUpload), averageUs(HudPhase::GpuUpload),
        phase(HudPhase::GpuUpload).p50, phase(HudPhase::GpuUpload).p95,
        phase(HudPhase::GpuUpload).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::Composite)]),
        sumUs(HudPhase::Composite), averageUs(HudPhase::Composite),
        phase(HudPhase::Composite).p50, phase(HudPhase::Composite).p95,
        phase(HudPhase::Composite).max,
        static_cast<unsigned long long>(st.hudPhaseCalls[static_cast<uint32_t>(HudPhase::TotalActive)]),
        sumUs(HudPhase::TotalActive), averageUs(HudPhase::TotalActive),
        phase(HudPhase::TotalActive).p50, phase(HudPhase::TotalActive).p95,
        phase(HudPhase::TotalActive).max,
        static_cast<unsigned long long>(st.cntCustomHudCalls),
        static_cast<unsigned long long>(st.cntCustomHudDrawn),
        static_cast<unsigned long long>(st.cntHudVisualRenders),
        static_cast<unsigned long long>(st.cntHudVisualReuses),
        static_cast<unsigned long long>(st.cntHudVisualIdentityProbes),
        static_cast<unsigned long long>(st.cntHudVisualStampChecks),
        static_cast<unsigned long long>(st.cntHudVisualStampCommits),
        static_cast<unsigned long long>(st.cntScoreboardPlanBuilds),
        static_cast<unsigned long long>(st.cntScoreboardFullPlanRebuilds),
        static_cast<unsigned long long>(st.cntScoreboardRasterCacheHits),
        static_cast<unsigned long long>(st.cntScoreboardRasterCacheMisses),
         static_cast<unsigned long long>(st.cntScoreboardRasterBackingAllocations),
         static_cast<unsigned long long>(st.cntScoreboardRasterBackingReuses),
         static_cast<unsigned long long>(st.sumScoreboardRasterBackingBytes),
         static_cast<unsigned long long>(st.sumScoreboardRasterCompositeBytes),
         static_cast<unsigned long long>(st.sumScoreboardRasterCompositePixels),
         static_cast<unsigned long long>(st.cntScoreboardRasterCompositeCalls),
         static_cast<unsigned long long>(st.sumHudOverlayCompositeBytes),
         static_cast<unsigned long long>(st.sumHudOverlayCompositePixels),
         static_cast<unsigned long long>(st.cntHudOverlayCompositeCalls),
         static_cast<unsigned long long>(st.sumScoreboardDirtyCellPixels),
         static_cast<unsigned long long>(st.cntHudVisualGenerationChanges),
         static_cast<unsigned long long>(st.cntHudConfigInvalidations),
         static_cast<unsigned long long>(st.cntScoreboardPlanGenerationChanges),
         static_cast<unsigned long long>(st.cntScoreboardRasterDirectDraws),
         static_cast<unsigned long long>(st.sumScoreboardRasterDirectPixels),
         static_cast<unsigned long long>(st.cntScoreboardStructureChecks),
        static_cast<unsigned long long>(st.cntScoreboardDynamicCellUpdates),
        static_cast<unsigned long long>(st.cntScoreboardTimeVisualChanges),
        static_cast<unsigned long long>(st.cntScoreboardOutlinePathHits),
        static_cast<unsigned long long>(st.cntScoreboardOutlinePathMisses),
        static_cast<unsigned long long>(st.cntHudRegionHashCalls),
        static_cast<unsigned long long>(st.sumHudRegionHashBytes),
        static_cast<unsigned long long>(st.cntHudUploadCalls),
        static_cast<unsigned long long>(st.cntHudRadarVboUploads),
        static_cast<unsigned long long>(st.cntHudRadarTexParameterCalls),
        static_cast<unsigned long long>(st.cntHudRestoreFastRejects),
        static_cast<unsigned long long>(st.cntHudRestoreRuntimeReads),
        static_cast<unsigned long long>(st.cntHudPatchWrites));

    // Retained compositor line. The two questions it answers are "how often did
    // the HUD actually change" (change vs nochange frames) and "when it did,
    // how much of the surface did that cost" -- region pixels against the
    // bounding-box pixels the old single-rectangle contract would have used.
    const double overdrawRatio = st.sumHudDirtyRegionPixels
        ? static_cast<double>(st.sumHudDirtyBboxPixels)
            / static_cast<double>(st.sumHudDirtyRegionPixels)
        : 0.0;
    std::fprintf(stderr,
        "[MelonPrimePerf] hud_compose session_id=%s instance_id=%llu report_seq=%llu "
        "visual_change_frames=%llu visual_nochange_frames=%llu retained_only_frames=%llu "
        "full_recompose=%llu partial_recompose=%llu repair_pass=%llu repair_fallback=%llu "
        "element_changes=%llu dirty_regions=%llu dirty_region_px=%llu "
        "dirty_bbox_px=%llu bbox_overdraw=%.2fx clear_px=%llu recompose_px=%llu "
        "upload_px=%llu upload_B=%llu "
        "sb_outer_calls=%llu sb_outer_px=%llu sb_partial_px=%llu sb_nochange_frames=%llu "
        "radar_filter_calls=%llu radar_filter_px=%llu radar_filter_us=%.1f\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        static_cast<unsigned long long>(st.cntHudVisualChangeFrames),
        static_cast<unsigned long long>(st.cntHudVisualNoChangeFrames),
        static_cast<unsigned long long>(st.cntHudRetainedOnlyFrames),
        static_cast<unsigned long long>(st.cntHudFullRecomposes),
        static_cast<unsigned long long>(st.cntHudPartialRecomposes),
        static_cast<unsigned long long>(st.cntHudComposeRepairPasses),
        static_cast<unsigned long long>(st.cntHudComposeRepairFallbacks),
        static_cast<unsigned long long>(st.cntHudElementVisualChanges),
        static_cast<unsigned long long>(st.cntHudDirtyRegions),
        static_cast<unsigned long long>(st.sumHudDirtyRegionPixels),
        static_cast<unsigned long long>(st.sumHudDirtyBboxPixels),
        overdrawRatio,
        static_cast<unsigned long long>(st.sumHudClearPixels),
        static_cast<unsigned long long>(st.sumHudRecomposePixels),
        static_cast<unsigned long long>(st.sumHudUploadPixels),
        static_cast<unsigned long long>(st.sumHudUploadBytes),
        static_cast<unsigned long long>(st.cntScoreboardOuterCompositeCalls),
        static_cast<unsigned long long>(st.sumScoreboardOuterCompositePixels),
        static_cast<unsigned long long>(st.sumScoreboardPartialCompositePixels),
        static_cast<unsigned long long>(st.cntScoreboardNoChangeFrames),
        static_cast<unsigned long long>(st.cntRadarCpuFilterCalls),
        static_cast<unsigned long long>(st.sumRadarCpuFilterPixels),
        st.freq ? (static_cast<double>(st.sumRadarCpuFilterTicks) * 1000000.0
                       / static_cast<double>(st.freq))
                : 0.0);
}

inline void ReportHudElementSummary(const State& st, uint64_t reportSeq)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(HudElementMetric::Count); ++i) {
        const auto metric = static_cast<HudElementMetric>(i);
        const PercentileSummary summary = SummarizeHudElement(st, metric);
        std::fprintf(stderr,
            "[MelonPrimePerf] hud_element_us session_id=%s instance_id=%llu "
            "report_seq=%llu element=%s calls=%llu retained=%u "
            "p50=%.1f p95=%.1f p99=%.1f max=%.1f area_px=%llu "
            "draw_calls=%llu glyphs=%llu image_draws=%llu\n",
            MelonPrimePerfSession::Text(),
            static_cast<unsigned long long>(st.instanceId),
            static_cast<unsigned long long>(reportSeq),
            HudElementMetricName(metric),
            static_cast<unsigned long long>(st.hudElementCalls[i]),
            summary.count,
            summary.p50, summary.p95, summary.p99, summary.max,
            static_cast<unsigned long long>(st.hudElementAreaPixels[i]),
            static_cast<unsigned long long>(st.hudElementDrawCalls[i]),
            static_cast<unsigned long long>(st.hudElementGlyphs[i]),
            static_cast<unsigned long long>(st.hudElementImageDraws[i]));
    }
}

inline void MaybeReport1Hz()
{
    State& st = S();
    if (!st.freq || IsCaptureOnly())
        return;

    const Uint64 now = SDL_GetPerformanceCounter();
    if (!st.lastReportTick)
        st.lastReportTick = now;

    const double sinceReportMs = TicksToMs(now - st.lastReportTick);
    if (sinceReportMs < 1000.0)
        return;

    const uint64_t reportSeq = NextReportSequence(st);
    ReportGenerationHeader(st, reportSeq);
    const auto reportClock = melonDS::MelonPrimePerfClock::Now();
    std::fprintf(stderr,
        "[MelonPrimePerfPhase] session_id=%s instance_id=%llu report_seq=%llu "
        "report_qpc_ticks=%llu qpc_frequency=%llu\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        static_cast<unsigned long long>(reportClock.Ticks),
        static_cast<unsigned long long>(reportClock.Frequency));

    const PercentileSummary frame = SummarizeDoubleSamples<State::kFrameWindowCap>(
        st.windowFrameMs, st.windowFrameCount, st.windowFrameCount);
    const auto secAvg = [&](Section sec) -> double {
        const uint32_t idx = static_cast<uint32_t>(sec);
        return st.secSamples[idx]
            ? st.secSumMs[idx] / static_cast<double>(st.secSamples[idx]) : 0.0;
    };
    const uint64_t inputTotal =
        st.cntInputSource[static_cast<uint32_t>(InputSource::WinRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::MacRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::LinuxRaw)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::PanelDelta)]
        + st.cntInputSource[static_cast<uint32_t>(InputSource::QCursorFallback)];

    std::fprintf(stderr,
        "[MelonPrimePerf] frame_ms session_id=%s instance_id=%llu "
        "report_seq=%llu "
        "p50=%.3f p95=%.3f p99=%.3f max=%.3f n=%u | "
        "sec_avg_ms sleep=%.3f spin=%.3f setup=%.3f vkbegin=%.3f input=%.3f prerun=%.3f run=%.3f draw=%.3f drain=%.3f book=%.3f yield=%.3f | "
        "input_src raw=%llu mac=%llu linux=%llu panel=%llu qcur=%llu (tot=%llu) | "
        "warp=%llu oog_patch=%llu osd_apply=%llu osd_write=%llu | "
        "hud_dirty_px=%llu gl_up_B=%llu dr3_skip=%llu hud_render_us=%.1f\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        frame.p50, frame.p95, frame.p99, frame.max, frame.count,
        secAvg(Section::LimiterSleep), secAvg(Section::LimiterSpin),
        secAvg(Section::FrameSetup), secAvg(Section::VulkanBegin),
        secAvg(Section::Input), secAvg(Section::PreRun), secAvg(Section::RunFrame),
        secAvg(Section::Draw), secAvg(Section::DeferredDrain),
        secAvg(Section::PostDrawBookkeeping), secAvg(Section::UncappedYield),
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
            ? TicksToMs(st.sumCustomHudTicks) * 1000.0
                / static_cast<double>(st.cntCustomHudFrames) : 0.0);

    std::fprintf(stderr,
        "[MelonPrimePerf] audit_counts session_id=%s instance_id=%llu "
        "report_seq=%llu "
        "stage_matrix_full_validation=%llu stage_matrix_validation_retry=%llu "
        "surface_visibility_state_change=%llu renderer_fast_cache_refresh=%llu "
        "crosshair_projection_accepted=%llu crosshair_projection_rejected=%llu\n",
        MelonPrimePerfSession::Text(),
        static_cast<unsigned long long>(st.instanceId),
        static_cast<unsigned long long>(reportSeq),
        static_cast<unsigned long long>(st.cntStageMatrixFullValidations),
        static_cast<unsigned long long>(st.cntStageMatrixValidationRetries),
        static_cast<unsigned long long>(st.cntSurfaceVisibilityStateChanges),
        static_cast<unsigned long long>(st.cntRendererFastCacheRefreshes),
        static_cast<unsigned long long>(st.cntCrosshairProjectionAccepted),
        static_cast<unsigned long long>(st.cntCrosshairProjectionRejected));

    ReportExplicitLatency(st, reportSeq);
    ReportInputMetricSummary(st, reportSeq);
    ReportHudPhaseSummary(st, reportSeq);
    ReportHudElementSummary(st, reportSeq);

    st.lastReportTick = now;
    ResetWindowStats();
    if (st.frameCsv)
        std::fflush(st.frameCsv);
}

inline void FrameBegin()
{
    if (!IsEnabled())
        return;

    State& st = S();
    if (!st.freq)
        st.freq = SDL_GetPerformanceFrequency();

    EnsureFrameCsv();

    st.frameOpen = true;
    st.frameStartTick = SDL_GetPerformanceCounter();
    st.sectionOpen = false;
    st.inputSampleTick = 0;
    st.inputSampleOpen = false;
    st.runFrameBeginRecorded = false;
    st.presentEndRecorded = false;
    st.currentInputToRunFrameUs = 0.0;
    st.currentInputToPresentEndUs = 0.0;
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
    const double latencyUs = TicksToMs(now - st.inputSampleTick) * 1000.0;
    ++st.inputToRunFrameCalls;
    if (latencyUs > st.inputToRunFrameMaxUs)
        st.inputToRunFrameMaxUs = latencyUs;
    RecordLatencySample(
        st.inputToRunFrameUs, st.inputToRunFrameWrite,
        st.inputToRunFrameCount, latencyUs);
    st.currentInputToRunFrameUs = latencyUs;
    st.runFrameBeginRecorded = true;
}

inline void MarkPresentEnd()
{
    State& st = S();
    if (!st.frameOpen || !st.inputSampleOpen || st.presentEndRecorded)
        return;
    const Uint64 now = SDL_GetPerformanceCounter();
    const double latencyUs = TicksToMs(now - st.inputSampleTick) * 1000.0;
    ++st.inputToPresentEndCalls;
    if (latencyUs > st.inputToPresentEndMaxUs)
        st.inputToPresentEndMaxUs = latencyUs;
    RecordLatencySample(
        st.inputToPresentEndUs, st.inputToPresentEndWrite,
        st.inputToPresentEndCount, latencyUs);
    st.currentInputToPresentEndUs = latencyUs;
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
    const double frameMs = TicksToMs(endTick - st.frameStartTick);
    WriteFrameCsv(st, endTick, frameMs);
    RecordFrameMs(frameMs);
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

inline void CountScoreboardRasterCacheHit()
{
    if (S().frameOpen)
        ++S().cntScoreboardRasterCacheHits;
}

inline void CountScoreboardRasterCacheMiss()
{
    if (S().frameOpen)
        ++S().cntScoreboardRasterCacheMisses;
}

inline void CountScoreboardRasterBackingAllocation(std::size_t bytes)
{
    if (!S().frameOpen)
        return;
    ++S().cntScoreboardRasterBackingAllocations;
    S().sumScoreboardRasterBackingBytes += static_cast<uint64_t>(bytes);
}

inline void CountScoreboardRasterBackingReuse()
{
    if (S().frameOpen)
        ++S().cntScoreboardRasterBackingReuses;
}

inline void CountScoreboardRasterComposite(std::size_t bytes, std::size_t pixels)
{
    if (!S().frameOpen)
        return;
    ++S().cntScoreboardRasterCompositeCalls;
    S().sumScoreboardRasterCompositeBytes += static_cast<uint64_t>(bytes);
    S().sumScoreboardRasterCompositePixels += static_cast<uint64_t>(pixels);
}

inline void CountScoreboardRasterDirectDraw(std::size_t pixels)
{
    if (!S().frameOpen)
        return;
    ++S().cntScoreboardRasterDirectDraws;
    S().sumScoreboardRasterDirectPixels += static_cast<uint64_t>(pixels);
}

inline void AddHudOverlayComposite(std::size_t bytes, std::size_t pixels)
{
    if (!S().frameOpen)
        return;
    ++S().cntHudOverlayCompositeCalls;
    S().sumHudOverlayCompositeBytes += static_cast<uint64_t>(bytes);
    S().sumHudOverlayCompositePixels += static_cast<uint64_t>(pixels);
}

inline void AddScoreboardDirtyCellPixels(std::size_t pixels)
{
    if (S().frameOpen)
        S().sumScoreboardDirtyCellPixels += static_cast<uint64_t>(pixels);
}

inline void CountHudVisualGenerationChange()
{
    if (IsEnabled())
        ++S().cntHudVisualGenerationChanges;
}

inline void CountHudConfigInvalidation()
{
    if (IsEnabled())
        ++S().cntHudConfigInvalidations;
}

inline void CountScoreboardPlanGenerationChange()
{
    if (S().frameOpen)
        ++S().cntScoreboardPlanGenerationChanges;
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

inline void CountHudFullRecompose()
{
    if (S().frameOpen)
        ++S().cntHudFullRecomposes;
}

inline void CountHudPartialRecompose()
{
    if (S().frameOpen)
        ++S().cntHudPartialRecomposes;
}

inline void CountHudComposeRepairPass()
{
    if (S().frameOpen)
        ++S().cntHudComposeRepairPasses;
}

inline void CountHudComposeRepairFallback()
{
    if (S().frameOpen)
        ++S().cntHudComposeRepairFallbacks;
}

inline void CountHudElementVisualChange()
{
    if (S().frameOpen)
        ++S().cntHudElementVisualChanges;
}

// One call per composed frame: `regions` is the bounded dirty-region count and
// `bboxPixels` the single rectangle the old bounding-box contract would have
// produced, so the overdraw ratio between them is directly observable.
inline void RecordHudDirtyRegions(uint32_t regions, uint64_t regionPixels,
                                  uint64_t bboxPixels)
{
    if (!S().frameOpen)
        return;
    State& st = S();
    if (regions == 0) {
        ++st.cntHudVisualNoChangeFrames;
        return;
    }
    ++st.cntHudVisualChangeFrames;
    st.cntHudDirtyRegions += regions;
    st.sumHudDirtyRegionPixels += regionPixels;
    st.sumHudDirtyBboxPixels += bboxPixels;
}

inline void AddHudClearPixels(uint64_t pixels)
{
    if (!S().frameOpen || pixels == 0)
        return;
    S().sumHudClearPixels += pixels;
}

inline void AddHudRecomposePixels(uint64_t pixels)
{
    if (!S().frameOpen || pixels == 0)
        return;
    S().sumHudRecomposePixels += pixels;
}

// A frame that composited the retained HUD layer without uploading anything.
inline void CountHudRetainedOnlyFrame()
{
    if (S().frameOpen)
        ++S().cntHudRetainedOnlyFrames;
}

inline void AddHudUploadRegion(uint64_t pixels, uint64_t bytes)
{
    if (!S().frameOpen)
        return;
    State& st = S();
    st.sumHudUploadPixels += pixels;
    st.sumHudUploadBytes += bytes;
}

inline void CountScoreboardOuterComposite(uint64_t pixels, bool partial)
{
    if (!S().frameOpen)
        return;
    State& st = S();
    ++st.cntScoreboardOuterCompositeCalls;
    st.sumScoreboardOuterCompositePixels += pixels;
    if (partial)
        st.sumScoreboardPartialCompositePixels += pixels;
}

inline void CountScoreboardNoChangeFrame()
{
    if (S().frameOpen)
        ++S().cntScoreboardNoChangeFrames;
}

inline void RecordRadarCpuFilter(uint64_t pixels, Uint64 ticks)
{
    if (!S().frameOpen)
        return;
    State& st = S();
    ++st.cntRadarCpuFilterCalls;
    st.sumRadarCpuFilterPixels += pixels;
    st.sumRadarCpuFilterTicks += ticks;
}

inline void CountHudRadarVboUpload()
{
    if (S().frameOpen)
        ++S().cntHudRadarVboUploads;
}

inline void CountHudRadarTexParameterCall()
{
    if (S().frameOpen)
        ++S().cntHudRadarTexParameterCalls;
}

inline void CountHudRestoreFastReject()
{
    if (S().frameOpen)
        ++S().cntHudRestoreFastRejects;
}

inline void CountHudRestoreRuntimeRead()
{
    if (S().frameOpen)
        ++S().cntHudRestoreRuntimeReads;
}

inline void CountHudPatchWrite()
{
    if (S().frameOpen)
        ++S().cntHudPatchWrites;
}

inline void CountStageMatrixFullValidation()
{
    if (S().frameOpen)
        ++S().cntStageMatrixFullValidations;
}

inline void CountStageMatrixValidationRetry()
{
    if (S().frameOpen)
        ++S().cntStageMatrixValidationRetries;
}

inline void CountSurfaceVisibilityStateChange()
{
    if (S().frameOpen)
        ++S().cntSurfaceVisibilityStateChanges;
}

inline void CountCrosshairProjection(bool accepted)
{
    if (!S().frameOpen)
        return;
    if (accepted)
        ++S().cntCrosshairProjectionAccepted;
    else
        ++S().cntCrosshairProjectionRejected;
}

inline void CountRendererFastCacheRefresh()
{
    if (S().frameOpen)
        ++S().cntRendererFastCacheRefreshes;
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
    const uint64_t reportSeq = NextReportSequence(st);
    ReportGenerationHeader(st, reportSeq);
    if (st.ringCount == 0)
        std::fprintf(stderr,
            "[MelonPrimePerf] shutdown session_id=%s instance_id=%llu "
            "report_seq=%llu: "
            "no frames recorded\n",
            MelonPrimePerfSession::Text(),
            static_cast<unsigned long long>(st.instanceId),
            static_cast<unsigned long long>(reportSeq));
    else
    {
        double sorted[State::kRingCap];
        const uint32_t n = st.ringCount;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (st.ringWrite + State::kRingCap - n + i)
                % State::kRingCap;
            sorted[i] = st.frameMsRing[idx];
        }
        std::sort(sorted, sorted + n);

        const double p50 = PercentileSorted(sorted, n, 0.50);
        const double p95 = PercentileSorted(sorted, n, 0.95);
        const double p99 = PercentileSorted(sorted, n, 0.99);
        const double maxMs = sorted[n - 1];

        std::fprintf(stderr,
            "[MelonPrimePerf] shutdown summary session_id=%s instance_id=%llu "
            "report_seq=%llu: "
            "frames=%u frame_ms "
            "p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
            MelonPrimePerfSession::Text(),
            static_cast<unsigned long long>(st.instanceId),
            static_cast<unsigned long long>(reportSeq), n,
            p50, p95, p99, maxMs);

        std::fprintf(stderr,
            "[MelonPrimePerf] histogram session_id=%s instance_id=%llu "
            "report_seq=%llu "
            "bucket_ms=%.1f:\n",
            MelonPrimePerfSession::Text(),
            static_cast<unsigned long long>(st.instanceId),
            static_cast<unsigned long long>(reportSeq), State::kHistBucketMs);
        for (uint32_t b = 0; b < State::kHistBuckets; ++b) {
            if (st.histTotal[b])
                std::fprintf(stderr, "  %4.1f-%4.1f ms: %u\n",
                    b * State::kHistBucketMs,
                    (b + 1) * State::kHistBucketMs,
                    st.histTotal[b]);
        }
        if (st.histOverflow)
            std::fprintf(stderr, "  >=%.1f ms: %u\n",
                State::kHistBuckets * State::kHistBucketMs,
                st.histOverflow);
    }

    // Capture-only runs intentionally defer all formatting and sorting until
    // this owning EmuThread shutdown point. These helpers also make the final
    // partial report useful when a live 1 Hz window never elapsed.
    ReportExplicitLatency(st, reportSeq);
    ReportInputMetricSummary(st, reportSeq);
    ReportHudPhaseSummary(st, reportSeq);
    ReportHudElementSummary(st, reportSeq);
    if (st.frameCsv)
        std::fflush(st.frameCsv);
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
    FrameSetup,
    VulkanBegin,
    PreRun,
    PostDrawBookkeeping,
    UncappedYield,
    Count
};
enum class HudPhase : uint8_t {
    State, ScoreboardPlan, ScoreboardRaster, QPainter, Clear, Hash,
    UploadPrepare, GpuUpload, Composite, TotalActive, Count
};
enum class HudElementMetric : uint8_t {
    Crosshair, Hp, WeaponAmmo, WeaponInventory, Bomb, MatchStatus,
    RankTime, Scoreboard, EnemyTarget, RadarCpuOverlay, Text, Icon,
    Gauge, Outline, Count
};

enum class InputMetric : uint8_t {
    InputTotal,
    JoystickLockWait,
    JoystickSample,
    JoystickProject,
    JoystickSDLUpdate,
    JoystickProcessMutexWait,
    JoystickProcessMutexHold,
    Count
};

inline bool IsEnabled() { return false; }
inline void BindInstance(uint64_t) {}
inline bool IsCaptureOnly() { return false; }
inline bool IsFrameActive() { return false; }
inline unsigned long long ReadTicksIfActive() { return 0; }
inline unsigned long long ReadTicksIfEnabled() { return 0; }
inline void BeginInputTotal() {}
inline void EndInputTotal() {}
inline void RecordInputMetricTicks(InputMetric, unsigned long long) {}
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
inline void CountScoreboardRasterCacheHit() {}
inline void CountScoreboardRasterCacheMiss() {}
inline void CountScoreboardRasterBackingAllocation(std::size_t) {}
inline void CountScoreboardRasterBackingReuse() {}
inline void CountScoreboardRasterComposite(std::size_t, std::size_t) {}
inline void CountScoreboardRasterDirectDraw(std::size_t) {}
inline void AddHudOverlayComposite(std::size_t, std::size_t) {}
inline void AddScoreboardDirtyCellPixels(std::size_t) {}
inline void CountHudVisualGenerationChange() {}
inline void CountHudConfigInvalidation() {}
inline void CountScoreboardPlanGenerationChange() {}
inline void CountScoreboardDynamicCellUpdate(bool) {}
inline void CountScoreboardStructureCheck() {}
inline void CountScoreboardOutlinePathHit() {}
inline void CountScoreboardOutlinePathMiss() {}
inline void CountHudRegionHash(std::size_t) {}
inline void CountHudUploadCall() {}
inline void CountHudFullRecompose() {}
inline void CountHudPartialRecompose() {}
inline void CountHudComposeRepairPass() {}
inline void CountHudComposeRepairFallback() {}
inline void CountHudElementVisualChange() {}
inline void RecordHudDirtyRegions(uint32_t, uint64_t, uint64_t) {}
inline void AddHudClearPixels(uint64_t) {}
inline void AddHudRecomposePixels(uint64_t) {}
inline void CountHudRetainedOnlyFrame() {}
inline void AddHudUploadRegion(uint64_t, uint64_t) {}
inline void CountScoreboardOuterComposite(uint64_t, bool) {}
inline void CountScoreboardNoChangeFrame() {}
inline void RecordRadarCpuFilter(uint64_t, uint64_t) {}
inline void CountHudRadarVboUpload() {}
inline void CountHudRadarTexParameterCall() {}
inline void CountHudRestoreFastReject() {}
inline void CountHudRestoreRuntimeRead() {}
inline void CountHudPatchWrite() {}
inline void AddHudElementDrawStats(uint64_t, uint32_t, uint32_t, uint32_t) {}
inline void CountHudElementText(const char*) {}
inline void CountHudElementGlyphs(uint32_t) {}
inline void CountHudElementImageDraw() {}
inline void CountStageMatrixFullValidation() {}
inline void CountStageMatrixValidationRetry() {}
inline void CountSurfaceVisibilityStateChange() {}
inline void CountCrosshairProjection(bool) {}
inline void CountRendererFastCacheRefresh() {}
inline void ShutdownReport() {}

class ScopedInputMetric {
public:
    explicit ScopedInputMetric(InputMetric) {}
    void Stop() {}
};

class ScopedHudPhase {
public:
    explicit ScopedHudPhase(HudPhase) {}
    void Stop() {}
};

class ScopedHudElement {
public:
    explicit ScopedHudElement(HudElementMetric) {}
    void Stop() {}
};

struct ScopedSection {
    explicit ScopedSection(Section) {}
    ~ScopedSection() = default;
};

} // namespace MelonPrimePerf

#endif

#endif // MELONPRIME_PERF_PROBE_H
