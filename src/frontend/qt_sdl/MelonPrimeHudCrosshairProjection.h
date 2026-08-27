#ifndef MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H
#define MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <climits>
#include <cmath>
#include <cstdint>

// =========================================================================
//  High-resolution Custom HUD crosshair centre.
//
//  MPH projects the crosshair with Q12 fixed point and the NDS hardware
//  divider, then quantises the result twice: the raw signed Q32 quotient is
//  rounded to a Q12 NDC, and that NDC is converted to an integer 256x192
//  screen position which the ROM caches as an s16 pair.
//
//  Custom HUD only needs the centre, and it draws at output resolution, so
//  both quantisation steps are pure precision loss: one native DS pixel
//  becomes a multi-pixel jump once the frame is scaled up, which is what makes
//  the reticle visibly step as the player aims.
//
//  This module reproduces the game's arithmetic up to the raw Q32 quotient and
//  stops there. Everything before that point must stay bit-identical to the
//  ROM, because the input is the game's own fixed-point state:
//
//    * the affine transform accumulates its three products in 64 bits and
//      shifts once, with no rounding term (02081A4C and its per-version
//      equivalents are machine-code identical across all seven ROMs);
//    * every projection product carries its own +0x800 before ASR 12, so the
//      products cannot be summed first;
//    * clipW <= 0 means the point is behind the near plane, and the ROM writes
//      no screen position at all -- callers must keep their previous value or
//      fall back rather than invent one.
//
//  Reference: mphCodex mphAnalysis/HUD/High-Resolution-Crosshair.
//
//  Deliberately free of Qt, melonDS and Custom HUD state so the arithmetic can
//  be exercised directly by tools/testing/crosshair-projection-tests.cpp.
// =========================================================================

namespace MelonPrime::CrosshairProjection {

// The ROM's arithmetic is two's-complement with arithmetic right shifts.
// C++17 leaves >> on a negative value implementation-defined rather than
// undefined; every toolchain this project builds with defines it as an
// arithmetic shift, and this pins that assumption at compile time instead of
// leaving it implicit.
static_assert((-1 >> 1) == -1,
    "MPH fixed-point reproduction requires an arithmetic right shift");

// Offsets into the local player struct. Both are read relative to the
// player-position-adjusted base, not slot 0: the crosshair belongs to the
// local player, who is only in slot 0 when the match happens to seat them
// there.
inline constexpr uint32_t kPlayerProjectionSourceOffset = 0xA8u;
inline constexpr uint32_t kPlayerViewTransformOffset = 0x5B4u;

inline constexpr int kSourceWordCount = 3;      // Q12 Vec3
inline constexpr int kViewWordCount = 12;       // 3x4 Q12 affine transform
inline constexpr int kProjectionWordCount = 16; // 4x4 Q12 projection matrix

// One game frame of projection inputs: 12 + 48 + 64 = 124 bytes of guest RAM.
struct Input {
    int32_t source[kSourceWordCount];
    int32_t view[kViewWordCount];
    int32_t projection[kProjectionWordCount];
};

// Fractional DS-space centre: x in 0..256, y in 0..192, before the HUD
// painter transform maps it to output pixels. Keeping it fractional is the
// entire point -- the single remaining rounding then happens against actual
// output pixels rather than native DS pixels.
struct Result {
    double dsX;
    double dsY;
    // Raw quotients, retained so validation can reconstruct the ROM's own
    // native s16 path and prove the reproduction is bit-exact.
    int64_t q32X;
    int64_t q32Y;
};

// The affine transform sums three products in 64 bits and shifts once. It does
// NOT round, unlike the projection below; adding a rounding term here would
// diverge from the ROM.
[[nodiscard]] constexpr int32_t TransformRow(
    const Input& in, int c0, int c1, int c2, int translate) noexcept
{
    const int64_t accumulated =
          static_cast<int64_t>(in.source[0]) * in.view[c0]
        + static_cast<int64_t>(in.source[1]) * in.view[c1]
        + static_cast<int64_t>(in.source[2]) * in.view[c2];
    return static_cast<int32_t>(accumulated >> 12) + in.view[translate];
}

// Each projection product rounds on its own before the shift. Summing first
// and rounding once would drift from the ROM by up to one Q12 unit per term.
[[nodiscard]] constexpr int32_t MulRoundQ12(int32_t a, int32_t b) noexcept
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * b + 0x800) >> 12);
}

[[nodiscard]] constexpr int32_t ClipRow(
    const int32_t (&view)[3], const Input& in,
    int r0, int r1, int r2, int translate) noexcept
{
    return MulRoundQ12(view[0], in.projection[r0])
         + MulRoundQ12(view[1], in.projection[r1])
         + MulRoundQ12(view[2], in.projection[r2])
         + in.projection[translate];
}

// The NDS divider is set up in 64/32 signed mode with the numerator in the
// high word, which is exactly numerator * 2^32. The multiply cannot overflow:
// the extreme int32 numerator maps to -2^63, which is representable, and
// clipW > 0 is guaranteed by the caller so the INT64_MIN / -1 trap is
// unreachable. C++ integer division truncates toward zero, matching the
// hardware's signed quotient.
[[nodiscard]] constexpr int64_t DivideQ32(int32_t numerator, int32_t clipW) noexcept
{
    return (static_cast<int64_t>(numerator) * 4294967296LL) / clipW;
}

