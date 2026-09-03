/* Isolated algorithmic cost measurement for the opt-in direct-aim
   (pen/tablet) path.

   Measures the two operations the feature adds when it is enabled:
     - per input event: DirectAimSourceArbiter::SubmitAbsolute + mailbox publish
     - per guest frame: MelonPrimeThreadBridge::ConsumeDirectAimForEmu

   This is not an end-to-end measurement: it excludes Qt/Win32 ingress,
   cross-thread scheduling/cache migration, cursor policy, and frame timing.
   Do not use these numbers as end-to-end latency. The disabled configuration
   is not benchmarked here because it executes
   neither operation: the frame projection tests one cached bool and branches
   past the whole block. Verify that claim against the built object instead of
   inferring it, e.g.

     objdump -d build/release-mingw-x86_64/src/frontend/qt_sdl/CMakeFiles/\
       melonDS.dir/MelonPrimeGameInput.cpp.obj

   Build on Windows MinGW:
     c++ -O3 -std=c++20 -Isrc/frontend/qt_sdl \
       tools/perf/direct-aim-mailbox-benchmark.cpp -o direct-aim-benchmark.exe
*/

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "MelonPrimeDirectAimSource.h"
#include "MelonPrimeThreadBridge.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kEventIterations = 20'000'000;
constexpr int kFrameIterations = 20'000'000;

double NanosPerOp(Clock::duration elapsed, int iterations)
{
    return std::chrono::duration<double, std::nano>(elapsed).count()
        / static_cast<double>(iterations);
}

} // namespace

int main()
{
    MelonPrime::MelonPrimeThreadBridge bridge;
    MelonPrime::DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    bridge.PublishDirectAimSourceFromGui(
        static_cast<uint8_t>(MelonPrime::DirectAimHostSource::None));

    // Warm the latch and the baseline so the measured loop is the steady state,
    // not the once-per-capture transition.
    (void)arbiter.SubmitAbsolute(
        MelonPrime::DirectAimHostSource::WinPointerPen, 1, 1000.0, 1000.0);
    bridge.PublishDirectAimSourceFromGui(
        static_cast<uint8_t>(MelonPrime::DirectAimHostSource::WinPointerPen));

    int64_t sink = 0;

    double x = 1000.0;
    double y = 1000.0;
    const auto eventStart = Clock::now();
    for (int i = 0; i < kEventIterations; ++i) {
        // A 1000 Hz pen reporting ~1 px per report is the realistic shape.
        x += (i & 1) ? 1.0 : -1.0;
        y += (i & 2) ? 1.0 : -1.0;
        const auto result = arbiter.SubmitAbsolute(
            MelonPrime::DirectAimHostSource::WinPointerPen, 1, x, y);
        bridge.AddDirectAimDeltaFromGui(result.dx, result.dy);
        sink += result.dx;
    }
    const auto eventElapsed = Clock::now() - eventStart;

    int32_t dx = 0;
    int32_t dy = 0;
    const auto frameStart = Clock::now();
    for (int i = 0; i < kFrameIterations; ++i) {
        sink += bridge.ConsumeDirectAimForEmu(dx, dy);
        sink += dx;
    }
    const auto frameElapsed = Clock::now() - frameStart;

    std::printf("isolated algorithmic cost, direct-aim producer "
                "(arbiter+mailbox, per input event): %.2f ns\n",
                NanosPerOp(eventElapsed, kEventIterations));
    std::printf("isolated algorithmic cost, direct-aim consumer "
                "(mailbox, per guest frame): %.2f ns\n",
                NanosPerOp(frameElapsed, kFrameIterations));
    std::printf("checksum: %lld\n", static_cast<long long>(sink));
    return 0;
}
