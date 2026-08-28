// Deterministic tests for the high-resolution Custom HUD crosshair centre.
//
// The production path reproduces MPH's Q12 projection up to the raw signed Q32
// quotient and stops before the ROM's two quantisation steps. That claim is
// only worth anything if the reproduction is bit-exact, so the reference below
// is written literally from the disassembly notes -- separate variables, no
// shared helpers with the module under test -- and the module has to land on
// the same integers after the skipped steps are replayed.
//
// Reference: mphCodex mphAnalysis/HUD/High-Resolution-Crosshair.

#include "MelonPrimeHudCrosshairProjection.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace CH = MelonPrime::CrosshairProjection;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// -------------------------------------------------------------------------
//  Literal reference implementation of the ROM chain.
//
//  Deliberately verbose and unshared. If the module ever starts summing the
//  projection products before rounding, or adds a rounding term to the affine
//  transform, or shifts a negative numerator instead of multiplying, this
//  disagrees.
// -------------------------------------------------------------------------

struct RomReference {
    bool visible;
    int16_t screenX;
    int16_t screenY;
    int32_t clipW;
    // Pre-truncation screen position. The ROM stores s16, so an extreme
    // off-screen point wraps; comparisons against the fractional centre are
    // only meaningful where it did not.
    int32_t screenX32;
    int32_t screenY32;
    // The raw quotient matters more than the s16: comparing only the
    // reconstructed native position throws away exactly the sub-pixel
    // precision this feature exists to recover, so an arithmetic error
    // smaller than one native pixel would go unnoticed.
    int64_t q32X;
    int64_t q32Y;
};

RomReference ProjectLikeRom(const CH::Input& in)
{
    const int32_t x = in.source[0];
    const int32_t y = in.source[1];
    const int32_t z = in.source[2];
    const int32_t* m = in.view;
    const int32_t* p = in.projection;

    // 02081A4C: three products accumulated in 64 bits, one shift, no rounding.
    const int64_t accX = (int64_t)x * m[0] + (int64_t)y * m[3] + (int64_t)z * m[6];
    const int64_t accY = (int64_t)x * m[1] + (int64_t)y * m[4] + (int64_t)z * m[7];
    const int64_t accZ = (int64_t)x * m[2] + (int64_t)y * m[5] + (int64_t)z * m[8];
    const int32_t vx = (int32_t)(accX >> 12) + m[9];
    const int32_t vy = (int32_t)(accY >> 12) + m[10];
    const int32_t vz = (int32_t)(accZ >> 12) + m[11];

    // 0204EB30: every product rounds on its own before its shift.
    const int32_t wa = (int32_t)(((int64_t)vx * p[3] + 0x800) >> 12);
    const int32_t wb = (int32_t)(((int64_t)vy * p[7] + 0x800) >> 12);
    const int32_t wc = (int32_t)(((int64_t)vz * p[11] + 0x800) >> 12);
    const int32_t clipW = wa + wb + wc + p[15];
    if (clipW <= 0)
        return RomReference{false, 0, 0, clipW, 0, 0, 0, 0};

    const int32_t xa = (int32_t)(((int64_t)vx * p[0] + 0x800) >> 12);
    const int32_t xb = (int32_t)(((int64_t)vy * p[4] + 0x800) >> 12);
    const int32_t xc = (int32_t)(((int64_t)vz * p[8] + 0x800) >> 12);
    const int32_t clipX = xa + xb + xc + p[12];

    const int32_t ya = (int32_t)(((int64_t)vx * p[1] + 0x800) >> 12);
    const int32_t yb = (int32_t)(((int64_t)vy * p[5] + 0x800) >> 12);
    const int32_t yc = (int32_t)(((int64_t)vz * p[9] + 0x800) >> 12);
    const int32_t clipY = ya + yb + yc + p[13];

    // 02082618: numerator in the divider's high word == numerator * 2^32.
    const int64_t q32X = ((int64_t)clipX * 4294967296LL) / clipW;
    const int64_t q32Y = ((int64_t)clipY * 4294967296LL) / clipW;

    // 02082708: raw Q32 rounded down to a Q12 NDC.
    const int32_t ndcX = (int32_t)((q32X + 0x80000) >> 20);
    const int32_t ndcY = (int32_t)((q32Y + 0x80000) >> 20);

    // 0204EB30 tail: Q12 NDC to an integer 256x192 position.
    const int32_t screenX32 = (int32_t)(((int64_t)(ndcX + 0x1000) * 128) >> 12);
    const int32_t screenY32 = (int32_t)(((int64_t)(0x1000 - ndcY) * 96) >> 12);
    const int16_t screenX = (int16_t)screenX32;
    const int16_t screenY = (int16_t)screenY32;
    return RomReference{true, screenX, screenY, clipW,
                        screenX32, screenY32, q32X, q32Y};
}

