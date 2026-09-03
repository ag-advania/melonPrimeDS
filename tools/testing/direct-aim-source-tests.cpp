// Direct-aim host source arbitration and absolute-to-relative normalization.
//
// These vectors encode the contract that keeps pen/tablet aim from jumping or
// double-counting: the first absolute sample after any boundary is a seed, and
// one capture generation admits exactly one logical source.

#include "MelonPrimeDirectAimSource.h"

#include <cstdio>
#include <cstdlib>

namespace {

using MelonPrime::DirectAimBaselineReset;
using MelonPrime::DirectAimHostSource;
using MelonPrime::DirectAimSourceArbiter;
using MelonPrime::DirectAimSourceIsAbsolute;

int g_failures = 0;

void Check(bool condition, const char* what)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
}

void CheckDelta(const DirectAimSourceArbiter::SubmitResult& result,
                bool accepted, bool seeded, int dx, int dy,
                const char* what)
{
    if (result.accepted == accepted && result.seeded == seeded
        && result.dx == dx && result.dy == dy)
        return;
    std::fprintf(stderr,
                 "FAIL: %s (accepted=%d seeded=%d dx=%d dy=%d)\n",
                 what, result.accepted ? 1 : 0, result.seeded ? 1 : 0,
                 result.dx, result.dy);
    ++g_failures;
}

// sample1=(100,100) -> (0,0); sample2=(105,97) -> (+5,-3); repeat -> (0,0)
void TestAbsoluteDelta()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 100, 100),
               true, true, 0, 0, "first absolute sample seeds without motion");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 105, 97),
               true, false, 5, -3, "second sample differences");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 105, 97),
               true, false, 0, 0, "a repeated position produces no motion");
}

// An inactive capture never produces aim.
void TestCaptureGate()
{
    DirectAimSourceArbiter arbiter;
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 50, 50),
               false, false, 0, 0, "no capture: sample rejected");
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 50, 50);
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 60, 50);
    arbiter.EndCapture();
    Check(arbiter.Authority() == DirectAimHostSource::None,
          "capture end releases the authority");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 900, 900),
               false, false, 0, 0, "after capture end: sample rejected");
}

// generation 1: (100,100); generation 2: (900,500) -> first delta is 0.
void TestGenerationSwitch()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    const uint32_t first = arbiter.Generation();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 100, 100);
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 120, 100);
    arbiter.EndCapture();
    arbiter.BeginCapture();
    Check(arbiter.Generation() != first, "a new capture advances the generation");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 900, 500),
               true, true, 0, 0, "re-entry seeds instead of jumping");
}

// QtTablet (100,100) then WinPointer (1600,900): the pre-empting source seeds.
void TestSourceSwitch()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 100, 100),
               true, true, 0, 0, "QtTablet latches first");
    Check(arbiter.Authority() == DirectAimHostSource::QtTablet,
          "QtTablet holds the authority");
    const auto pen =
        arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 7, 1600, 900);
    Check(pen.authorityChanged, "a higher-priority pen source pre-empts");
    CheckDelta(pen, true, true, 0, 0, "the pre-empting source seeds");
    Check(arbiter.Authority() == DirectAimHostSource::WinPointerPen,
          "WinPointerPen holds the authority after pre-emption");
    // The demoted route must not contribute again for this capture.
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 400, 400),
               false, false, 0, 0, "the demoted source is suppressed");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 7, 1610, 890),
               true, false, 10, -10, "the authority keeps differencing");
}

// pointer1=(100,100), pointer2=(600,400): the new pointer identity seeds.
void TestPointerIdSwitch()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 100, 100);
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 2, 600, 400),
               true, true, 0, 0, "a new pointer id seeds");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 2, 601, 399),
               true, false, 1, -1, "the new pointer then differences");
}

