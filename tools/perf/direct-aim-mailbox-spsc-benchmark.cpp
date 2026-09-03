/* Two-thread SPSC measurement for the opt-in direct-aim mailbox.

   The GUI producer executes DirectAimSourceArbiter::SubmitAbsolute followed by
   MelonPrimeThreadBridge::AddDirectAimDeltaFromGui. The EmuThread consumer
   executes MelonPrimeThreadBridge::ConsumeDirectAimForEmu at an independent
   frame rate. This is still a synthetic mailbox workload: it does not include
   Qt event dispatch, Windows native APIs, cursor policy, or game-frame work.

   Each producer/consumer rate pair is measured for 250 ms by default. Use
   --duration-ms N for a longer run. The mailbox fields intentionally retain
   their current layout; this benchmark does not claim that false sharing has
   been fixed. Compare the same rate matrix on the target hardware before
   changing the layout.

   Build/run through the explicit CMake target:
     cmake --build <build> --target melonprime_direct_aim_mailbox_spsc_benchmark
     <build>/perf/melonprime_direct_aim_mailbox_spsc_benchmark.exe
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "MelonPrimeDirectAimSource.h"
#include "MelonPrimeThreadBridge.h"

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::duration<double, std::nano>;

constexpr uint32_t kProducerRates[] = {125, 500, 1000, 2000, 8000};
constexpr uint32_t kConsumerRates[] = {60, 120, 144, 240};
constexpr uint32_t kDefaultDurationMs = 250;

struct Percentiles
{
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

Percentiles Summarize(std::vector<double>& samples)
{
    if (samples.empty())
        return {};
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&samples](double p) {
        const std::size_t index = static_cast<std::size_t>(
            p * static_cast<double>(samples.size() - 1));
        return samples[index];
    };
    return {
        percentile(0.50),
        percentile(0.95),
        percentile(0.99),
        samples.back(),
    };
}

struct CaseResult
{
    uint64_t producerEvents = 0;
    uint64_t consumerConsumes = 0;
    uint64_t consumerNonZeroConsumes = 0;
    int64_t producerChecksum = 0;
    int64_t consumerChecksum = 0;
    double elapsedSeconds = 0.0;
    Percentiles producerNs;
    Percentiles consumerNs;
};

uint32_t ParseDurationMs(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--duration-ms") != 0)
            continue;
        const long parsed = std::strtol(argv[i + 1], nullptr, 10);
        if (parsed > 0 && parsed <= 60'000)
            return static_cast<uint32_t>(parsed);
    }
    return kDefaultDurationMs;
}

CaseResult RunCase(uint32_t producerHz,
                   uint32_t consumerHz,
                   uint32_t durationMs)
{
    MelonPrime::MelonPrimeThreadBridge bridge;
    MelonPrime::DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    bridge.PublishDirectAimSourceFromGui(static_cast<uint8_t>(
        MelonPrime::DirectAimHostSource::WinPointerPen));

    // Remove the once-per-capture seed from the producer distribution.
    (void)arbiter.SubmitAbsolute(
        MelonPrime::DirectAimHostSource::WinPointerPen, 17, 0.0, 0.0);

    const std::size_t reserveCount = static_cast<std::size_t>(
        std::max<uint32_t>(producerHz, consumerHz) * durationMs / 1000 + 32);
    std::vector<double> producerSamples;
    std::vector<double> consumerSamples;
    producerSamples.reserve(reserveCount);
    consumerSamples.reserve(reserveCount);

    std::atomic<uint32_t> ready{0};
    std::atomic_bool go{false};
    std::atomic_bool stop{false};
    Clock::time_point start;
    uint64_t producerEvents = 0;
    uint64_t consumerConsumes = 0;
    uint64_t consumerNonZeroConsumes = 0;
    int64_t producerChecksum = 0;
    int64_t consumerChecksum = 0;

    const auto producerPeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(producerHz)));
    const auto consumerPeriod = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(consumerHz)));
    const auto runDuration = std::chrono::milliseconds(durationMs);

    std::thread producer([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::yield();

        const auto deadline = start + runDuration;
        auto next = start;
        double x = 0.0;
        double y = 0.0;
        while (Clock::now() < deadline && !stop.load(std::memory_order_acquire)) {
            const auto now = Clock::now();
            if (now < next) {
                std::this_thread::sleep_until(next);
                continue;
            }

            x += (producerEvents & 1u) ? 1.0 : -1.0;
            y += (producerEvents & 2u) ? 0.5 : -0.5;
            const auto operationStart = Clock::now();
            const auto result = arbiter.SubmitAbsolute(
                MelonPrime::DirectAimHostSource::WinPointerPen,
                17,
                x,
                y);
            bridge.AddDirectAimDeltaFromGui(result.dx, result.dy);
            const auto operationEnd = Clock::now();
            producerSamples.push_back(
                Nanoseconds(operationEnd - operationStart).count());
            producerChecksum += result.dx + result.dy;
            ++producerEvents;

            next += producerPeriod;
            if (next <= operationEnd)
                next = operationEnd + producerPeriod;
        }
        stop.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire))
            std::this_thread::yield();

        const auto deadline = start + runDuration;
        auto next = start;
        while (Clock::now() < deadline && !stop.load(std::memory_order_acquire)) {
            const auto now = Clock::now();
            if (now < next) {
                std::this_thread::sleep_until(next);
                continue;
            }

            int32_t dx = 0;
            int32_t dy = 0;
            const auto operationStart = Clock::now();
            const auto source = bridge.ConsumeDirectAimForEmu(dx, dy);
            const auto operationEnd = Clock::now();
            consumerSamples.push_back(
                Nanoseconds(operationEnd - operationStart).count());
            consumerChecksum += static_cast<int64_t>(source) + dx + dy;
            if ((dx | dy) != 0)
                ++consumerNonZeroConsumes;
            ++consumerConsumes;

            next += consumerPeriod;
            if (next <= operationEnd)
                next = operationEnd + consumerPeriod;
        }
    });

    while (ready.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();
    start = Clock::now();
    go.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    const auto end = Clock::now();
    CaseResult result;
    result.producerEvents = producerEvents;
    result.consumerConsumes = consumerConsumes;
    result.consumerNonZeroConsumes = consumerNonZeroConsumes;
    result.producerChecksum = producerChecksum;
    result.consumerChecksum = consumerChecksum;
    result.elapsedSeconds = std::chrono::duration<double>(end - start).count();
    result.producerNs = Summarize(producerSamples);
    result.consumerNs = Summarize(consumerSamples);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    const uint32_t durationMs = ParseDurationMs(argc, argv);
    std::printf(
        "direct-aim SPSC mailbox benchmark: duration_ms=%u "
        "layout=current-adjacent-fields-no-mitigation\n",
        durationMs);
    std::printf(
        "columns: producer_hz consumer_hz producer_events consumer_consumes "
        "consumer_nonzero producer_events_per_sec consumer_consumes_per_sec "
        "producer_ns[event]{p50,p95,p99,max} "
        "consumer_ns[consume]{p50,p95,p99,max}\n");

    for (const uint32_t producerHz : kProducerRates) {
        for (const uint32_t consumerHz : kConsumerRates) {
            const CaseResult result = RunCase(producerHz, consumerHz, durationMs);
            const double seconds = result.elapsedSeconds > 0.0
                ? result.elapsedSeconds : 1.0;
            std::printf(
                "producer_hz=%u consumer_hz=%u producer_events=%llu "
                "consumer_consumes=%llu consumer_nonzero=%llu "
                "producer_events_per_sec=%.1f consumer_consumes_per_sec=%.1f "
                "producer_ns[event]{p50=%.2f p95=%.2f p99=%.2f max=%.2f} "
                "consumer_ns[consume]{p50=%.2f p95=%.2f p99=%.2f max=%.2f}\n",
                producerHz,
                consumerHz,
                static_cast<unsigned long long>(result.producerEvents),
                static_cast<unsigned long long>(result.consumerConsumes),
                static_cast<unsigned long long>(result.consumerNonZeroConsumes),
                static_cast<double>(result.producerEvents) / seconds,
                static_cast<double>(result.consumerConsumes) / seconds,
                result.producerNs.p50,
                result.producerNs.p95,
                result.producerNs.p99,
                result.producerNs.max,
                result.consumerNs.p50,
                result.consumerNs.p95,
                result.consumerNs.p99,
                result.consumerNs.max);
        }
    }
    return EXIT_SUCCESS;
}
