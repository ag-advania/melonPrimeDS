// MELONPRIME_METAL_COMPUTE_SPAN_MATH_V1
// Shared MSL prologue for the Metal compute span/raster kernels.
//
// This fragment carries the DS fixed-point span math ported from the OpenGL
// compute renderer (src/GPU3D_Compute_shaders.h). The GLSL original emulates
// 64-bit arithmetic with 32-bit pairs because GLSL lacks 64-bit integers; MSL
// has native `ulong`, so the same operations are expressed directly. The
// observable results (including truncation and overflow behaviour) are the
// same, which is what the OpenGL-parity requirement is about.
//
// The fragment is prepended to the span/bin and textured-raster libraries by
// MetalComputeRenderer3D::CreateComputeFoundation(); it therefore owns the
// SpanSetupY/SpanSetupX declarations for both.
static constexpr const char* kMetalComputeSpanMathSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

constant uint XSpanSetup_Linear = 1u << 0u;
constant uint XSpanSetup_FillInside = 1u << 1u;
constant uint XSpanSetup_FillLeft = 1u << 2u;
constant uint XSpanSetup_FillRight = 1u << 3u;

// GPU3D_Compute_shaders.h uses YFactorShift = 9 in InterpSpans and 8 in
// Rasterise. The two passes share the helpers here, so the shift is explicit.
constant int MPFactorShiftY = 9;
constant int MPFactorShiftX = 8;

struct SpanSetupY
{
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;

    int I0, I1;
    int Linear;
    int IRecip;
    int W0n, W0d, W1d;

    int Increment;

    int X0, X1, Y0, Y1;
    int XMin, XMax;
    int DxInitial;

    int XCovIncr;
    uint IsDummy;
};

struct SpanSetupX
{
    int X0, X1;
    int InsideStart, InsideEnd, EdgeCovL, EdgeCovR;
    int XRecip;
    uint Flags;
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;
    int CovLInitial, CovRInitial;
};

// ---------------------------------------------------------------------------
// Attribute interpolation
// ---------------------------------------------------------------------------

static inline int mp_interp_attr_persp(int y0, int y1, int ifactor, int shift)
{
    if (y0 == y1)
        return y0;
    if (y0 < y1)
        return y0 + (((y1 - y0) * ifactor) >> shift);
    return y1 + (((y0 - y1) * ((1 << shift) - ifactor)) >> shift);
}

// absoluteOffsets mirrors the GLSL `#ifndef Rasterise` branches: the Y pass
// interpolates along signed edge indices, the X pass along a positive run.
static inline int mp_interp_attr_linear(
    int y0, int y1, int i, int irecip, int idiff, bool absoluteOffsets)
{
    (void)irecip;
    if (y0 == y1)
        return y0;

    const uint denominator = uint(abs(idiff));
    if (denominator == 0u)
        return y0;
    if (y0 < y1)
    {
        const uint offset = absoluteOffsets ? uint(abs(i)) : uint(i);
        const ulong numerator = ulong(uint(y1 - y0)) * ulong(offset);
        return y0 + int(uint(numerator / ulong(denominator)));
    }

    const uint offset =
        absoluteOffsets ? uint(abs(idiff - i)) : uint(idiff - i);
    const ulong numerator = ulong(uint(y0 - y1)) * ulong(offset);
    return y1 + int(uint(numerator / ulong(denominator)));
}

// interpSpans selects the wider InterpSpans normalisation (variable pre-shift)
// over the fixed 9-bit pre-shift used along X spans.
static inline uint mp_interp_z_zbuffer(
    int z0, int z1, int i, int irecip, int idiff, bool interpSpans)
{
    if (z0 == z1)
        return uint(z0);

    uint base, disp, factor;
    if (z0 < z1)
    {
        base = uint(z0);
        disp = uint(z1 - z0);
        factor = uint(abs(i));
    }
    else
    {
        base = uint(z1);
        disp = uint(z0 - z1);
        factor = uint(abs(idiff - i));
    }

    uint shiftLeft = 0u;
    uint shiftRight = 22u;
    if (interpSpans)
    {
        if (disp > 0x3FFu)
        {
            shiftLeft = (31u - clz(disp)) - 9u;
            disp >>= shiftLeft;
        }
    }
    else
    {
        disp >>= 9u;
        shiftRight = 13u;
    }

    const ulong product =
        ulong(disp * factor) * ulong(uint(abs(irecip)) >> 8u);
    return base + (uint(product >> shiftRight) << shiftLeft);
}

