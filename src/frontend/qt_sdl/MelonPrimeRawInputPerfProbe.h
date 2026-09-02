#ifndef MELONPRIME_RAW_INPUT_PERF_PROBE_H
#define MELONPRIME_RAW_INPUT_PERF_PROBE_H

// Windows Raw Input contention and recovery telemetry.
// Compile gate: MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY + MELONPRIME_DS
// on Windows. Runtime gate: MELONPRIME_RAW_INPUT_PERF=1.
// Release builds keep the same lock semantics without the telemetry state.

#include <atomic>
#include <cstdint>
#include <mutex>

#if defined(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY) \
    && defined(MELONPRIME_DS) && defined(_WIN32)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#endif

namespace MelonPrime {
namespace RawInputPerf {

#if defined(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY) \
    && defined(MELONPRIME_DS) && defined(_WIN32)

enum class LockSite : uint8_t {
    Other = 0,
    Snapshot,
    LateLatch,
    DeferredDrain,
    HiddenWndProc,
    NativeEvent,
    Count
};

enum class Stage : uint8_t {
    RawSnapshot = 0,
    RawLateLatch,
    RawDeferredDrain,
    Count
};

// Stable names for the Phase 0 developer telemetry contract. StageStats and
// the counters below keep the storage specialized, while this enum gives
// scripts and audit checks one vocabulary for the measured surfaces.
enum class Metric : uint8_t {
    RawSubscriptionLockWait = 0,
    RawSnapshot,
    RawLateLatch,
    RawDeferredDrain,
    RawBatchCallCount,
    RawBatchEventCount,
    Count
};

enum class LockKind : uint8_t {
    Subscription,
    Frame,
};

struct StageStats {
    static constexpr uint32_t kSampleCap = 2048;
    std::atomic<uint64_t> samples[kSampleCap]{};
    std::atomic<uint64_t> writeIndex{ 0 };
    std::atomic<uint64_t> calls{ 0 };
    std::atomic<uint64_t> sumNs{ 0 };
    std::atomic<uint64_t> maxNs{ 0 };
};

struct Counters {
    std::atomic<uint64_t> mutexAcquisitions{ 0 };
    std::atomic<uint64_t> mutexWaitNs{ 0 };
    std::atomic<uint64_t> mutexHoldNs{ 0 };
    std::atomic<uint64_t> maxMutexWaitNs{ 0 };
    std::atomic<uint64_t> maxMutexHoldNs{ 0 };
    std::atomic<uint64_t> subscriptionMutexAcquisitions{ 0 };
    std::atomic<uint64_t> subscriptionMutexWaitNs{ 0 };
    std::atomic<uint64_t> subscriptionMutexHoldNs{ 0 };
    std::atomic<uint64_t> subscriptionMutexMaxWaitNs{ 0 };
    std::atomic<uint64_t> frameMutexAcquisitions{ 0 };
    std::atomic<uint64_t> frameMutexWaitNs{ 0 };
    std::atomic<uint64_t> frameMutexHoldNs{ 0 };
    std::atomic<uint64_t> frameMutexMaxWaitNs{ 0 };
    std::atomic<uint64_t> recursiveAcquisitions{ 0 };
    std::atomic<uint64_t> subscriptionMaxRecursionDepth{ 0 };
    std::atomic<uint64_t> frameMaxRecursionDepth{ 0 };
    std::atomic<uint64_t> rawBufferCalls{ 0 };
    std::atomic<uint64_t> rawBufferNs{ 0 };
    std::atomic<uint64_t> maxRawBufferNs{ 0 };
    std::atomic<uint64_t> getAsyncKeyStateCalls{ 0 };
    std::atomic<uint64_t> hiddenWindowDispatches{ 0 };
    std::atomic<uint64_t> stuckRecoveryScans{ 0 };
    std::atomic<uint64_t> stuckRecoveryClears{ 0 };
    std::atomic<uint64_t> lockWaitBySite[
        static_cast<uint32_t>(LockSite::Count)]{};
    StageStats stage[static_cast<uint32_t>(Stage::Count)];
    std::atomic<uint64_t> rawBatchCalls{ 0 };
    std::atomic<uint64_t> rawBatchNonEmptyCalls{ 0 };
    std::atomic<uint64_t> rawBatchEmptyCalls{ 0 };
    std::atomic<uint64_t> rawBatchEvents{ 0 };
    std::atomic<uint64_t> lateLatchDeltaClaims{ 0 };
    std::atomic<uint64_t> postDrawEventsCaptured{ 0 };
};

inline Counters& Stats() noexcept
{
    static Counters counters;
    return counters;
}

inline bool Enabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_RAW_INPUT_PERF");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

inline bool IsCaptureOnly() noexcept
{
    static const bool captureOnly = [] {
        const char* value = std::getenv("MELONPRIME_RAW_INPUT_PERF_CAPTURE_ONLY");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return captureOnly;
}

inline uint64_t NowNs() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void AtomicMax(std::atomic<uint64_t>& target, uint64_t value) noexcept
{
    uint64_t previous = target.load(std::memory_order_relaxed);
    while (previous < value
        && !target.compare_exchange_weak(
            previous, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

inline void RecordMutexWait(
    uint64_t elapsedNs, LockSite site = LockSite::Other,
    LockKind kind = LockKind::Subscription) noexcept
{
    auto& stats = Stats();
    stats.mutexAcquisitions.fetch_add(1, std::memory_order_relaxed);
    stats.mutexWaitNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxMutexWaitNs, elapsedNs);
    stats.lockWaitBySite[static_cast<uint32_t>(site)].fetch_add(
        elapsedNs, std::memory_order_relaxed);
    if (kind == LockKind::Subscription) {
        stats.subscriptionMutexAcquisitions.fetch_add(1, std::memory_order_relaxed);
        stats.subscriptionMutexWaitNs.fetch_add(elapsedNs, std::memory_order_relaxed);
        AtomicMax(stats.subscriptionMutexMaxWaitNs, elapsedNs);
    }
    else {
        stats.frameMutexAcquisitions.fetch_add(1, std::memory_order_relaxed);
        stats.frameMutexWaitNs.fetch_add(elapsedNs, std::memory_order_relaxed);
        AtomicMax(stats.frameMutexMaxWaitNs, elapsedNs);
    }
}

inline void RecordMutexHold(
    uint64_t elapsedNs, LockKind kind = LockKind::Subscription) noexcept
{
    auto& stats = Stats();
    stats.mutexHoldNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxMutexHoldNs, elapsedNs);
    if (kind == LockKind::Subscription)
        stats.subscriptionMutexHoldNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    else
        stats.frameMutexHoldNs.fetch_add(elapsedNs, std::memory_order_relaxed);
}

struct LockDepthSlot {
    const void* mutex = nullptr;
    uint32_t depth = 0;
};

// Telemetry-only bookkeeping. The fixed slots avoid introducing a map or
// allocation into the lock wrapper while still distinguishing nested locks on
// different subscription-local frame mutexes.
static constexpr uint32_t kLockDepthSlotCap = 8;
inline thread_local LockDepthSlot g_subscriptionLockDepth[kLockDepthSlotCap]{};
inline thread_local LockDepthSlot g_frameLockDepth[kLockDepthSlotCap]{};

inline uint32_t EnterLockDepth(
    LockDepthSlot* depthSlots, const void* mutex) noexcept
{
    for (uint32_t i = 0; i < kLockDepthSlotCap; ++i) {
        if (depthSlots[i].mutex == mutex)
            return ++depthSlots[i].depth;
    }
    for (uint32_t i = 0; i < kLockDepthSlotCap; ++i) {
        if (!depthSlots[i].mutex) {
            depthSlots[i].mutex = mutex;
            depthSlots[i].depth = 1;
            return 1;
        }
    }
    return 0;
}

inline void LeaveLockDepth(
    LockDepthSlot* depthSlots, const void* mutex) noexcept
{
    for (uint32_t i = 0; i < kLockDepthSlotCap; ++i) {
        if (depthSlots[i].mutex != mutex)
            continue;
        if (depthSlots[i].depth > 1) {
            --depthSlots[i].depth;
        }
        else {
            depthSlots[i] = {};
        }
        return;
    }
}

inline void RecordRecursiveAcquisition(
    LockKind kind, uint32_t depth) noexcept
{
    if (!Enabled() || depth <= 1)
        return;
    auto& stats = Stats();
    stats.recursiveAcquisitions.fetch_add(1, std::memory_order_relaxed);
    if (kind == LockKind::Subscription)
        AtomicMax(stats.subscriptionMaxRecursionDepth, depth);
    else
        AtomicMax(stats.frameMaxRecursionDepth, depth);
}

inline void RecordRawBuffer(uint64_t elapsedNs) noexcept
{
    auto& stats = Stats();
    stats.rawBufferCalls.fetch_add(1, std::memory_order_relaxed);
    stats.rawBufferNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxRawBufferNs, elapsedNs);
}

inline void RecordStage(Stage stage, uint64_t elapsedNs) noexcept
{
    if (!Enabled() || elapsedNs == 0)
        return;
    auto& stats = Stats().stage[static_cast<uint32_t>(stage)];
    const uint64_t index = stats.writeIndex.fetch_add(
        1, std::memory_order_relaxed) % StageStats::kSampleCap;
    stats.samples[index].store(elapsedNs, std::memory_order_relaxed);
    stats.calls.fetch_add(1, std::memory_order_relaxed);
    stats.sumNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxNs, elapsedNs);
}

inline void RecordRawBatch(uint64_t events) noexcept
{
    if (!Enabled())
        return;
    auto& stats = Stats();
    stats.rawBatchCalls.fetch_add(1, std::memory_order_relaxed);
    stats.rawBatchEvents.fetch_add(events, std::memory_order_relaxed);
    if (events == 0)
        stats.rawBatchEmptyCalls.fetch_add(1, std::memory_order_relaxed);
    else
        stats.rawBatchNonEmptyCalls.fetch_add(1, std::memory_order_relaxed);
}

inline void RecordLateLatchDelta(int x, int y) noexcept
{
    if (Enabled() && (x != 0 || y != 0))
        Stats().lateLatchDeltaClaims.fetch_add(1, std::memory_order_relaxed);
}

inline thread_local bool g_postDrawCapture = false;

class PostDrawCaptureScope {
public:
    PostDrawCaptureScope() noexcept
        : m_previous(g_postDrawCapture)
    {
        g_postDrawCapture = true;
    }