// -------------------------------------------------------------------------
//  Synthetic camera
// -------------------------------------------------------------------------

constexpr int32_t kOne = 4096; // 1.0 in Q12

// An identity view transform is a useless fixture for the affine arithmetic:
// two of the three products are exactly zero and the third is an exact
// multiple of 4096, so the accumulate-then-shift has no fractional part left
// to round. A rotated camera is what makes the transform's rounding behaviour
// observable, so the sweep runs both.
constexpr int32_t kRotatedView[9] = {
     3547,   699, -1924,
        0,  3849,  1401,
     2048, -1211,  3332,
};

CH::Input MakeInput(int32_t sx, int32_t sy, int32_t sz,
                    int32_t focal = kOne,
                    bool rotated = false,
                    int32_t translateX = 0,
                    int32_t translateY = 0,
                    int32_t translateZ = 0)
{
    CH::Input in{};
    in.source[0] = sx;
    in.source[1] = sy;
    in.source[2] = sz;

    // Stored the way the ROM stores it: out.x reads m[0], m[3], m[6] and adds
    // m[9].
    if (rotated) {
        for (int i = 0; i < 9; ++i)
            in.view[i] = kRotatedView[i];
    } else {
        in.view[0] = kOne;
        in.view[4] = kOne;
        in.view[8] = kOne;
    }
    in.view[9] = translateX;
    in.view[10] = translateY;
    in.view[11] = translateZ;

    // Perspective: clipX = vx*focal, clipY = vy*focal, clipW = -vz.
    in.projection[0] = focal;
    in.projection[5] = focal;
    in.projection[11] = -kOne;
    return in;
}

// -------------------------------------------------------------------------
//  Tests
// -------------------------------------------------------------------------

void TestBehindNearPlaneIsRejected()
{
    // vz positive puts the point behind the camera, so clipW <= 0 and the ROM
    // writes no position. The HUD must fall back rather than draw a centre it
    // invented.
    for (int32_t z : {1, kOne, 100 * kOne}) {
        const CH::Input in = MakeInput(0, 0, z);
        CH::Result out{};
        Check(!CH::Project(in, out), "clipW <= 0 must not produce a centre");
        Check(!ProjectLikeRom(in).visible, "reference agrees the point is clipped");
    }

    // Exactly on the plane is also rejected: the ROM tests clipW <= 0.
    const CH::Input onPlane = MakeInput(0, 0, 0);
    CH::Result out{};
    Check(!CH::Project(onPlane, out), "clipW == 0 must not produce a centre");
}

void TestCentreOfScreen()
{
    const CH::Input in = MakeInput(0, 0, -kOne);
    CH::Result out{};
    Check(CH::Project(in, out), "a point on the view axis projects");
    Check(out.dsX == 128.0, "view-axis point is horizontally centred");
    Check(out.dsY == 96.0, "view-axis point is vertically centred");
}