// Reproduces the ROM's projection up to the raw Q32 quotient.
// Returns false when the point is behind the near plane (clipW <= 0), which is
// the same condition under which the ROM writes no screen position.
[[nodiscard]] constexpr bool Project(const Input& in, Result& out) noexcept
{
    const int32_t view[3] = {
        TransformRow(in, 0, 3, 6, 9),
        TransformRow(in, 1, 4, 7, 10),
        TransformRow(in, 2, 5, 8, 11),
    };

    const int32_t clipW = ClipRow(view, in, 3, 7, 11, 15);
    if (clipW <= 0)
        return false;

    const int32_t clipX = ClipRow(view, in, 0, 4, 8, 12);
    const int32_t clipY = ClipRow(view, in, 1, 5, 9, 13);

    out.q32X = DivideQ32(clipX, clipW);
    out.q32Y = DivideQ32(clipY, clipW);

    // 2^-32, exact in binary64, so this is a scale rather than a division.
    constexpr double kInvQ32 = 1.0 / 4294967296.0;
    out.dsX = 128.0 * (1.0 + static_cast<double>(out.q32X) * kInvQ32);
    out.dsY =  96.0 * (1.0 - static_cast<double>(out.q32Y) * kInvQ32);
    return true;
}

// =========================================================================
//  Native reconstruction.
//
//  Not used by the render path. These reproduce the two quantisation steps the
//  high-resolution path deliberately skips, so a test can take the same raw
//  Q32 quotient and land on the exact s16 pair the ROM caches. That is what
//  makes "the fractional centre is the same point, only unrounded" a checkable
//  claim rather than an assertion.
// =========================================================================

[[nodiscard]] constexpr int32_t NdcQ12FromQ32(int64_t q32) noexcept
{
    return static_cast<int32_t>((q32 + 0x80000) >> 20);
}

[[nodiscard]] constexpr int16_t NativeScreenX(int32_t ndcXQ12) noexcept
{
    return static_cast<int16_t>(
        (static_cast<int64_t>(ndcXQ12 + 0x1000) * 128) >> 12);
}

[[nodiscard]] constexpr int16_t NativeScreenY(int32_t ndcYQ12) noexcept
{
    return static_cast<int16_t>(
        (static_cast<int64_t>(0x1000 - ndcYQ12) * 96) >> 12);
}

// The ROM publishes its own projection of this same point as an s16 pair, and
// the reconstruction above is bit-exact by construction. Agreement is
// therefore the check that the sampled inputs are the ones the ROM actually
// used.
//
// It is needed because the inputs are live intermediates, not published
// values: the projection matrix is a shared global that other render passes
// overwrite, and the source vector and view transform live in a player struct
// selected by a player index. Custom HUD samples at presentation time, which
// is not synchronised with the game's crosshair update, so a frame that
// catches any of those mid-change would otherwise project a point from a
// different camera -- the crosshair lands somewhere unrelated.
//
// Disagreeing costs that frame's sub-pixel precision and nothing else: the
// caller falls back to the same cache the legacy path read.
[[nodiscard]] constexpr bool MatchesNative(
    const Result& result, int16_t nativeX, int16_t nativeY) noexcept
{
    return NativeScreenX(NdcQ12FromQ32(result.q32X)) == nativeX
        && NativeScreenY(NdcQ12FromQ32(result.q32Y)) == nativeY;
}

// =========================================================================
//  Sub-pixel deadband.
//
//  The centre above is projected rather than read from the ROM's quantised
//  cache, so it carries the game's own sub-pixel noise: the same aim ray
//  hitting geometry at a different depth rounds slightly differently through
//  the Q12 chain, and across a silhouette edge the depth jumps outright.
//  Rounding that straight to a pixel makes the centre toggle between two
//  neighbours whenever it sits near a pixel boundary, which reads as the thin
//  crosshair arms flickering at half intensity.
//
//  The ROM's quantisation used to hide this. A Schmitt trigger restores the
//  stability without giving the precision back: the committed pixel only moves
//  once the exact centre is more than half a pixel plus the deadband away, so
//  real aiming still tracks continuously while boundary dither cannot flip it.
//  A large jump is unaffected -- it lands far outside the band and re-rounds.
// =========================================================================

// Default and clamp range for the configurable deadband. Zero is meaningful:
// it restores plain rounding for anyone who prefers the crosshair to follow
// the projected centre exactly.
inline constexpr double kDefaultPixelDeadband = 0.25;
inline constexpr double kMaxPixelDeadband = 2.0;

// The toggle and the width travel together so a caller cannot reach for the
// width alone and silently lose the toggle. Resolving here also keeps "off"
// and "zero width" provably the same thing rather than two code paths that
// could drift.
struct DeadbandSetting {
    bool enabled = true;
    double widthPx = kDefaultPixelDeadband;

    [[nodiscard]] double Resolve() const noexcept
    {
        return enabled ? widthPx : 0.0;
    }
};

// `sticky` carries the committed pixel across frames. kNoCommittedPixel means
// there is none yet, so the next call snaps rather than easing out of a stale
// position; callers reset to it whenever the crosshair leaves the screen.
inline constexpr int kNoCommittedPixel = INT_MIN;

[[nodiscard]] inline int CommitPixel(
    double exact, double deadband, int& sticky) noexcept
{
    // Only the upper clamp does anything. A negative band makes both
    // comparisons true, so the pixel re-rounds every call -- and re-rounding a
    // value already within half a pixel returns that same pixel, which is
    // exactly what a zero band does. An unclamped large band, by contrast,
    // would freeze the crosshair in place, so that one is load-bearing.
    const double band =
        deadband > kMaxPixelDeadband ? kMaxPixelDeadband : deadband;
    if (sticky == kNoCommittedPixel
        || exact > static_cast<double>(sticky) + 0.5 + band
        || exact < static_cast<double>(sticky) - 0.5 - band) {
        sticky = static_cast<int>(std::lround(exact));
    }
    return sticky;
}

} // namespace MelonPrime::CrosshairProjection

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H