    ~PostDrawCaptureScope() { g_postDrawCapture = m_previous; }

    PostDrawCaptureScope(const PostDrawCaptureScope&) = delete;
    PostDrawCaptureScope& operator=(const PostDrawCaptureScope&) = delete;

private:
    bool m_previous;
};

inline void RecordPostDrawEvents(uint64_t events) noexcept
{
    if (Enabled() && g_postDrawCapture)
        Stats().postDrawEventsCaptured.fetch_add(
            events, std::memory_order_relaxed);
}

inline void CountGetAsyncKeyState() noexcept
{
    if (!Enabled())
        return;
    Stats().getAsyncKeyStateCalls.fetch_add(1, std::memory_order_relaxed);
}

inline void CountHiddenWindowDispatch() noexcept
{
    if (!Enabled())
        return;
    Stats().hiddenWindowDispatches.fetch_add(1, std::memory_order_relaxed);
}

inline void CountStuckRecovery(bool cleared) noexcept
{
    if (!Enabled())
        return;
    auto& stats = Stats();
    stats.stuckRecoveryScans.fetch_add(1, std::memory_order_relaxed);
    if (cleared)
        stats.stuckRecoveryClears.fetch_add(1, std::memory_order_relaxed);
}

// Measures the selected Raw subscription mutex acquisitions without changing
// the recursive locking contract. The caller keeps this object in the same
// scope as the old std::lock_guard.
class SubscriptionMutexGuard {
public:
    explicit SubscriptionMutexGuard(
        std::recursive_mutex& mutex, LockSite site = LockSite::Other)
        : m_lock(mutex, std::defer_lock)
        , m_measure(Enabled())
        , m_site(site)
        , m_waitStartNs(m_measure ? NowNs() : 0)
        , m_acquiredNs(0)
        , m_mutex(&mutex)
        , m_depth(0)
    {
        m_lock.lock();
        if (m_measure) {
            m_acquiredNs = NowNs();
            m_depth = EnterLockDepth(g_subscriptionLockDepth, m_mutex);
        }
    }