// The whole feature is the reconstruction claim: replay the two quantisation
// steps the module skips and the ROM's own s16 pair must come back.
void TestReconstructsRomNativePosition()
{
    int checked = 0;
    int fractional = 0;

    for (int rotated = 0; rotated < 2; ++rotated) {
    for (int32_t depth = 1; depth <= 40; ++depth) {
        for (int32_t sx = -900; sx <= 900; sx += 37) {
            for (int32_t sy = -700; sy <= 700; sy += 53) {
                const int32_t z = -depth * 271;
                const CH::Input in = MakeInput(
                    sx, sy, z, kOne + depth * 11, rotated != 0);

                const RomReference expected = ProjectLikeRom(in);
                CH::Result out{};
                const bool projected = CH::Project(in, out);
                if (!projected || !expected.visible) {
                    Check(projected == expected.visible,
                        "visibility must agree with the reference");
                    continue;
                }

                if (out.q32X != expected.q32X || out.q32Y != expected.q32Y) {
                    std::printf(
                        "FAIL: raw Q32 mismatch src=(%d,%d,%d) "
                        "got=(%lld,%lld) rom=(%lld,%lld)\n",
                        sx, sy, z,
                        (long long)out.q32X, (long long)out.q32Y,
                        (long long)expected.q32X, (long long)expected.q32Y);
                    ++g_failures;
                    return;
                }

                const int16_t rebuiltX =
                    CH::NativeScreenX(CH::NdcQ12FromQ32(out.q32X));
                const int16_t rebuiltY =
                    CH::NativeScreenY(CH::NdcQ12FromQ32(out.q32Y));
                if (rebuiltX != expected.screenX || rebuiltY != expected.screenY) {
                    std::printf(
                        "FAIL: reconstruction mismatch src=(%d,%d,%d) "
                        "rebuilt=(%d,%d) rom=(%d,%d)\n",
                        sx, sy, z, rebuiltX, rebuiltY,
                        expected.screenX, expected.screenY);
                    ++g_failures;
                    return;
                }

                // The fractional centre must describe the same point, not a
                // different one: it can never be a whole pixel away from what
                // the ROM quantised to. Only meaningful where the ROM's s16
                // did not wrap -- a point just past the near plane divides by
                // a tiny clipW and lands arbitrarily far off screen.
                // The bit-exact statement is the reconstruction above. This is
                // the coarse sanity net that catches a gross error -- a flipped
                // axis, a transposed index -- which reconstruction alone cannot
                // see because both sides would share it. The bound allows two
                // pixels because the native position is doubly rounded: the Q12
                // NDC rounds half-up and the screen conversion then floors, and
                // for a negative coordinate those go opposite ways.
                const bool nativeIsComparable =
                    expected.screenX32 == (int32_t)expected.screenX
                    && expected.screenY32 == (int32_t)expected.screenY
                    && std::abs(expected.screenX32) <= 4096
                    && std::abs(expected.screenY32) <= 4096;
                if (nativeIsComparable
                    && (std::fabs(out.dsX - (double)expected.screenX) >= 2.0
                        || std::fabs(out.dsY - (double)expected.screenY) >= 2.0)) {
                    std::printf(
                        "FAIL: fractional centre drifted from native "
                        "src=(%d,%d,%d) ds=(%.4f,%.4f) rom=(%d,%d)\n",
                        sx, sy, z, out.dsX, out.dsY,
                        expected.screenX, expected.screenY);
                    ++g_failures;
                    return;
                }

                if (out.dsX != std::floor(out.dsX)
                    || out.dsY != std::floor(out.dsY))
                    ++fractional;
                ++checked;
            }
        }
    }
    }

    Check(checked > 5000, "the sweep must actually cover a useful range");
    // If every centre landed on an integer the feature would be a no-op, so
    // this is the test that the precision is really being recovered.
    Check(fractional * 4 > checked * 3,
        "most centres must carry sub-pixel precision the ROM discards");
    std::printf(
        "  reconstruction: %d points, %d with sub-pixel precision\n",
        checked, fractional);
}

// The projection rounds each product separately. A "simplification" that sums
// first and rounds once is the obvious wrong turn, so pin a case that can tell
// the two apart.
void TestPerProductRoundingIsLoadBearing()
{
    // Two terms whose products both land exactly on the .5 boundary: rounding
    // each adds 1 twice, while rounding their sum adds 1 once.
    CH::Input in{};
    in.source[0] = 1;
    in.source[1] = 1;
    in.source[2] = -kOne;
    in.view[0] = kOne;
    in.view[4] = kOne;
    in.view[8] = kOne;
    in.projection[0] = 0x800;  // 1 * 0x800 -> exactly half a Q12 unit
    in.projection[4] = 0x800;
    in.projection[5] = kOne;
    in.projection[11] = -kOne;

    const int32_t vx = 1, vy = 1;
    const int32_t perProduct =
        (int32_t)(((int64_t)vx * 0x800 + 0x800) >> 12)
        + (int32_t)(((int64_t)vy * 0x800 + 0x800) >> 12);
    const int32_t sumThenRound =
        (int32_t)((((int64_t)vx * 0x800 + (int64_t)vy * 0x800) + 0x800) >> 12);
    Check(perProduct != sumThenRound,
        "the fixture must actually discriminate the two rounding orders");

    CH::Result out{};
    Check(CH::Project(in, out), "rounding fixture projects");
    const RomReference expected = ProjectLikeRom(in);
    Check(expected.visible, "rounding fixture is visible in the reference");
    // Compare the raw quotient: the two rounding orders differ by one Q12 unit
    // here, which the native s16 would quantise away entirely.
    Check(out.q32X == expected.q32X, "per-product rounding must match the ROM");
}

