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

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
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

} // namespace

int main()
{
    TestBehindNearPlaneIsRejected();
    TestCentreOfScreen();
    TestReconstructsRomNativePosition();
    TestPerProductRoundingIsLoadBearing();
    TestNegativeAndExtremeNumerators();
    TestTranslationAndDepthOrdering();

    if (g_failures != 0) {
        std::printf("crosshair-projection-tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf(
        "crosshair-projection-tests: clip gate, ROM reconstruction, rounding "
        "order, signed divide, orientation PASS\n");
    return 0;
}