// wideProduct is the InterpSpans case, where the product needs 64 bits. Along
// X spans the factor is only 8-bit, so the GLSL original stays in 32 bits.
static inline uint mp_interp_z_wbuffer(
    int z0, int z1, int ifactor, int shift, bool wideProduct)
{
    if (z0 == z1)
        return uint(z0);

    if (!wideProduct)
    {
        if (z0 < z1)
            return uint(z0) + uint(((z1 - z0) * ifactor) >> shift);
        return uint(z1) + uint(((z0 - z1) * ((1 << shift) - ifactor)) >> shift);
    }

    if (z0 < z1)
    {
        const ulong product = ulong(uint(z1 - z0)) * ulong(uint(ifactor));
        return uint(z0) + uint(product >> uint(shift));
    }
    const ulong product =
        ulong(uint(z0 - z1)) * ulong(uint((1 << shift) - ifactor));
    return uint(z1) + uint(product >> uint(shift));
}

// ---------------------------------------------------------------------------
// Perspective factors
// ---------------------------------------------------------------------------

static inline int mp_calc_factor_y(thread const SpanSetupY& span, int i)
{
    const uint numerator = uint(abs(i)) * uint(span.W0n);
    const uint denominator =
        uint(abs(i)) * uint(span.W0d) +
        uint(abs(span.I1 - span.I0 - i)) * uint(span.W1d);
    if (denominator == 0u)
        return 0;
    return int(uint(
        (ulong(numerator) << uint(MPFactorShiftY)) / ulong(denominator)));
}

static inline int mp_calc_factor_x(thread const SpanSetupX& span, int x)
{
    if (span.X0 == span.X1)
        return 0;

    x -= span.X0;
    const uint numerator = uint(x) * uint(span.W0);
    const uint denominator =
        uint(x) * uint(span.W0) +
        uint(span.X1 - span.X0 - x) * uint(span.W1);
    if (denominator == 0u)
        return 0;
    return int(uint(
        (ulong(numerator) << uint(MPFactorShiftX)) / ulong(denominator)));
}

// ---------------------------------------------------------------------------
// Edge length and coverage
// ---------------------------------------------------------------------------

static inline void mp_edge_params_xmajor(
    bool side,
    bool swapped,
    int dx,
    thread const SpanSetupY& span,
    thread int& edgeLen,
    thread int& edgeCov)
{
    const bool negative = span.X1 < span.X0;
    int len = 1;
    if (!swapped || side)
    {
        if (side != negative)
            len = (dx >> 18) - ((dx - span.Increment) >> 18);
        else
            len = ((dx + span.Increment) >> 18) - (dx >> 18);
    }
    edgeLen = len;

    const int xlen = span.XMax + 1 - span.XMin;
    int startX = dx >> 18;
    if (negative)
        startX = xlen - startX;
    if (side)
        startX = startX - len + 1;

    int startCov = 0;
    if (xlen != 0)
    {
        startCov = int(
            uint(((startX << 10) + 0x1FF) * (span.Y1 - span.Y0)) / uint(xlen));
    }

    edgeCov = int(1u << 31u) |
              ((startCov & 0x3FF) << 12) |
              (span.XCovIncr & 0x3FF);
    if (swapped)
        edgeLen = 1;
}

static inline void mp_edge_params_ymajor(
    bool side,
    bool swapped,
    int dx,
    thread const SpanSetupY& span,
    thread int& edgeLen,
    thread int& edgeCov)
{
    const bool negative = span.X1 < span.X0;
    edgeLen = 1;

    if (span.Increment == 0)
    {
        edgeCov = swapped ? 0 : 31;
        return;
    }

    int cov = ((dx >> 9) + (span.Increment >> 10)) >> 4;
    if ((cov >> 5) != (dx >> 18))
        cov = 31;
    cov &= 0x1F;
    if (swapped ? (side != negative) : (side == negative))
        cov = 0x1F - cov;
    edgeCov = cov;
}

static inline void mp_edge_params(
    bool side,
    bool swapped,
    int dx,
    thread const SpanSetupY& span,
    thread int& edgeLen,
    thread int& edgeCov)
{
    if (span.Increment > 0x40000)
        mp_edge_params_xmajor(side, swapped, dx, span, edgeLen, edgeCov);
    else
        mp_edge_params_ymajor(side, swapped, dx, span, edgeLen, edgeCov);
}

)MSL";