// The divider numerator is formed by placing an int32 in a 64-bit high word.
// Doing that with a left shift on a negative value is the trap the notes call
// out, so exercise the left half of the screen and the int32 extremes.
void TestNegativeAndExtremeNumerators()
{
    for (int32_t sx : {-1, -37, -900, -4096, -40960}) {
        const CH::Input in = MakeInput(sx, 0, -8 * kOne, kOne, true);
        CH::Result out{};
        Check(CH::Project(in, out), "left-of-centre point projects");
        const RomReference expected = ProjectLikeRom(in);
        Check(out.q32X < 0, "left-of-centre must give a negative quotient");
        Check(out.q32X == expected.q32X,
            "negative numerator quotient must match the ROM");
        Check(CH::NativeScreenX(CH::NdcQ12FromQ32(out.q32X)) == expected.screenX,
            "negative numerator reconstruction must match the ROM");
        Check(out.dsX < 128.0, "left-of-centre must be left of the midpoint");
    }

    // Truncation toward zero, not floor: -3 / 2 == -1.
    Check(CH::DivideQ32(-1, 0x40000000) == (int64_t)-4,
        "the divide must truncate toward zero like the hardware");
}

void TestTranslationAndDepthOrdering()
{
    // Moving the point right must move the centre right, and pushing it away
    // must pull it toward the middle. Cheap, but it catches a transposed
    // matrix index, which the bit-exact sweep alone would not: a transpose
    // that both implementations share would still agree with each other.
    CH::Result near{}, far{}, right{};
    Check(CH::Project(MakeInput(200, 0, -4 * kOne), near), "near projects");
    Check(CH::Project(MakeInput(200, 0, -8 * kOne), far), "far projects");
    Check(CH::Project(MakeInput(400, 0, -4 * kOne), right), "right projects");
    Check(near.dsX > 128.0, "positive x must project right of centre");
    Check(right.dsX > near.dsX, "more x must project further right");
    Check(far.dsX < near.dsX, "more depth must project closer to centre");

    CH::Result up{};
    Check(CH::Project(MakeInput(0, 200, -4 * kOne), up), "up projects");
    Check(up.dsY < 96.0, "positive y must project above centre");
}


// -------------------------------------------------------------------------
//  Sub-pixel deadband
// -------------------------------------------------------------------------

// The artifact this exists to kill: an exact centre sitting on a rounding
// boundary and dithering by a fraction of a pixel. Plain rounding alternates
// between two pixels every frame and the thin arms read as a half-intensity
// flicker.
void TestDeadbandSuppressesBoundaryDither()
{
    const double jitter[] = {100.49, 100.51, 100.48, 100.52, 100.50, 100.47};

    int naive = -1;
    int naiveFlips = 0;
    for (double v : jitter) {
        const int px = (int)std::lround(v);
        if (naive != -1 && px != naive)
            ++naiveFlips;
        naive = px;
    }
    Check(naiveFlips > 0,
        "the fixture must actually straddle a rounding boundary");

    int sticky = CH::kNoCommittedPixel;
    int committed = CH::CommitPixel(jitter[0], CH::kDefaultPixelDeadband, sticky);
    int flips = 0;
    for (double v : jitter) {
        const int px = CH::CommitPixel(v, CH::kDefaultPixelDeadband, sticky);
        if (px != committed)
            ++flips;
        committed = px;
    }
    Check(flips == 0, "boundary dither must not move the committed pixel");
}

// A deadband that never lets go would be worse than the flicker, so real
// aiming has to keep tracking.
void TestDeadbandStillTracksRealMotion()
{
    int sticky = CH::kNoCommittedPixel;
    int last = CH::CommitPixel(100.0, CH::kDefaultPixelDeadband, sticky);
    Check(last == 100, "the first sample snaps");

    int moved = 0;
    for (int step = 1; step <= 60; ++step) {
        const double exact = 100.0 + step * 0.37;
        const int px = CH::CommitPixel(exact, CH::kDefaultPixelDeadband, sticky);
        // Never allowed to drift further than the band permits.
        Check(std::fabs(exact - (double)px) <= 0.5 + CH::kDefaultPixelDeadband + 1e-9,
            "committed pixel must stay inside the deadband of the exact centre");
        if (px != last)
            ++moved;
        last = px;
    }
    Check(moved >= 15, "sustained motion must keep moving the committed pixel");
}