    ~SubscriptionMutexGuard()
    {
        const uint64_t releaseNs = m_measure ? NowNs() : 0;
        m_lock.unlock();
        if (m_measure) {
            if (m_acquiredNs >= m_waitStartNs)
                RecordMutexWait(
                    m_acquiredNs - m_waitStartNs,
                    m_site, LockKind::Subscription);
            if (releaseNs >= m_acquiredNs)
                RecordMutexHold(
                    releaseNs - m_acquiredNs, LockKind::Subscription);
            RecordRecursiveAcquisition(LockKind::Subscription, m_depth);
            LeaveLockDepth(g_subscriptionLockDepth, m_mutex);
        }
    }

    SubscriptionMutexGuard(const SubscriptionMutexGuard&) = delete;
    SubscriptionMutexGuard& operator=(const SubscriptionMutexGuard&) = delete;

private:
    std::unique_lock<std::recursive_mutex> m_lock;
    bool m_measure;
    LockSite m_site;
    uint64_t m_waitStartNs;
    uint64_t m_acquiredNs;
    std::recursive_mutex* m_mutex;
    uint32_t m_depth;
};

// The frame/data-plane lock is intentionally per subscription. Reusing the
// same telemetry surface for it keeps the lock-wait budget comparable before
// and after control/data separation without adding a second timing observer.
class FrameMutexGuard {
public:
    explicit FrameMutexGuard(
        std::recursive_mutex& mutex, LockSite site = LockSite::Other)
        : m_lock(mutex, std::defer_lock)
        , m_measure(Enabled())
        , m_site(site)
        , m_waitStartNs(m_measure ? NowNs() : 0)
        , m_acquiredNs(0)
        , m_mutex(&mutex)
        , m_depth(0)
    {
        m_lock.lock();
        if (m_measure) {
            m_acquiredNs = NowNs();
            m_depth = EnterLockDepth(g_frameLockDepth, m_mutex);
        }
    }

