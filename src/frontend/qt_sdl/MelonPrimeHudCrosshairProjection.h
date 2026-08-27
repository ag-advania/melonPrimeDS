#ifndef MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H
#define MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H

#ifdef MELONPRIME_CUSTOM_HUD

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

} // namespace MelonPrime::CrosshairProjection

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_CROSSHAIR_PROJECTION_H