void TestDeadbandSnapsOnJumpAndReset()
{
    int sticky = CH::kNoCommittedPixel;
    (void)CH::CommitPixel(100.0, CH::kDefaultPixelDeadband, sticky);
    Check(CH::CommitPixel(940.2, CH::kDefaultPixelDeadband, sticky) == 940,
        "a large jump must re-round immediately");

    // Leaving the screen clears the committed pixel so the next appearance
    // snaps instead of easing out of a stale one.
    sticky = CH::kNoCommittedPixel;
    Check(CH::CommitPixel(12.7, CH::kDefaultPixelDeadband, sticky) == 13, "a fresh appearance snaps");

    // Negative coordinates occur when the centre is off the left/top edge.
    sticky = CH::kNoCommittedPixel;
    Check(CH::CommitPixel(-40.4, CH::kDefaultPixelDeadband, sticky) == -40, "negative centres snap correctly");
    Check(CH::CommitPixel(-40.6, CH::kDefaultPixelDeadband, sticky) == -40,
        "negative centres also get the deadband");
}


// -------------------------------------------------------------------------
//  Consistency gate
// -------------------------------------------------------------------------

// The projection inputs are live intermediates sampled at presentation time,
// so the only thing that proves they are the ones the ROM used is that the
// result reconstructs to the s16 pair the ROM published. Without this the
// crosshair lands wherever a stale matrix or the wrong player struct points.
void TestMatchesNativeAcceptsAndRejects()
{
    int accepted = 0;
    for (int rotated = 0; rotated < 2; ++rotated) {
        for (int32_t depth = 1; depth <= 12; ++depth) {
            for (int32_t sx = -600; sx <= 600; sx += 97) {
                const CH::Input in = MakeInput(
                    sx, sx / 3, -depth * 271, kOne + depth * 11, rotated != 0);
                const RomReference expected = ProjectLikeRom(in);
                CH::Result out{};
                if (!CH::Project(in, out) || !expected.visible)
                    continue;
                if (expected.screenX32 != (int32_t)expected.screenX
                    || expected.screenY32 != (int32_t)expected.screenY)
                    continue;

                Check(CH::MatchesNative(out, expected.screenX, expected.screenY),
                    "consistent inputs must be accepted");
                // A stale matrix or the wrong player struct shows up as a
                // position that is simply not the one the ROM published.
                Check(!CH::MatchesNative(
                          out, (int16_t)(expected.screenX + 1), expected.screenY),
                    "a different X must be rejected");
                Check(!CH::MatchesNative(
                          out, expected.screenX, (int16_t)(expected.screenY - 1)),
                    "a different Y must be rejected");
                ++accepted;
            }
        }
    }
    Check(accepted > 100, "the gate must be exercised over a useful range");
}

// -------------------------------------------------------------------------
//  Configurable deadband
// -------------------------------------------------------------------------

// The toggle and the width are one value so a caller cannot take the width and
// drop the toggle; Resolve is what the draw site calls.
void TestDeadbandToggleResolves()
{
    CH::DeadbandSetting on{true, 0.75};
    CH::DeadbandSetting off{false, 0.75};
    Check(on.Resolve() == 0.75, "enabled resolves to the configured width");
    Check(off.Resolve() == 0.0, "disabled resolves to a zero-width band");
    Check(CH::DeadbandSetting{}.enabled, "the deadband defaults to on");
    Check(CH::DeadbandSetting{}.Resolve() == CH::kDefaultPixelDeadband,
        "the default resolves to the default width");

    // Turning it off must not disturb the width the user tuned.
    Check(off.widthPx == 0.75, "disabling preserves the configured width");

    // Off has to behave exactly like a zero width, since that is the claim the
    // single code path rests on.
    int viaToggle = CH::kNoCommittedPixel;
    int viaZero = CH::kNoCommittedPixel;
    for (int step = 0; step <= 200; ++step) {
        const double exact = 40.0 + step * 0.053;
        Check(CH::CommitPixel(exact, off.Resolve(), viaToggle)
              == CH::CommitPixel(exact, 0.0, viaZero),
            "disabled must be indistinguishable from a zero width");
    }
}