    ~FrameMutexGuard()
    {
        const uint64_t releaseNs = m_measure ? NowNs() : 0;
        m_lock.unlock();
        if (m_measure) {
            if (m_acquiredNs >= m_waitStartNs)
                RecordMutexWait(
                    m_acquiredNs - m_waitStartNs,
                    m_site, LockKind::Frame);
            if (releaseNs >= m_acquiredNs)
                RecordMutexHold(
                    releaseNs - m_acquiredNs, LockKind::Frame);
            RecordRecursiveAcquisition(LockKind::Frame, m_depth);
            LeaveLockDepth(g_frameLockDepth, m_mutex);
        }
    }

    FrameMutexGuard(const FrameMutexGuard&) = delete;
    FrameMutexGuard& operator=(const FrameMutexGuard&) = delete;

private:
    std::unique_lock<std::recursive_mutex> m_lock;
    bool m_measure;
    LockSite m_site;
    uint64_t m_waitStartNs;
    uint64_t m_acquiredNs;
    std::recursive_mutex* m_mutex;
    uint32_t m_depth;
};

class ScopedStage {
public:
    explicit ScopedStage(Stage stage)
        : m_stage(stage)
        , m_measure(Enabled())
        , m_startedNs(m_measure ? NowNs() : 0)
    {}

    ~ScopedStage()
    {
        if (m_measure)
            RecordStage(m_stage, NowNs() - m_startedNs);
    }

    ScopedStage(const ScopedStage&) = delete;
    ScopedStage& operator=(const ScopedStage&) = delete;

private:
    Stage m_stage;
    bool m_measure;
    uint64_t m_startedNs;
};

class RawBufferScope {
public:
    RawBufferScope()
        : m_measure(Enabled())
        , m_startedNs(m_measure ? NowNs() : 0)
    {}

    ~RawBufferScope()
    {
        if (m_measure)
            RecordRawBuffer(NowNs() - m_startedNs);
    }

