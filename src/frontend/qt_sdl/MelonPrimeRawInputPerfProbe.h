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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#endif

namespace MelonPrime {
namespace RawInputPerf {

#if defined(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY) \
    && defined(MELONPRIME_DS) && defined(_WIN32)

struct Counters {
    std::atomic<uint64_t> mutexAcquisitions{ 0 };
    std::atomic<uint64_t> mutexWaitNs{ 0 };
    std::atomic<uint64_t> mutexHoldNs{ 0 };
    std::atomic<uint64_t> maxMutexWaitNs{ 0 };
    std::atomic<uint64_t> maxMutexHoldNs{ 0 };
    std::atomic<uint64_t> rawBufferCalls{ 0 };
    std::atomic<uint64_t> rawBufferNs{ 0 };
    std::atomic<uint64_t> maxRawBufferNs{ 0 };
    std::atomic<uint64_t> getAsyncKeyStateCalls{ 0 };
    std::atomic<uint64_t> hiddenWindowDispatches{ 0 };
    std::atomic<uint64_t> stuckRecoveryScans{ 0 };
    std::atomic<uint64_t> stuckRecoveryClears{ 0 };
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

inline void RecordMutexWait(uint64_t elapsedNs) noexcept
{
    auto& stats = Stats();
    stats.mutexAcquisitions.fetch_add(1, std::memory_order_relaxed);
    stats.mutexWaitNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxMutexWaitNs, elapsedNs);
}

inline void RecordMutexHold(uint64_t elapsedNs) noexcept
{
    auto& stats = Stats();
    stats.mutexHoldNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxMutexHoldNs, elapsedNs);
}

inline void RecordRawBuffer(uint64_t elapsedNs) noexcept
{
    auto& stats = Stats();
    stats.rawBufferCalls.fetch_add(1, std::memory_order_relaxed);
    stats.rawBufferNs.fetch_add(elapsedNs, std::memory_order_relaxed);
    AtomicMax(stats.maxRawBufferNs, elapsedNs);
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
    explicit SubscriptionMutexGuard(std::recursive_mutex& mutex)
        : m_lock(mutex, std::defer_lock)
        , m_measure(Enabled())
        , m_acquiredNs(0)
    {
        const uint64_t waitStartNs = m_measure ? NowNs() : 0;
        m_lock.lock();
        if (m_measure) {
            m_acquiredNs = NowNs();
            RecordMutexWait(m_acquiredNs - waitStartNs);
        }
    }

    ~SubscriptionMutexGuard()
    {
        if (m_measure)
            RecordMutexHold(NowNs() - m_acquiredNs);
    }

    SubscriptionMutexGuard(const SubscriptionMutexGuard&) = delete;
    SubscriptionMutexGuard& operator=(const SubscriptionMutexGuard&) = delete;

private:
    std::unique_lock<std::recursive_mutex> m_lock;
    bool m_measure;
    uint64_t m_acquiredNs;
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

inline void MaybeReport() noexcept
{
    if (!Enabled())
        return;

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
    std::fflush(stderr);
}

#else

inline constexpr bool Enabled() noexcept { return false; }
inline void CountGetAsyncKeyState() noexcept {}
inline void CountHiddenWindowDispatch() noexcept {}
inline void CountStuckRecovery(bool) noexcept {}
inline void MaybeReport() noexcept {}

class SubscriptionMutexGuard {
public:
    explicit SubscriptionMutexGuard(std::recursive_mutex& mutex)
        : m_lock(mutex)
    {}

    SubscriptionMutexGuard(const SubscriptionMutexGuard&) = delete;
    SubscriptionMutexGuard& operator=(const SubscriptionMutexGuard&) = delete;

private:
    std::lock_guard<std::recursive_mutex> m_lock;
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