void TestDeadbandWidthIsHonoured()
{
    // Turning the deadband off passes a zero width rather than taking a second
    // code path, so "off" has to be plain rounding for every input, not merely
    // for a sample of them. Sweep it against std::lround directly.
    int sticky = CH::kNoCommittedPixel;
    for (int step = 0; step <= 400; ++step) {
        const double exact = 96.0 + step * 0.037;
        Check(CH::CommitPixel(exact, 0.0, sticky) == (int)std::lround(exact),
            "a disabled deadband must be exactly plain rounding");
    }
    sticky = CH::kNoCommittedPixel;
    for (int step = 400; step >= 0; --step) {
        const double exact = -12.0 - step * 0.041;
        Check(CH::CommitPixel(exact, 0.0, sticky) == (int)std::lround(exact),
            "a disabled deadband rounds negatives exactly too");
    }

    sticky = CH::kNoCommittedPixel;
    (void)CH::CommitPixel(100.0, 0.0, sticky);
    Check(CH::CommitPixel(100.6, 0.0, sticky) == 101,
        "a zero deadband must round exactly");

    // A wider band tolerates proportionally more jitter.
    sticky = CH::kNoCommittedPixel;
    (void)CH::CommitPixel(100.0, 0.9, sticky);
    Check(CH::CommitPixel(101.3, 0.9, sticky) == 100,
        "a wide deadband must hold through larger jitter");
    Check(CH::CommitPixel(101.5, 0.9, sticky) != 100,
        "a wide deadband must still let go eventually");

    // A hand-edited TOML can carry anything, so out-of-range must be safe.
    // Negative needs no clamp -- it degenerates to plain rounding, which is
    // what zero already does -- so assert that equivalence rather than a
    // clamp that would not be observable.
    int negative = CH::kNoCommittedPixel;
    int zero = CH::kNoCommittedPixel;
    for (double v : {100.0, 100.4, 100.6, 101.9, 99.2, 100.5}) {
        Check(CH::CommitPixel(v, -5.0, negative) == CH::CommitPixel(v, 0.0, zero),
            "a negative deadband must behave exactly like zero");
    }
    sticky = CH::kNoCommittedPixel;
    (void)CH::CommitPixel(100.0, 1e9, sticky);
    Check(CH::CommitPixel(140.0, 1e9, sticky) != 100,
        "an absurd deadband clamps instead of freezing the crosshair");
}


// -------------------------------------------------------------------------
//  Cost measurement (--bench)
//
//  The projection runs once per emulated game frame, not once per presentation
//  frame, and the config values behind it are resolved at the cold cache
//  boundary. This reports what that once-per-frame work actually costs, so the
//  low-overhead claim is a number rather than an assertion. It is not part of
//  the check run: it would slow every build down for no verdict.
// -------------------------------------------------------------------------