    RawBufferScope(const RawBufferScope&) = delete;
    RawBufferScope& operator=(const RawBufferScope&) = delete;

private:
    bool m_measure;
    uint64_t m_startedNs;
};

struct StagePercentiles {
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

inline StagePercentiles SummarizeStage(const StageStats& stage) noexcept
{
    StagePercentiles result;
    const uint64_t total = stage.calls.load(std::memory_order_relaxed);
    const uint32_t count = static_cast<uint32_t>(
        total < StageStats::kSampleCap ? total : StageStats::kSampleCap);
    if (count) {
        uint64_t values[StageStats::kSampleCap];
        for (uint32_t i = 0; i < count; ++i)
            values[i] = stage.samples[i].load(std::memory_order_relaxed);
        std::sort(values, values + count);
        const auto percentileNs = [&](double percentile) {
            const double index = percentile * static_cast<double>(count - 1);
            const uint32_t lower = static_cast<uint32_t>(index);
            const uint32_t upper = lower + 1 < count ? lower + 1 : lower;
            const double fraction = index - static_cast<double>(lower);
            return static_cast<double>(values[lower]) * (1.0 - fraction)
                + static_cast<double>(values[upper]) * fraction;
        };
        result.p50 = percentileNs(0.50) / 1000.0;
        result.p95 = percentileNs(0.95) / 1000.0;
        result.p99 = percentileNs(0.99) / 1000.0;
    }
    result.max = static_cast<double>(
        stage.maxNs.load(std::memory_order_relaxed)) / 1000.0;
    return result;
}

inline void Report(bool force) noexcept
{
    if (!Enabled() || (!force && IsCaptureOnly()))
        return;

    if (!force) {
        static std::atomic<uint64_t> lastReportNs{ 0 };
        const uint64_t nowNs = NowNs();
        uint64_t previousReportNs = lastReportNs.load(std::memory_order_relaxed);
        for (;;) {
            if (previousReportNs != 0
                && nowNs - previousReportNs < 1000000000ULL)
                return;
            if (lastReportNs.compare_exchange_weak(
                    previousReportNs, nowNs,
                    std::memory_order_relaxed, std::memory_order_relaxed))
                break;
        }
    }

    const auto& stats = Stats();
    std::fprintf(stderr,
        "[MelonPrimeRawPerf] mutex_acq=%llu mutex_wait_ns=%llu "
        "mutex_hold_ns=%llu mutex_wait_max_ns=%llu mutex_hold_max_ns=%llu "
        "raw_buffer_calls=%llu raw_buffer_ns=%llu raw_buffer_max_ns=%llu "
        "get_async_calls=%llu hidden_dispatches=%llu recovery_scans=%llu "
        "recovery_clears=%llu\n",
        static_cast<unsigned long long>(stats.mutexAcquisitions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.mutexWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.mutexHoldNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.maxMutexWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.maxMutexHoldNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBufferCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBufferNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.maxRawBufferNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.getAsyncKeyStateCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.hiddenWindowDispatches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.stuckRecoveryScans.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.stuckRecoveryClears.load(std::memory_order_relaxed)));
    std::fprintf(stderr,
        "[MelonPrimeRawPerf] lock_planes "
        "subscription_mutex_acq=%llu subscription_mutex_wait_ns=%llu "
        "subscription_mutex_hold_ns=%llu subscription_mutex_max_wait_ns=%llu "
        "frame_mutex_acq=%llu frame_mutex_wait_ns=%llu "
        "frame_mutex_hold_ns=%llu frame_mutex_max_wait_ns=%llu "
        "recursive_acquisitions=%llu subscription_max_recursion_depth=%llu "
        "frame_max_recursion_depth=%llu\n",
        static_cast<unsigned long long>(stats.subscriptionMutexAcquisitions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.subscriptionMutexWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.subscriptionMutexHoldNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.subscriptionMutexMaxWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.frameMutexAcquisitions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.frameMutexWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.frameMutexHoldNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.frameMutexMaxWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.recursiveAcquisitions.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.subscriptionMaxRecursionDepth.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.frameMaxRecursionDepth.load(std::memory_order_relaxed)));

    const StagePercentiles snapshot = SummarizeStage(
        stats.stage[static_cast<uint32_t>(Stage::RawSnapshot)]);
    const StagePercentiles lateLatch = SummarizeStage(
        stats.stage[static_cast<uint32_t>(Stage::RawLateLatch)]);
    const StagePercentiles deferredDrain = SummarizeStage(
        stats.stage[static_cast<uint32_t>(Stage::RawDeferredDrain)]);
    std::fprintf(stderr,
        "[MelonPrimeRawPerf] stage_us "
        "snapshot[p50=%.1f p95=%.1f p99=%.1f max=%.1f] "
        "late_latch[p50=%.1f p95=%.1f p99=%.1f max=%.1f] "
        "deferred_drain[p50=%.1f p95=%.1f p99=%.1f max=%.1f] "
        "lock_wait_ns snapshot=%llu late=%llu deferred=%llu hidden=%llu native=%llu | "
        "raw_batch calls=%llu nonempty=%llu empty=%llu events=%llu "
        "late_delta_claims=%llu post_draw_events=%llu\n",
        snapshot.p50, snapshot.p95, snapshot.p99, snapshot.max,
        lateLatch.p50, lateLatch.p95, lateLatch.p99, lateLatch.max,
        deferredDrain.p50, deferredDrain.p95, deferredDrain.p99, deferredDrain.max,
        static_cast<unsigned long long>(stats.lockWaitBySite[static_cast<uint32_t>(LockSite::Snapshot)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.lockWaitBySite[static_cast<uint32_t>(LockSite::LateLatch)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.lockWaitBySite[static_cast<uint32_t>(LockSite::DeferredDrain)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.lockWaitBySite[static_cast<uint32_t>(LockSite::HiddenWndProc)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.lockWaitBySite[static_cast<uint32_t>(LockSite::NativeEvent)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchNonEmptyCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchEmptyCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchEvents.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.lateLatchDeltaClaims.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.postDrawEventsCaptured.load(std::memory_order_relaxed)));
    std::fprintf(stderr,
        "[MelonPrimeRawPerf] metric_contract "
        "RawSubscriptionLockWait=%llu "
        "RawSnapshot=%llu RawLateLatch=%llu RawDeferredDrain=%llu "
        "RawBatchCallCount=%llu RawBatchEventCount=%llu\n",
        static_cast<unsigned long long>(stats.subscriptionMutexWaitNs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.stage[static_cast<uint32_t>(Stage::RawSnapshot)].calls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.stage[static_cast<uint32_t>(Stage::RawLateLatch)].calls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.stage[static_cast<uint32_t>(Stage::RawDeferredDrain)].calls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(stats.rawBatchEvents.load(std::memory_order_relaxed)));
    std::fflush(stderr);
}

inline void MaybeReport() noexcept
{
    Report(false);
}

inline void ShutdownReport() noexcept
{
    if (!Enabled())
        return;
    Report(true);
}

#else

inline constexpr bool Enabled() noexcept { return false; }
inline constexpr bool IsCaptureOnly() noexcept { return false; }
enum class LockSite : uint8_t {
    Other,
    Snapshot,
    LateLatch,
    DeferredDrain,
    HiddenWndProc,
    NativeEvent,
    Count
};
enum class Stage : uint8_t {
    RawSnapshot,
    RawLateLatch,
    RawDeferredDrain,
    Count
};
enum class Metric : uint8_t {
    RawSubscriptionLockWait,
    RawSnapshot,
    RawLateLatch,
    RawDeferredDrain,
    RawBatchCallCount,
    RawBatchEventCount,
    Count
};
inline void CountGetAsyncKeyState() noexcept {}
inline void CountHiddenWindowDispatch() noexcept {}
inline void CountStuckRecovery(bool) noexcept {}
inline void RecordRawBatch(uint64_t) noexcept {}
inline void RecordLateLatchDelta(int, int) noexcept {}
inline void RecordPostDrawEvents(uint64_t) noexcept {}
enum class LockKind : uint8_t {
    Subscription,
    Frame,
};
inline void RecordRecursiveAcquisition(LockKind, uint32_t) noexcept {}
inline void MaybeReport() noexcept {}
inline void ShutdownReport() noexcept {}

class PostDrawCaptureScope {
public:
    PostDrawCaptureScope() = default;
};

class SubscriptionMutexGuard {
public:
    explicit SubscriptionMutexGuard(
        std::recursive_mutex& mutex, LockSite = LockSite::Other)
        : m_lock(mutex)
    {}

    SubscriptionMutexGuard(const SubscriptionMutexGuard&) = delete;
    SubscriptionMutexGuard& operator=(const SubscriptionMutexGuard&) = delete;

private:
    std::lock_guard<std::recursive_mutex> m_lock;
};

class FrameMutexGuard {
public:
    explicit FrameMutexGuard(
        std::recursive_mutex& mutex, LockSite = LockSite::Other)
        : m_lock(mutex)
    {}

    FrameMutexGuard(const FrameMutexGuard&) = delete;
    FrameMutexGuard& operator=(const FrameMutexGuard&) = delete;

private:
    std::lock_guard<std::recursive_mutex> m_lock;
};

class ScopedStage {
public:
    explicit ScopedStage(Stage) {}
};

class RawBufferScope {
public:
    RawBufferScope() = default;
    RawBufferScope(const RawBufferScope&) = delete;
    RawBufferScope& operator=(const RawBufferScope&) = delete;
};

#endif

} // namespace RawInputPerf
} // namespace MelonPrime

#endif // MELONPRIME_RAW_INPUT_PERF_PROBE_H