// One physical movement surfacing on three routes yields one logical delta.
void TestDuplicateSuppression()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 3, 200, 200);
    int32_t totalX = 0;
    int32_t totalY = 0;
    const DirectAimHostSource routes[] = {
        DirectAimHostSource::WinPointerPen,
        DirectAimHostSource::QtTablet,
        DirectAimHostSource::InjectedAbsolutePointer,
    };
    for (const DirectAimHostSource route : routes) {
        const auto result = arbiter.SubmitAbsolute(route, 3, 212, 205);
        totalX += result.dx;
        totalY += result.dy;
    }
    Check(totalX == 12 && totalY == 5,
          "three routes for one movement produce one logical delta");
}

// The relative mouse never appears in this arbiter at all: it keeps its own
// transport, and the frame projection falls back to it whenever the arbiter
// reports no motion. A still pen must therefore report a zero delta rather
// than holding the frame.
void TestStillPenYieldsNoMotion()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    Check(!DirectAimSourceIsAbsolute(arbiter.Authority()),
          "an unlatched capture claims nothing, so Raw owns the frame");

    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 500, 500);
    Check(DirectAimSourceIsAbsolute(arbiter.Authority()),
          "a pen sample latches an absolute authority");

    // A pen resting on the tablet keeps reporting the same position. Every one
    // of those frames must hand the aim frame back to the relative mouse.
    for (int i = 0; i < 8; ++i) {
        CheckDelta(
            arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 500, 500),
            true, false, 0, 0, "a still pen produces no motion");
    }
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 512, 494),
               true, false, 12, -6, "the pen resumes differencing when it moves");
}

// A self-inflicted cursor move (a clip or a warp) arrives as an injected
// absolute sample. After a baseline drop it must contribute zero, never a
// screen-sized jump.
void TestSelfCursorMoveContributesNothing()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(
        DirectAimHostSource::InjectedAbsolutePointer, 0, 100, 100);
    arbiter.DropBaseline(DirectAimBaselineReset::Lifecycle);
    CheckDelta(arbiter.SubmitAbsolute(
                   DirectAimHostSource::InjectedAbsolutePointer, 0, 1900, 1000),
               true, true, 0, 0, "the post-warp sample seeds instead of jumping");
}

// A dropped baseline seeds on re-entry rather than replaying the gap.
void TestBaselineDrop()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 300, 300);
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 310, 300);
    arbiter.DropBaseline(DirectAimBaselineReset::PointerLeave);
    Check(!arbiter.BaselineValid(), "pointer leave drops the baseline");
    Check(arbiter.Authority() == DirectAimHostSource::WinPointerPen,
          "the authority survives a baseline drop inside a capture");
    CheckDelta(arbiter.SubmitAbsolute(DirectAimHostSource::WinPointerPen, 1, 1500, 20),
               true, true, 0, 0, "re-entry seeds after a leave");
}

// Sub-pixel motion accumulates instead of being quantized away, and the
// remainder never turns into drift.
void TestSubPixelResidual()
{
    DirectAimSourceArbiter arbiter;
    arbiter.BeginCapture();
    (void)arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, 0.0, 0.0);
    int32_t total = 0;
    double x = 0.0;
    for (int i = 0; i < 10; ++i) {
        x += 0.5;
        total += arbiter.SubmitAbsolute(DirectAimHostSource::QtTablet, 1, x, 0.0).dx;
    }
    Check(total == 5, "ten half-pixel steps accumulate to exactly five");
}

} // namespace

int main()
{
    TestAbsoluteDelta();
    TestCaptureGate();
    TestGenerationSwitch();
    TestSourceSwitch();
    TestPointerIdSwitch();
    TestDuplicateSuppression();
    TestStillPenYieldsNoMotion();
    TestSelfCursorMoveContributesNothing();
    TestBaselineDrop();
    TestSubPixelResidual();

    if (g_failures != 0) {
        std::fprintf(stderr, "direct-aim source tests: %d failure(s)\n",
                     g_failures);
        return EXIT_FAILURE;
    }
    std::printf("direct-aim source tests: all checks passed\n");
    return EXIT_SUCCESS;
}