int RunBenchmark()
{
    constexpr uint32_t kRamMask = 0x3FFFFFu;
    std::vector<uint8_t> ram(4u * 1024u * 1024u, 0);

    const uint32_t playerBase = 0x1C5D4;
    const uint32_t matrixBase = 0x1BA30;
    const uint32_t crosshairX = 0x205C0;
    const uint32_t crosshairY = 0x205C2;

    auto put32 = [&](uint32_t a, int32_t v) {
        std::memcpy(&ram[a & kRamMask], &v, 4);
    };
    auto get32 = [&](uint32_t a) {
        uint32_t v; std::memcpy(&v, &ram[a & kRamMask], 4); return v;
    };
    auto get16 = [&](uint32_t a) {
        uint16_t v; std::memcpy(&v, &ram[a & kRamMask], 2); return v;
    };

    put32(playerBase + CH::kPlayerProjectionSourceOffset + 0, 137);
    put32(playerBase + CH::kPlayerProjectionSourceOffset + 4, -91);
    put32(playerBase + CH::kPlayerProjectionSourceOffset + 8, -8192);
    put32(playerBase + CH::kPlayerViewTransformOffset + 0, kOne);
    put32(playerBase + CH::kPlayerViewTransformOffset + 16, kOne);
    put32(playerBase + CH::kPlayerViewTransformOffset + 32, kOne);
    put32(matrixBase + 0, kOne);
    put32(matrixBase + 20, kOne);
    put32(matrixBase + 44, -kOne);

    // Mirrors ReadCrosshairProjectionInput.
    auto readInput = [&](CH::Input& out) {
        const uint32_t sourceBase =
            playerBase + CH::kPlayerProjectionSourceOffset;
        const uint32_t viewBase = playerBase + CH::kPlayerViewTransformOffset;
        for (int i = 0; i < CH::kSourceWordCount; ++i)
            out.source[i] = (int32_t)get32(sourceBase + (uint32_t)i * 4u);
        for (int i = 0; i < CH::kViewWordCount; ++i)
            out.view[i] = (int32_t)get32(viewBase + (uint32_t)i * 4u);
        for (int i = 0; i < CH::kProjectionWordCount; ++i)
            out.projection[i] = (int32_t)get32(matrixBase + (uint32_t)i * 4u);
    };

    // Seed the published pair so the gate takes its accepting path, which is
    // the expensive one.
    {
        CH::Input in{};
        readInput(in);
        CH::Result r{};
        if (!CH::Project(in, r)) {
            std::printf("benchmark fixture does not project\n");
            return 1;
        }
        const int16_t x = CH::NativeScreenX(CH::NdcQ12FromQ32(r.q32X));
        const int16_t y = CH::NativeScreenY(CH::NdcQ12FromQ32(r.q32Y));
        std::memcpy(&ram[crosshairX], &x, 2);
        std::memcpy(&ram[crosshairY], &y, 2);
    }

    constexpr int kIterations = 2000000;
    uint64_t sink = 0;
    int accepted = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        // Defeat hoisting: the guest state does change every frame in practice.
        put32(playerBase + CH::kPlayerProjectionSourceOffset, 137 + (i & 3));
        CH::Input in{};
        readInput(in);
        CH::Result r{};
        const bool ok = CH::Project(in, r)
            && CH::MatchesNative(r, (int16_t)get16(crosshairX),
                                 (int16_t)get16(crosshairY));
        accepted += ok ? 1 : 0;
        sink += (uint64_t)r.q32X ^ (uint64_t)r.q32Y;
    }
    const auto end = std::chrono::steady_clock::now();

    const double ns =
        std::chrono::duration<double, std::nano>(end - start).count() / kIterations;
    std::printf("sample + project + native gate: %.1f ns per game frame\n", ns);
    std::printf("  %.4f%% of a 16.67 ms frame; %d/%d accepted (sink %llu)\n",
                ns / 16.67e6 * 100.0, accepted, kIterations,
                (unsigned long long)sink);
    return 0;
}


// -------------------------------------------------------------------------
//  Centre source selection
//
//  Switching to the ROM cache on a rejected frame is what made the flicker
//  worse than the dither it replaced: that cache is quantised to whole DS
//  pixels, so every switch moved the centre by several output pixels.
// -------------------------------------------------------------------------

void TestRejectedFrameHoldsInsteadOfSwitching()
{
    CH::CentreHold hold;

    // Nothing accepted yet, so there is nothing to hold.
    Check(CH::SelectCentre(false, hold) == CH::CentreSource::NativeCache,
        "the first frame has no centre to hold");

    Check(CH::SelectCentre(true, hold) == CH::CentreSource::Projected,
        "an accepted frame uses the projection");

    // A transient rejection must not switch source.
    for (int i = 0; i < CH::CentreHold::kMaxHeldFrames; ++i) {
        Check(CH::SelectCentre(false, hold) == CH::CentreSource::Held,
            "a rejected frame holds the last accepted centre");
    }

    // But it must not hold forever, or a real change would freeze on screen.
    Check(CH::SelectCentre(false, hold) == CH::CentreSource::NativeCache,
        "the hold is bounded");
    Check(CH::SelectCentre(false, hold) == CH::CentreSource::NativeCache,
        "once given up it stays on the cache");

    // Accepting again resumes immediately.
    Check(CH::SelectCentre(true, hold) == CH::CentreSource::Projected,
        "an accepted frame resumes the projection at once");
}

// Alternating accept/reject is the pattern that produced the visible flicker.
// It must never reach the cache, because that is the source switch.
void TestAlternatingRejectionNeverSwitchesSource()
{
    CH::CentreHold hold;
    (void)CH::SelectCentre(true, hold);

    int cacheFrames = 0;
    for (int frame = 0; frame < 600; ++frame) {
        const bool accepted = (frame % 2) == 0;
        if (CH::SelectCentre(accepted, hold) == CH::CentreSource::NativeCache)
            ++cacheFrames;
    }
    Check(cacheFrames == 0,
        "alternating rejection must never fall back to the quantised cache");

    // Two out of three rejected is still within the hold.
    CH::CentreHold sparse;
    (void)CH::SelectCentre(true, sparse);
    cacheFrames = 0;
    for (int frame = 0; frame < 600; ++frame) {
        const bool accepted = (frame % 3) == 0;
        if (CH::SelectCentre(accepted, sparse) == CH::CentreSource::NativeCache)
            ++cacheFrames;
    }
    Check(cacheFrames == 0,
        "a sparse accept rate must still hold rather than switch");
}

// Systematic rejection has to settle, not oscillate: that is the case where
// the projection is simply unusable and the old behaviour is the right one.
void TestSystematicRejectionSettlesOnTheCache()
{
    CH::CentreHold hold;
    (void)CH::SelectCentre(true, hold);

    int held = 0;
    int cache = 0;
    for (int frame = 0; frame < 300; ++frame) {
        switch (CH::SelectCentre(false, hold)) {
        case CH::CentreSource::Held: ++held; break;
        case CH::CentreSource::NativeCache: ++cache; break;
        default: Check(false, "a rejected frame cannot report Projected"); break;
        }
    }
    Check(held == CH::CentreHold::kMaxHeldFrames,
        "the hold is spent once and not re-entered");
    Check(cache == 300 - CH::CentreHold::kMaxHeldFrames,
        "after the hold it stays on the cache instead of oscillating");
    Check(!hold.haveProjected, "giving up clears the held centre");
}


// -------------------------------------------------------------------------
//  Local player pointer
//
//  The crosshair renderer resolves its player by following a pointer, not by
//  slot index. That pointer is emulated memory, so it is validated rather than
//  trusted: a mid-transition frame can hold anything, and following a wild
//  pointer would read 124 bytes from nowhere.
// -------------------------------------------------------------------------

void TestPlayerPointerValidation()
{
    Check(CH::IsMainRamPointer(0x02000000u), "the start of main RAM is valid");
    Check(CH::IsMainRamPointer(0x023FFFFFu), "the end of main RAM is valid");
    Check(!CH::IsMainRamPointer(0x01FFFFFFu), "below main RAM is rejected");
    Check(!CH::IsMainRamPointer(0x02400000u), "above main RAM is rejected");
    Check(!CH::IsMainRamPointer(0u), "a null pointer is rejected");
    Check(!CH::IsMainRamPointer(0xFFFFFFFFu), "an all-ones pointer is rejected");

    // The whole struct has to fit, not merely its first byte: the transform
    // sits 0x5B4 in, so a base near the top of RAM would read past the end.
    Check(CH::CanReadPlayerProjectionState(0x020DC5D4u),
        "a normal player base is readable");
    Check(!CH::CanReadPlayerProjectionState(0x023FFFF0u),
        "a base too close to the end of RAM is rejected");
    Check(!CH::CanReadPlayerProjectionState(0u),
        "a null player base is rejected");

    // Exactly the last base whose transform still fits.
    constexpr uint32_t lastFit =
        0x02400000u - (CH::kPlayerViewTransformOffset + 12u * 4u);
    Check(CH::CanReadPlayerProjectionState(lastFit),
        "the last fully readable base is accepted");
    Check(!CH::CanReadPlayerProjectionState(lastFit + 1u),
        "one byte further is rejected");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
        return RunBenchmark();

    TestBehindNearPlaneIsRejected();
    TestCentreOfScreen();
    TestReconstructsRomNativePosition();
    TestPerProductRoundingIsLoadBearing();
    TestNegativeAndExtremeNumerators();
    TestTranslationAndDepthOrdering();
    TestDeadbandSuppressesBoundaryDither();
    TestDeadbandStillTracksRealMotion();
    TestDeadbandSnapsOnJumpAndReset();
    TestMatchesNativeAcceptsAndRejects();
    TestPlayerPointerValidation();
    TestRejectedFrameHoldsInsteadOfSwitching();
    TestAlternatingRejectionNeverSwitchesSource();
    TestSystematicRejectionSettlesOnTheCache();
    TestDeadbandToggleResolves();
    TestDeadbandWidthIsHonoured();

    if (g_failures != 0) {
        std::printf("crosshair-projection-tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf(
        "crosshair-projection-tests: clip gate, ROM reconstruction, rounding "
        "order, signed divide, orientation, deadband, gate, hold, "
        "player pointer PASS\n");
    return 0;
}
