/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef GPU3D_DX12_SHADERS_H
#define GPU3D_DX12_SHADERS_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>

// HLSL port of the OpenGL compute renderer's shader set
// (GPU3D_Compute_shaders.h). The compute variants are compiled offline into
// GPU3D_DX12_ShaderBlobs.inc; only the native presenter's small VS/PS pair
// still uses d3dcompiler_47.dll at runtime.
//
// The tile-binned pipeline, the fixed-point math and the intermediate buffer
// layouts are a 1:1 port; the differences are all forced by HLSL:
//
//   * GLSL's umulExtended/uaddCarry/findMSB/findLSB/bitCount/bitfield* become
//     umul/manual carry/firstbithigh/firstbitlow/countbits/manual shifts.
//   * Texture wrapping is done in the shader with integer math instead of
//     sampler state: HLSL cannot Sample() a UINT texture, so the decoded
//     texture array is read with Load() at explicitly wrapped texel coords.
//   * The display-capture-as-texture path is dropped. The DX12 renderer is
//     paired with the software 2D renderer, which writes captures into real
//     VRAM, so the ordinary texcache lookup already returns the captured data.
//
// The internal pixel encoding is identical to the OpenGL compute renderer's:
// color words are r6 | g6<<8 | b6<<16 | a5<<24, which is also exactly the
// layout the software 2D compositor expects from Renderer3D::GetLine().
namespace melonDS::DX12Shaders
{

// Prepended to every shader. The renderer only specializes values required by
// HLSL numthreads. Screen dimensions and scale-dependent buffer offsets are
// frame constants, so changing resolution never requires recompiling HLSL.
inline const std::string Common = R"(

static const int CoarseTileCountX = 8;
static const int CoarseTileW = (CoarseTileCountX * TileSize);
static const int CoarseTileH = (CoarseTileCountY * TileSize);

static const int BinStride = 2048 / 32;
static const int CoarseBinStride = BinStride / 32;

static const int MaxVariants = 2048;

// Must stay <= 65535: matches the OpenGL renderer's split dispatch for the
// driver group-count limit (melonDS issue #2047). Kept identical so the
// per-variant indirect arguments the CPU side writes mean the same thing.
static const uint RasteriseChunkSize = 32768u;

static const uint ResultColorStart = 0u;
// Byte offsets into the bin-result buffer. It mixes a fixed header with a
// trailing flexible array, so it is bound as a RWByteAddressBuffer rather than
// a structured buffer. Layout matches BinResultHeader in GPU3D_DX12.h.
static const uint BinVariantWorkCountBase     = 0u;                         // uint4[MaxVariants]
static const uint BinSortedWorkOffsetBase     = uint(MaxVariants) * 16u;    // uint[MaxVariants]
static const uint BinVariantWorkRealCountBase = uint(MaxVariants) * 20u;    // uint[MaxVariants]
static const uint BinSortWorkWorkCountBase    = uint(MaxVariants) * 24u;    // uint4
static const uint BinMaskAndOffsetBase        = uint(MaxVariants) * 24u + 16u;

static const int BinningCoarseMaskStart = 0;
static const uint WorkDescsUnsortedStart = 0u;

static const uint XSpanSetup_Linear = 1u << 0;
static const uint XSpanSetup_FillInside = 1u << 1;
static const uint XSpanSetup_FillLeft = 1u << 2;
static const uint XSpanSetup_FillRight = 1u << 3;

cbuffer MetaUniform : register(b1)
{
    uint NumPolygons;
    uint NumVariants;
    uint AlphaRef;
    uint DispCnt;

    // .x = toon color, .y = fog density, .z = edge color
    uint4 ToonTable[34];

    uint ClearColor;
    uint ClearDepth;
    uint ClearAttr;
    uint FogOffset;

    uint FogShift;
    uint FogColor;
    float2 ClearBitmapOffset;

};

// Per-dispatch root constants.
cbuffer DispatchUniform : register(b0)
{
    uint CurVariant;
    uint TexWidth;
    uint TexHeight;
    uint TexWrapS;   // 0 = clamp, 1 = repeat, 2 = mirrored repeat
    uint TexWrapT;
    uint InterpSpanBase;
    uint InterpSpanCount;
    uint DispatchPad;

    uint ScreenWidth;
    uint ScreenHeight;
    uint ScaleFactor;
    uint TilesPerLine;

    uint TileLines;
    uint FramebufferStride;
    uint ResultDepthStart;
    uint ResultAttrStart;

    uint BinningMaskStart;
    uint BinningWorkOffsetsStart;
    uint WorkDescsSortedStart;
    uint MaxWorkTiles;
};

struct Polygon
{
    int FirstXSpan;
    int YTop, YBot;

    int XMin, XMax;
    int XMinY, XMaxY;

    uint Variant;
    uint Attr;

    float TextureLayer;
    uint FacingView;
};

struct XSpanSetup
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

struct YSpanSetup
{
    // Attributes
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;

    // Interpolator
    int I0, I1;
    uint Linear;
    int IRecip;
    int W0n, W0d, W1d;

    // Slope
    int Increment;

    int X0, X1, Y0, Y1;
    int XMin, XMax;
    int DxInitial;

    int XCovIncr;

    uint IsDummy;
};

Texture2D<uint> ClearBitmapColor : register(t0);
Texture2D<uint> ClearBitmapDepth : register(t1);
StructuredBuffer<Polygon> Polygons : register(t2);
StructuredBuffer<YSpanSetup> YSpanSetups : register(t3);
Buffer<uint4> SetupIndices : register(t4);
Texture2DArray<uint4> CurrentTexture : register(t5);

RWStructuredBuffer<uint> ResultValue : register(u0);
RWStructuredBuffer<uint> FinalFB : register(u1);
RWStructuredBuffer<uint> ColorTiles : register(u2);
RWStructuredBuffer<uint> DepthTiles : register(u3);
RWStructuredBuffer<uint> AttrTiles : register(u4);
RWByteAddressBuffer BinResult : register(u5);
RWStructuredBuffer<uint2> WorkDescs : register(u6);
RWStructuredBuffer<XSpanSetup> XSpanSetups : register(u7);
RWStructuredBuffer<uint> ResolveOut : register(u8);
RWStructuredBuffer<uint> CaptureSidecarBuffer : register(u9);
RWStructuredBuffer<uint> BlendContinuationState : register(u10);
RWStructuredBuffer<uint> ResultWinner : register(u11);
RWByteAddressBuffer IndirectArgs : register(u12);
RWTexture2DArray<float4> DirectOutput : register(u13);

// `firstbithigh`/`firstbitlow` disagree between shader targets about whether
// the index is counted from the LSB or the MSB, and the fixed-point division
// below is exquisitely sensitive to that. Compute it explicitly instead, with
// GLSL findMSB/findLSB semantics (bit index from the LSB).
uint FindMSB(uint v)
{
    uint r = 0u;
    uint x = v;
    if ((x & 0xFFFF0000u) != 0u) { x >>= 16; r += 16u; }
    if ((x & 0x0000FF00u) != 0u) { x >>= 8;  r += 8u; }
    if ((x & 0x000000F0u) != 0u) { x >>= 4;  r += 4u; }
    if ((x & 0x0000000Cu) != 0u) { x >>= 2;  r += 2u; }
    if ((x & 0x00000002u) != 0u) { r += 1u; }
    return r;
}

uint FindLSB(uint v)
{
    return FindMSB(v & (0u - v));
}

uint LoadBinMask(int index)
{
    return BinResult.Load(BinMaskAndOffsetBase + uint(index) * 4u);
}

void StoreBinMask(int index, uint value)
{
    BinResult.Store(BinMaskAndOffsetBase + uint(index) * 4u, value);
}

#ifdef InterpSpans
static const uint YFactorShift = 9u;
#else
static const uint YFactorShift = 8u;
#endif

#if defined(InterpSpans) || defined(Rasterise)

// GLSL's umulExtended has no cs_5_1 equivalent (`umul` is not exposed by the
// legacy compiler), so the 32x32 -> 64 product is built from 16-bit halves.
void UMul64(uint a, uint b, out uint hi, out uint lo)
{
    uint a0 = a & 0xFFFFu, a1 = a >> 16;
    uint b0 = b & 0xFFFFu, b1 = b >> 16;

    uint p00 = a0 * b0;
    uint p01 = a0 * b1;
    uint p10 = a1 * b0;
    uint p11 = a1 * b1;

    // Cannot overflow: the maximum sum is exactly 0xFFFFFFFF.
    uint mid = p10 + (p00 >> 16) + (p01 & 0xFFFFu);

    lo = (mid << 16) | (p00 & 0xFFFFu);
    hi = p11 + (p01 >> 16) + (mid >> 16);
}

uint Umulh(uint a, uint b)
{
    uint lo, hi;
    UMul64(a, b, hi, lo);
    return hi;
}

static const uint startTable[256] = {
    254, 252, 250, 248, 246, 244, 242, 240, 238, 236, 234, 233, 231, 229, 227, 225, 224, 222, 220, 218,
    217, 215, 213, 212, 210, 208, 207, 205, 203, 202, 200, 199, 197, 195, 194, 192, 191, 189, 188, 186,
    185, 183, 182, 180, 179, 178, 176, 175, 173, 172, 170, 169, 168, 166, 165, 164, 162, 161, 160, 158,
    157, 156, 154, 153, 152, 151, 149, 148, 147, 146, 144, 143, 142, 141, 139, 138, 137, 136, 135, 134,
    132, 131, 130, 129, 128, 127, 126, 125, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112,
    111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92,
    91, 90, 89, 88, 88, 87, 86, 85, 84, 83, 82, 81, 80, 80, 79, 78, 77, 76, 75, 74,
    74, 73, 72, 71, 70, 70, 69, 68, 67, 66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 59,
    58, 57, 56, 56, 55, 54, 53, 53, 52, 51, 50, 50, 49, 48, 48, 47, 46, 46, 45, 44,
    43, 43, 42, 41, 41, 40, 39, 39, 38, 37, 37, 36, 35, 35, 34, 33, 33, 32, 32, 31,
    30, 30, 29, 28, 28, 27, 27, 26, 25, 25, 24, 24, 23, 22, 22, 21, 21, 20, 19, 19,
    18, 18, 17, 17, 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 10, 10, 9, 9, 8, 8,
    7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0
};

uint Div(uint x, uint y, out uint r)
{
    // https://www.microsoft.com/en-us/research/publication/software-integer-division/
    uint k = 31u - FindMSB(y);
    uint ty = (y << k) >> (32u - 9u);
    uint t = startTable[ty - 256u] + 256u;
    uint z = (t << (32u - 9u)) >> (32u - k - 1u);
    uint my = 0u - y;

    z += Umulh(z, my * z);
    z += Umulh(z, my * z);

    uint q = Umulh(x, z);
    r = x - y * q;
    if (r >= y)
    {
        r = r - y;
        q = q + 1u;
        if (r >= y)
        {
            r = r - y;
            q = q + 1u;
        }
    }

    return q;
}

uint Div64_32_32(uint numHi, uint numLo, uint den)
{
    // based on libdivide's 128/64 division, halved to 64/32=32
    const uint b = (1u << 16);

    uint shift = 31u - FindMSB(den);
    den <<= shift;
    numHi <<= shift;
    numHi |= (numLo >> ((0u - shift) & 31u)) & uint(-int(shift) >> 31);
    numLo <<= shift;

    uint num1 = (numLo >> 16);
    uint num0 = (numLo & 0xFFFFu);
    uint den1 = (den >> 16);
    uint den0 = (den & 0xFFFFu);

    uint rhat;
    uint qhat = Div(numHi, den1, rhat);
    uint c1 = qhat * den0;
    uint c2 = rhat * b + num1;
    if (c1 > c2) qhat -= (c1 - c2 > den) ? 2u : 1u;
    uint q1 = qhat & 0xFFFFu;

    uint rem = numHi * b + num1 - q1 * den;

    qhat = Div(rem, den1, rhat);
    c1 = qhat * den0;
    c2 = rhat * b + num0;
    if (c1 > c2) qhat -= (c1 - c2 > den) ? 2u : 1u;

    // bitfieldInsert(qhat, q1, 16, 16)
    return (qhat & 0xFFFFu) | (q1 << 16);
}

int InterpolateAttrPersp(int y0, int y1, int ifactor)
{
    int result = y0;
    if (y0 != y1)
    {
        result = y0 < y1
            ? y0 + (((y1 - y0) * ifactor) >> YFactorShift)
            : y1 + (((y0 - y1) * ((1 << YFactorShift) - ifactor)) >> YFactorShift);
    }
    return result;
}

int InterpolateAttrLinear(int y0, int y1, int i, int irecip, int idiff)
{
    int result = y0;
    if (y0 != y1)
    {
        uint numeratorLo, numeratorHi;
        uint denominator = uint(abs(idiff));
        if (y0 < y1)
        {
#ifndef Rasterise
            uint offset = uint(abs(i));
#else
            uint offset = uint(i);
#endif
            UMul64(uint(y1 - y0), offset, numeratorHi, numeratorLo);
            uint quotient;
            if (numeratorHi == 0u)
            {
                uint remainder;
                quotient = Div(numeratorLo, denominator, remainder);
            }
            else
            {
                quotient = Div64_32_32(numeratorHi, numeratorLo, denominator);
            }
            result = y0 + int(quotient);
        }
        else
        {
#ifndef Rasterise
            uint offset = uint(abs(idiff - i));
#else
            uint offset = uint(idiff - i);
#endif
            UMul64(uint(y0 - y1), offset, numeratorHi, numeratorLo);
            uint quotient;
            if (numeratorHi == 0u)
            {
                uint remainder;
                quotient = Div(numeratorLo, denominator, remainder);
            }
            else
            {
                quotient = Div64_32_32(numeratorHi, numeratorLo, denominator);
            }
            result = y1 + int(quotient);
        }
    }
    return result;
}

uint InterpolateZZBuffer(int z0, int z1, int i, int irecip, int idiff)
{
    uint result = uint(z0);
    if (z0 != z1)
    {
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

#ifdef InterpSpans
        int shiftl = 0;
        const int shiftr = 22;
        if (disp > 0x3FFu)
        {
            shiftl = int(FindMSB(disp)) - 9;
            disp >>= shiftl;
        }
#else
        disp >>= 9;
        const int shiftl = 0;
        const int shiftr = 13;
#endif
        uint mulLo, mulHi;

        UMul64(disp * factor, uint(abs(irecip)) >> 8, mulHi, mulLo);

        result = base + (((mulLo >> shiftr) | (mulHi << (32 - shiftr))) << shiftl);
    }
    return result;
}

uint InterpolateZWBuffer(int z0, int z1, int ifactor)
{
    uint result = uint(z0);
    if (z0 != z1)
    {
#ifdef Rasterise
        // along x spans the precision is only 8 bit, so the result always fits
        if (z0 < z1)
            result = uint(z0) + uint(((z1 - z0) * ifactor) >> YFactorShift);
        else
            result = uint(z1) + uint(((z0 - z1) * ((1 << YFactorShift) - ifactor)) >> YFactorShift);
#else
        uint mulLo, mulHi;
        if (z0 < z1)
        {
            UMul64(uint(z1 - z0), uint(ifactor), mulHi, mulLo);
            result = uint(z0) + ((mulLo >> YFactorShift) | (mulHi << (32u - YFactorShift)));
        }
        else
        {
            UMul64(uint(z0 - z1), uint((1 << YFactorShift) - ifactor), mulHi, mulLo);
            result = uint(z1) + ((mulLo >> YFactorShift) | (mulHi << (32u - YFactorShift)));
        }
#endif
    }
    return result;
}

#endif // InterpSpans || Rasterise
)";

// ---------------------------------------------------------------------------
// Stage 1: per-scanline X span interpolation
// ---------------------------------------------------------------------------
inline const std::string InterpSpans = R"(

int CalcYFactorY(YSpanSetup span, int i)
{
    uint numLo = uint(abs(i)) * uint(span.W0n);
    uint numHi = 0u;
    numHi |= numLo >> (32u - YFactorShift);
    numLo <<= YFactorShift;

    uint den = uint(abs(i)) * uint(span.W0d) + uint(abs(span.I1 - span.I0 - i)) * uint(span.W1d);

    int factor = 0;
    if (den != 0u)
        factor = int(Div64_32_32(numHi, numLo, den));
    return factor;
}

int CalculateDx(int y, YSpanSetup span)
{
    return span.DxInitial + (y - span.Y0) * span.Increment;
}

int CalculateX(int dx, YSpanSetup span)
{
    int x = span.X0;
    if (span.X1 < span.X0)
        x -= dx >> 18;
    else
        x += dx >> 18;
    return clamp(x, span.XMin, span.XMax);
}

bool ShouldDecrementRightVertical(YSpanSetup spanL, YSpanSetup spanR, int xl, int xr)
{
    return spanR.Increment == 0
        && (spanL.Increment != 0 || xl != xr)
        && xr != 0;
}

bool IsBottomNonFlatEdge(int y, Polygon polygon, YSpanSetup spanL, YSpanSetup spanR)
{
    return y == polygon.YBot - 1 && spanL.X1 != spanR.X1;
}

void EdgeParams_XMajor(bool side, bool swapped, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    bool negative = span.X1 < span.X0;
    int len = 1;
    if (!swapped || side)
    {
        if (side != negative)
            len = (dx >> 18) - ((dx - span.Increment) >> 18);
        else
            len = ((dx + span.Increment) >> 18) - (dx >> 18);
    }
    edgelen = len;

    int xlen = span.XMax + 1 - span.XMin;
    int startx = dx >> 18;
    if (negative) startx = xlen - startx;
    if (side) startx = startx - len + 1;

    uint r;
    int startcov = int(Div(uint(((startx << 10) + 0x1FF) * (span.Y1 - span.Y0)), uint(xlen), r));
    edgecov = int(1u << 31) | ((startcov & 0x3FF) << 12) | (span.XCovIncr & 0x3FF);
    if (swapped)
        edgelen = 1;
}

void EdgeParams_YMajor(bool side, bool swapped, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    bool negative = span.X1 < span.X0;
    edgelen = 1;

    if (span.Increment == 0)
    {
        edgecov = swapped ? 0 : 31;
    }
    else
    {
        int cov = ((dx >> 9) + (span.Increment >> 10)) >> 4;
        if ((cov >> 5) != (dx >> 18)) cov = 31;
        cov &= 0x1F;
        if (swapped ? (side != negative) : (side == negative))
            cov = 0x1F - cov;

        edgecov = cov;
    }
}

void EdgeParams(bool side, bool swapped, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    if (span.Increment > int(0x40000u))
        EdgeParams_XMajor(side, swapped, dx, span, edgelen, edgecov);
    else
        EdgeParams_YMajor(side, swapped, dx, span, edgelen, edgecov);
}

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= InterpSpanCount)
        return;

    uint setupIndex = InterpSpanBase + id.x;
    uint4 setup = SetupIndices.Load(int(setupIndex));

    YSpanSetup spanL = YSpanSetups[setup.y];
    YSpanSetup spanR = YSpanSetups[setup.z];

    XSpanSetup xspan;
    xspan.Flags = 0u;

    int y = int(setup.w);

    int dxl = CalculateDx(y, spanL);
    int dxr = CalculateDx(y, spanR);

    int xl = CalculateX(dxl, spanL);
    int xr = CalculateX(dxr, spanR);

    if (ShouldDecrementRightVertical(spanL, spanR, xl, xr))
        xr--;

    Polygon polygon = Polygons[setup.x];

    int edgeLenL, edgeLenR;

    bool swappedEdges = xl > xr;
    if (swappedEdges)
    {
        YSpanSetup tmpSpan = spanL;
        spanL = spanR;
        spanR = tmpSpan;

        int tmp = xl;
        xl = xr;
        xr = tmp;

        EdgeParams(true, true, dxr, spanL, edgeLenL, xspan.EdgeCovL);
        EdgeParams(false, true, dxl, spanR, edgeLenR, xspan.EdgeCovR);
    }
    else
    {
        EdgeParams(false, false, dxl, spanL, edgeLenL, xspan.EdgeCovL);
        EdgeParams(true, false, dxr, spanR, edgeLenR, xspan.EdgeCovR);
    }

    xspan.CovLInitial = (xspan.EdgeCovL >> 12) & 0x3FF;
    if (xspan.CovLInitial == int(0x3FFu))
        xspan.CovLInitial = 0;
    xspan.CovRInitial = (xspan.EdgeCovR >> 12) & 0x3FF;
    if (xspan.CovRInitial == int(0x3FFu))
        xspan.CovRInitial = 0;

    xspan.X0 = xl;
    xspan.X1 = xr + 1;

    uint polyalpha = ((polygon.Attr >> 16) & 0x1Fu);
    bool isWireframe = polyalpha == 0u;

    if (!isWireframe || (y == polygon.YTop || y == polygon.YBot - 1))
        xspan.Flags |= XSpanSetup_FillInside;

    xspan.InsideStart = xspan.X0 + edgeLenL;
    if (xspan.InsideStart > xspan.X1)
        xspan.InsideStart = xspan.X1;
    xspan.InsideEnd = xspan.X1 - edgeLenR;
    if (xspan.InsideEnd > xspan.X1)
        xspan.InsideEnd = xspan.X1;

    bool fillAllEdges = isWireframe
        || (polyalpha < 31u && (DispCnt & (1u << 3)) != 0u)
        || (DispCnt & (3u << 4)) != 0u;
    bool bottomXMajor = IsBottomNonFlatEdge(y, polygon, spanL, spanR);
    bool leftNegative = spanL.X1 < spanL.X0;
    bool rightNegative = spanR.X1 < spanR.X0;
    bool leftXMajor = spanL.Increment > int(0x40000u);
    bool rightXMajor = spanR.Increment > int(0x40000u);

    bool fillLeft;
    bool fillRight;
    if (swappedEdges)
    {
        fillLeft = leftNegative || !leftXMajor || (bottomXMajor && leftXMajor);
        fillRight = (!rightNegative && rightXMajor)
            || (!(rightNegative && rightXMajor) && spanL.Increment == 0)
            || (bottomXMajor && rightXMajor);
    }
    else
    {
        fillLeft = leftNegative || !leftXMajor || (bottomXMajor && leftXMajor)
            || (spanL.Increment == spanR.Increment && xspan.X0 + edgeLenL == xspan.X1);
        fillRight = (!rightNegative && rightXMajor) || spanR.Increment == 0
            || (bottomXMajor && rightXMajor);
    }

    if (fillAllEdges || fillLeft)
        xspan.Flags |= XSpanSetup_FillLeft;
    if (fillAllEdges || fillRight)
        xspan.Flags |= XSpanSetup_FillRight;

    if (spanL.I0 == spanL.I1)
    {
        xspan.TexcoordU0 = spanL.TexcoordU0;
        xspan.TexcoordV0 = spanL.TexcoordV0;
        xspan.ColorR0 = spanL.ColorR0;
        xspan.ColorG0 = spanL.ColorG0;
        xspan.ColorB0 = spanL.ColorB0;
        xspan.Z0 = spanL.Z0;
        xspan.W0 = spanL.W0;
    }
    else
    {
        int i = y - spanL.I0;
        int ifactor = CalcYFactorY(spanL, i);
        int idiff = spanL.I1 - spanL.I0;

#ifdef ZBuffer
        xspan.Z0 = int(InterpolateZZBuffer(spanL.Z0, spanL.Z1, i, spanL.IRecip, idiff));
#else
        xspan.Z0 = int(InterpolateZWBuffer(spanL.Z0, spanL.Z1, ifactor));
#endif

        if (spanL.Linear == 0u)
        {
            xspan.TexcoordU0 = InterpolateAttrPersp(spanL.TexcoordU0, spanL.TexcoordU1, ifactor);
            xspan.TexcoordV0 = InterpolateAttrPersp(spanL.TexcoordV0, spanL.TexcoordV1, ifactor);

            xspan.ColorR0 = InterpolateAttrPersp(spanL.ColorR0, spanL.ColorR1, ifactor);
            xspan.ColorG0 = InterpolateAttrPersp(spanL.ColorG0, spanL.ColorG1, ifactor);
            xspan.ColorB0 = InterpolateAttrPersp(spanL.ColorB0, spanL.ColorB1, ifactor);

            xspan.W0 = InterpolateAttrPersp(spanL.W0, spanL.W1, ifactor);
        }
        else
        {
            xspan.TexcoordU0 = InterpolateAttrLinear(spanL.TexcoordU0, spanL.TexcoordU1, i, spanL.IRecip, idiff);
            xspan.TexcoordV0 = InterpolateAttrLinear(spanL.TexcoordV0, spanL.TexcoordV1, i, spanL.IRecip, idiff);

            xspan.ColorR0 = InterpolateAttrLinear(spanL.ColorR0, spanL.ColorR1, i, spanL.IRecip, idiff);
            xspan.ColorG0 = InterpolateAttrLinear(spanL.ColorG0, spanL.ColorG1, i, spanL.IRecip, idiff);
            xspan.ColorB0 = InterpolateAttrLinear(spanL.ColorB0, spanL.ColorB1, i, spanL.IRecip, idiff);

            xspan.W0 = spanL.W0; // linear mode is only taken when W0 == W1
        }
    }

    if (spanR.I0 == spanR.I1)
    {
        xspan.TexcoordU1 = spanR.TexcoordU0;
        xspan.TexcoordV1 = spanR.TexcoordV0;
        xspan.ColorR1 = spanR.ColorR0;
        xspan.ColorG1 = spanR.ColorG0;
        xspan.ColorB1 = spanR.ColorB0;
        xspan.Z1 = spanR.Z0;
        xspan.W1 = spanR.W0;
    }
    else
    {
        int i = y - spanR.I0;
        int ifactor = CalcYFactorY(spanR, i);
        int idiff = spanR.I1 - spanR.I0;

#ifdef ZBuffer
        xspan.Z1 = int(InterpolateZZBuffer(spanR.Z0, spanR.Z1, i, spanR.IRecip, idiff));
#else
        xspan.Z1 = int(InterpolateZWBuffer(spanR.Z0, spanR.Z1, ifactor));
#endif

        if (spanR.Linear == 0u)
        {
            xspan.TexcoordU1 = InterpolateAttrPersp(spanR.TexcoordU0, spanR.TexcoordU1, ifactor);
            xspan.TexcoordV1 = InterpolateAttrPersp(spanR.TexcoordV0, spanR.TexcoordV1, ifactor);

            xspan.ColorR1 = InterpolateAttrPersp(spanR.ColorR0, spanR.ColorR1, ifactor);
            xspan.ColorG1 = InterpolateAttrPersp(spanR.ColorG0, spanR.ColorG1, ifactor);
            xspan.ColorB1 = InterpolateAttrPersp(spanR.ColorB0, spanR.ColorB1, ifactor);

            xspan.W1 = InterpolateAttrPersp(spanR.W0, spanR.W1, ifactor);
        }
        else
        {
            xspan.TexcoordU1 = InterpolateAttrLinear(spanR.TexcoordU0, spanR.TexcoordU1, i, spanR.IRecip, idiff);
            xspan.TexcoordV1 = InterpolateAttrLinear(spanR.TexcoordV0, spanR.TexcoordV1, i, spanR.IRecip, idiff);

            xspan.ColorR1 = InterpolateAttrLinear(spanR.ColorR0, spanR.ColorR1, i, spanR.IRecip, idiff);
            xspan.ColorG1 = InterpolateAttrLinear(spanR.ColorG0, spanR.ColorG1, i, spanR.IRecip, idiff);
            xspan.ColorB1 = InterpolateAttrLinear(spanR.ColorB0, spanR.ColorB1, i, spanR.IRecip, idiff);

            xspan.W1 = spanR.W0;
        }
    }

    xspan.XRecip = 0;
    bool isLinear = xspan.W0 == xspan.W1 && ((xspan.W0 | xspan.W1) & 0x7F) == 0;
    if (isLinear)
        xspan.Flags |= XSpanSetup_Linear;

    // When W-buffering, XRecip is only consumed by linear spans; when
    // Z-buffering it is always needed.
#ifdef ZBuffer
    bool needXRecip = true;
#else
    bool needXRecip = isLinear;
#endif
    if (needXRecip)
    {
        uint r;
        xspan.XRecip = int(Div(1u << 30, uint(xspan.X1 - xspan.X0), r));
    }

    XSpanSetups[setupIndex] = xspan;
}

)";

// ---------------------------------------------------------------------------
// Binning setup
// ---------------------------------------------------------------------------
inline const std::string ClearIndirectWorkCount = R"(
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    BinResult.Store4(BinVariantWorkCountBase + id.x * 16u, uint4(1u, 1u, 0u, 0u));
}
)";

inline const std::string ClearCoarseBinMask = R"(
[numthreads(ClearCoarseBinMaskLocalSize, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    StoreBinMask(BinningCoarseMaskStart + int(id.x) * CoarseBinStride + 0, 0u);
    StoreBinMask(BinningCoarseMaskStart + int(id.x) * CoarseBinStride + 1, 0u);
}
)";

// ---------------------------------------------------------------------------
// Coarse + fine binning in one pass
// ---------------------------------------------------------------------------
inline const std::string BinCombined = R"(

bool BinPolygon(Polygon polygon, int2 topLeft, int2 botRight)
{
    bool binned = polygon.YTop <= botRight.y && polygon.YBot > topLeft.y;
    if (binned)
    {
        int polygonHeight = polygon.YBot - polygon.YTop;

        // Convex polygons: within a tile that does not contain the direction
        // change, sampling the top-most and bottom-most span is enough to bound
        // the polygon horizontally.
        int polyInnerTopY = clamp(topLeft.y - polygon.YTop, 0, max(polygonHeight - 1, 0));
        int polyInnerBotY = clamp(botRight.y - polygon.YTop, 0, max(polygonHeight - 1, 0));

        XSpanSetup xspanTop = XSpanSetups[polygon.FirstXSpan + polyInnerTopY];
        XSpanSetup xspanBot = XSpanSetups[polygon.FirstXSpan + polyInnerBotY];

        int minXL = (polygon.XMinY >= topLeft.y && polygon.XMinY <= botRight.y)
            ? polygon.XMin
            : min(xspanTop.X0, xspanBot.X0);
        binned = minXL <= botRight.x;

        if (binned)
        {
            int maxXR = (polygon.XMaxY >= topLeft.y && polygon.XMaxY <= botRight.y)
                ? polygon.XMax
                : max(xspanTop.X1, xspanBot.X1) - 1;
            binned = maxXR >= topLeft.x;
        }
    }
    return binned;
}

groupshared uint mergedMaskShared;

[numthreads(CoarseTileArea, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint localIndex : SV_GroupIndex)
{
    int groupIdx = int(groupId.x);
    int2 coarseTile = int2(groupId.yz);

    int localIdx = int(localIndex);

    if (localIdx == 0)
        mergedMaskShared = 0u;
    GroupMemoryBarrierWithGroupSync();

    int localPolygonIdx = groupIdx * 32 + localIdx;
    int polygonIdx = int(CurVariant) + localPolygonIdx;

    int2 coarseTopLeft = coarseTile * int2(CoarseTileW, CoarseTileH);
    int2 coarseBotRight = coarseTopLeft + int2(CoarseTileW - 1, CoarseTileH - 1);

    bool binned = false;
    if (localIdx < 32 && uint(localPolygonIdx) < TexWidth && uint(polygonIdx) < NumPolygons)
        binned = BinPolygon(Polygons[polygonIdx], coarseTopLeft, coarseBotRight);

    if (binned)
    {
        uint old;
        InterlockedOr(mergedMaskShared, 1u << uint(localIdx), old);
    }
    GroupMemoryBarrierWithGroupSync();
    uint mergedMask = mergedMaskShared;

    int2 fineTile = int2(localIdx & 0x7, localIdx >> 3);

    int2 fineTileTopLeft = coarseTopLeft + fineTile * int2(TileSize, TileSize);
    int2 fineTileBotRight = fineTileTopLeft + int2(TileSize - 1, TileSize - 1);

    uint binnedMask = 0u;
    while (mergedMask != 0u)
    {
        int bit = int(FindLSB(mergedMask));
        mergedMask &= ~(1u << uint(bit));

        int binPolygonIdx = int(CurVariant) + groupIdx * 32 + bit;

        if (BinPolygon(Polygons[binPolygonIdx], fineTileTopLeft, fineTileBotRight))
            binnedMask |= 1u << uint(bit);
    }

    int linearTile = fineTile.x + fineTile.y * TilesPerLine
        + coarseTile.x * CoarseTileCountX + coarseTile.y * TilesPerLine * CoarseTileCountY;

    // The host partitions consecutive polygons using a conservative sum of
    // their tile bounding boxes, so this batch cannot exceed MaxWorkTiles.
    // Dropping a layer here would differ from Software rendering.
    uint workOffset = 0u;
    if (binnedMask != 0u)
        BinResult.InterlockedAdd(BinVariantWorkCountBase + 12u, uint(countbits(binnedMask)), workOffset);

    StoreBinMask(BinningMaskStart + linearTile * BinStride + groupIdx, binnedMask);
    int coarseMaskIdx = linearTile * CoarseBinStride + (groupIdx >> 5);
    if (binnedMask != 0u)
    {
        uint old;
        BinResult.InterlockedOr(
            BinMaskAndOffsetBase + uint(BinningCoarseMaskStart + coarseMaskIdx) * 4u,
            1u << uint(groupIdx & 0x1F),
            old);
    }

    if (binnedMask != 0u)
    {
        StoreBinMask(BinningWorkOffsetsStart + linearTile * BinStride + groupIdx, workOffset);

        uint tilePositionCombined = uint(fineTileTopLeft.x) | (uint(fineTileTopLeft.y) << 16);

        int idx = 0;
        while (binnedMask != 0u)
        {
            int bit = int(FindLSB(binnedMask));
            binnedMask &= ~(1u << uint(bit));

            int workPolygonIdx = int(CurVariant) + groupIdx * 32 + bit;
            uint variantIdx = Polygons[workPolygonIdx].Variant;

            uint inVariantOffset;
            BinResult.InterlockedAdd(BinVariantWorkCountBase + variantIdx * 16u + 8u, 1u, inVariantOffset);

            WorkDescs[WorkDescsUnsortedStart + workOffset + uint(idx)] = uint2(
                tilePositionCombined,
                (uint(workPolygonIdx) & 0x7FFu) | (inVariantOffset << 11));

            idx++;
        }
    }
}
)";

inline const std::string CalcOffsets = R"(
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x < NumVariants)
    {
        if (id.x == 0u)
        {
            uint total = BinResult.Load(BinVariantWorkCountBase + 12u);
            uint4 sortArgs = uint4((total + 31u) / 32u, 1u, 1u, 0u);
            BinResult.Store4(BinSortWorkWorkCountBase, sortArgs);
            IndirectArgs.Store4(BinSortWorkWorkCountBase, sortArgs);
        }

        uint realCount = BinResult.Load(BinVariantWorkCountBase + id.x * 16u + 8u);

        uint sortedOffset;
        BinResult.InterlockedAdd(BinVariantWorkCountBase + 16u + 12u, realCount, sortedOffset);
        BinResult.Store(BinSortedWorkOffsetBase + id.x * 4u, sortedOffset);

        // Split the per-variant rasterise dispatch across the Y and Z axes so
        // neither exceeds the driver group limit. The raw count is preserved in
        // VariantWorkRealCount for the bounds check in the rasterise shader.
        BinResult.Store(BinVariantWorkRealCountBase + id.x * 4u, realCount);
        BinResult.Store(BinVariantWorkCountBase + id.x * 16u + 4u,
            (realCount + RasteriseChunkSize - 1u) / RasteriseChunkSize);
        BinResult.Store(BinVariantWorkCountBase + id.x * 16u + 8u,
            min(realCount, RasteriseChunkSize));
        IndirectArgs.Store4(
            BinVariantWorkCountBase + id.x * 16u,
            uint4(1u,
                (realCount + RasteriseChunkSize - 1u) / RasteriseChunkSize,
                min(realCount, RasteriseChunkSize), 0u));
    }
}
)";

inline const std::string SortWork = R"(
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint total = BinResult.Load(BinVariantWorkCountBase + 12u);
    if (id.x < total)
    {
        uint2 workDesc = WorkDescs[WorkDescsUnsortedStart + id.x];
        uint inVariantOffset = workDesc.y >> 11;
        uint polygonIdx = workDesc.y & 0x7FFu;
        uint variantIdx = Polygons[polygonIdx].Variant;

        uint sortedIndex = BinResult.Load(BinSortedWorkOffsetBase + variantIdx * 4u) + inVariantOffset;
        WorkDescs[WorkDescsSortedStart + sortedIndex] = uint2(
            workDesc.x,
            (workDesc.y & 0x7FFu) | (id.x << 11));
    }
}
)";

// ---------------------------------------------------------------------------
// Per-variant tile rasterization
// ---------------------------------------------------------------------------
inline const std::string Rasterise = R"(

int CalcYFactorX(XSpanSetup span, int x)
{
    x -= span.X0;

    int factor = 0;
    if (span.X0 != span.X1)
    {
        uint numLo = uint(x) * uint(span.W0);
        uint numHi = 0u;
        numHi |= numLo >> (32u - YFactorShift);
        numLo <<= YFactorShift;

        uint den = uint(x) * uint(span.W0) + uint(span.X1 - span.X0 - x) * uint(span.W1);

        if (den != 0u)
            factor = int(Div64_32_32(numHi, numLo, den));
    }
    return factor;
}

#ifdef UseTexture
// HLSL cannot Sample() a UINT texture, so GL_REPEAT / GL_MIRRORED_REPEAT /
// GL_CLAMP_TO_EDGE are reproduced here with integer math. Every texture the
// cache hands out is power-of-two sized, which is what makes the masking form
// exact.
int WrapTexCoord(int c, int size, uint mode)
{
    int wrapped = clamp(c, 0, size - 1);
    if (mode == 1u)
        wrapped = c & (size - 1);
    else if (mode == 2u)
    {
        int m = c & ((size << 1) - 1);
        wrapped = (m >= size) ? ((size << 1) - 1 - m) : m;
    }
    return wrapped;
}

uint4 SampleTexture(int u, int v, uint layer)
{
    uint4 result = uint4(0u, 0u, 0u, 0u);
    // Rasterise reuses the InterpSpans-only constants as
    // captureType/captureYOffset/captureReference. This keeps the root
    // constant ABI at eight DWORDs for every compute pipeline.
    if (InterpSpanBase != 0u)
    {
        int captureWidth = InterpSpanBase == 1u ? 128 : 256;
        int scaledWidth = captureWidth * int(ScaleFactor);
        int sx = int(floor(float(u) * float(ScaleFactor) / 16.0f));
        int sy = int(floor(
            (float(v) / 16.0f + float(int(InterpSpanCount)))
            * float(scaledWidth) / float(TexHeight)));
        sx = WrapTexCoord(sx, scaledWidth, TexWrapS);
        sy = WrapTexCoord(sy, scaledWidth, TexWrapT);

        uint scale = ScaleFactor;
        uint sxu = uint(sx);
        uint syu = uint(sy);
        uint address = (DispatchPad & 0xFFFFu)
            + (syu / scale) * uint(captureWidth)
            + (sxu / scale);
        uint reference = (DispatchPad & 0xFFFF0000u) | (address & 0xFFFFu);
        uint bank = (reference >> 28u) & 3u;
        uint version = (reference >> 30u) & 1u;
        uint cell = ((version * 4u + bank) * 65536u) + address;
        uint sample = (syu % scale) * scale + (sxu % scale);
        uint packed = CaptureSidecarBuffer[cell * scale * scale + sample];
        result = uint4(
            packed & 0x3Fu,
            (packed >> 8u) & 0x3Fu,
            (packed >> 16u) & 0x3Fu,
            (packed >> 24u) & 0x1Fu);
    }
    else
    {
        // The GL renderer normalizes by the texture size and relies on NEAREST
        // filtering; floor(u/16) is the same texel and avoids the float round trip.
        int iu = WrapTexCoord(u >> 4, int(TexWidth), TexWrapS);
        int iv = WrapTexCoord(v >> 4, int(TexHeight), TexWrapT);
        result = CurrentTexture.Load(int4(iu, iv, int(layer), 0));
    }
    return result;
}
#endif

[numthreads(TileSize, TileSize, 1)]
void main(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    // The dispatch is split as (1, ceil(count/chunk), min(count, chunk)) to
    // respect the group-count limit. Rebuild the linear work index and drop the
    // overdispatched tail groups. The early return only depends on the group
    // id and the shader has no barriers, so it is uniform and safe.
    uint linearWorkIdx = groupId.y * RasteriseChunkSize + groupId.z;
    if (linearWorkIdx >= BinResult.Load(BinVariantWorkRealCountBase + CurVariant * 4u))
        return;

    uint sortedBase = BinResult.Load(BinSortedWorkOffsetBase + CurVariant * 4u);
    uint2 workDesc = WorkDescs[WorkDescsSortedStart + sortedBase + linearWorkIdx];

    Polygon polygon = Polygons[workDesc.y & 0x7FFu];
    int2 position = int2(int(workDesc.x & 0xFFFFu), int(workDesc.x >> 16)) + int2(localId.xy);
    uint tileOffset = (workDesc.y >> 11) * uint(TileSize * TileSize)
        + uint(TileSize) * localId.y + localId.x;

    uint color = 0u;
    if (position.y >= polygon.YTop && position.y < polygon.YBot)
    {
        XSpanSetup xspan = XSpanSetups[polygon.FirstXSpan + (position.y - polygon.YTop)];

        bool insideLeftEdge = position.x < xspan.InsideStart;
        bool insideRightEdge = position.x >= xspan.InsideEnd;
        bool insidePolygonInside = !insideLeftEdge && !insideRightEdge;

        if (position.x >= xspan.X0 && position.x < xspan.X1
            && ((insideLeftEdge && (xspan.Flags & XSpanSetup_FillLeft) != 0u)
                || (insideRightEdge && (xspan.Flags & XSpanSetup_FillRight) != 0u)
                || (insidePolygonInside && (xspan.Flags & XSpanSetup_FillInside) != 0u)))
        {
            uint attr = 0u;
            if (position.y == polygon.YTop)
                attr |= 0x4u;
            else if (position.y == polygon.YBot - 1)
                attr |= 0x8u;

            if (insideLeftEdge)
            {
                attr |= 0x1u;

                int cov = xspan.EdgeCovL;
                if (cov < 0)
                {
                    int coverageStart = max(xspan.X0, 0);
                    int xcov = xspan.CovLInitial
                        + (xspan.EdgeCovL & 0x3FF) * (position.x - coverageStart);
                    cov = min(xcov >> 5, 31);
                }

                attr |= uint(cov) << 8;
            }
            else if (insideRightEdge)
            {
                attr |= 0x2u;

                int cov = xspan.EdgeCovR;
                if (cov < 0)
                {
                    int coverageStart = max(max(xspan.InsideStart, xspan.InsideEnd), 0);
                    int xcov = xspan.CovRInitial
                        + (xspan.EdgeCovR & 0x3FF) * (position.x - coverageStart);
                    // Keep this signed: hexadecimal HLSL literals are unsigned.
                    cov = max(31 - (xcov >> 5), 0);
                }

                attr |= uint(cov) << 8;
            }
            else if ((DispCnt & (1u << 4)) != 0u && (attr & 0xFu) != 0u)
            {
                attr |= 0x1Fu << 8;
            }

            uint z;
            int u, v, vr, vg, vb;

            if (xspan.X0 == xspan.X1)
            {
                z = uint(xspan.Z0);
                u = xspan.TexcoordU0;
                v = xspan.TexcoordV0;
                vr = xspan.ColorR0;
                vg = xspan.ColorG0;
                vb = xspan.ColorB0;
            }
            else
            {
                int ifactor = CalcYFactorX(xspan, position.x);
                int idiff = xspan.X1 - xspan.X0;
                int i = position.x - xspan.X0;

#ifdef ZBuffer
                z = InterpolateZZBuffer(xspan.Z0, xspan.Z1, i, xspan.XRecip, idiff);
#else
                z = InterpolateZWBuffer(xspan.Z0, xspan.Z1, ifactor);
#endif
                if ((xspan.Flags & XSpanSetup_Linear) == 0u)
                {
                    u = InterpolateAttrPersp(xspan.TexcoordU0, xspan.TexcoordU1, ifactor);
                    v = InterpolateAttrPersp(xspan.TexcoordV0, xspan.TexcoordV1, ifactor);

                    vr = InterpolateAttrPersp(xspan.ColorR0, xspan.ColorR1, ifactor);
                    vg = InterpolateAttrPersp(xspan.ColorG0, xspan.ColorG1, ifactor);
                    vb = InterpolateAttrPersp(xspan.ColorB0, xspan.ColorB1, ifactor);
                }
                else
                {
                    u = InterpolateAttrLinear(xspan.TexcoordU0, xspan.TexcoordU1, i, xspan.XRecip, idiff);
                    v = InterpolateAttrLinear(xspan.TexcoordV0, xspan.TexcoordV1, i, xspan.XRecip, idiff);

                    vr = InterpolateAttrLinear(xspan.ColorR0, xspan.ColorR1, i, xspan.XRecip, idiff);
                    vg = InterpolateAttrLinear(xspan.ColorG0, xspan.ColorG1, i, xspan.XRecip, idiff);
                    vb = InterpolateAttrLinear(xspan.ColorB0, xspan.ColorB1, i, xspan.XRecip, idiff);
                }
            }

#ifndef ShadowMask
            vr >>= 3;
            vg >>= 3;
            vb >>= 3;

            uint r, g, b;
            uint a = 0u;
            uint polyalpha = (polygon.Attr >> 16) & 0x1Fu;

#if defined(Toon)
            {
                uint tooncolor = ToonTable[vr >> 1].x;
                vr = int(tooncolor & 0xFFu);
                vg = int((tooncolor >> 8) & 0xFFu);
                vb = int((tooncolor >> 16) & 0xFFu);
            }
#endif
#if defined(Highlight)
            vg = vr;
            vb = vr;
#endif

#ifdef NoTexture
            a = polyalpha;
#endif
            r = uint(vr);
            g = uint(vg);
            b = uint(vb);

#ifdef UseTexture
            // Software narrows interpolated S/T to signed 16-bit before
            // converting from 12.4 fixed point.
            u = (u << 16) >> 16;
            v = (v << 16) >> 16;

            uint4 texcolor = SampleTexture(u, v, uint(polygon.TextureLayer));

#ifdef Decal
            if (texcolor.a == 31u)
            {
                r = texcolor.r;
                g = texcolor.g;
                b = texcolor.b;
            }
            else if (texcolor.a > 0u)
            {
                r = ((texcolor.r * texcolor.a) + (uint(vr) * (31u - texcolor.a))) >> 5;
                g = ((texcolor.g * texcolor.a) + (uint(vg) * (31u - texcolor.a))) >> 5;
                b = ((texcolor.b * texcolor.a) + (uint(vb) * (31u - texcolor.a))) >> 5;
            }
            a = polyalpha;
#endif
#if defined(Modulate) || defined(Toon) || defined(Highlight)
            r = ((texcolor.r + 1u) * uint(vr + 1) - 1u) >> 6;
            g = ((texcolor.g + 1u) * uint(vg + 1) - 1u) >> 6;
            b = ((texcolor.b + 1u) * uint(vb + 1) - 1u) >> 6;
            a = ((texcolor.a + 1u) * (polyalpha + 1u) - 1u) >> 5;
#endif
#endif // UseTexture

#ifdef Highlight
            {
                uint tooncolor = ToonTable[vr >> 1].x;

                r = min(r + (tooncolor & 0xFFu), 63u);
                g = min(g + ((tooncolor >> 8) & 0xFFu), 63u);
                b = min(b + ((tooncolor >> 16) & 0xFFu), 63u);
            }
#endif

            if (polyalpha == 0u)
                a = 31u;

            if (a > AlphaRef)
            {
                color = r | (g << 8) | (b << 16) | (a << 24);

                DepthTiles[tileOffset] = z;
                AttrTiles[tileOffset] = attr;
            }
#else // ShadowMask
            color = 0xFFFFFFFFu; // any nonzero value works as the "covered" flag
            DepthTiles[tileOffset] = z;
#endif
        }
    }

    ColorTiles[tileOffset] = color;
}
)";

// ---------------------------------------------------------------------------
// Per-pixel depth test, blending and clear plane
// ---------------------------------------------------------------------------
inline const std::string DepthBlend = R"(

bool DepthTestEqual(uint dstDepth, uint tileDepth)
{
#ifdef WBuffer
    return (dstDepth - tileDepth + 0xFFu) <= 0x1FEu;
#else
    return (dstDepth - tileDepth + 0x200u) <= 0x400u;
#endif
}

bool DepthTestPasses(bool equalDepthTest, bool facingView, uint dstDepth, uint tileDepth, uint dstAttr)
{
    bool passes = false;
    if (equalDepthTest)
        passes = DepthTestEqual(dstDepth, tileDepth);
    else if (facingView && (dstAttr & 0x00400010u) == 0x00000010u)
        passes = tileDepth <= dstDepth;
    else
        passes = tileDepth < dstDepth;
    return passes;
}

void PlotTranslucent(inout uint color, inout uint depth, inout uint attr, bool isShadow,
    uint tileColor, uint srcA, uint tileDepth, uint srcAttr, bool writeDepth)
{
    uint blendAttr = (srcAttr & 0xE0F0u) | ((srcAttr >> 8) & 0xFF0000u) | (1u << 22) | (attr & 0xFF001F0Fu);

    if ((!isShadow || (attr & (1u << 22)) != 0u)
        ? (attr & 0x007F0000u) != (blendAttr & 0x007F0000u)
        : (attr & 0x3F000000u) != (srcAttr & 0x3F000000u))
    {
        if (writeDepth)
            depth = tileDepth;

        if ((attr & (1u << 15)) == 0u)
            blendAttr &= ~(1u << 15);
        attr = blendAttr;

        uint srcRB = tileColor & 0x3F003Fu;
        uint srcG = tileColor & 0x003F00u;
        uint dstRB = color & 0x3F003Fu;
        uint dstG = color & 0x003F00u;
        uint dstA = color & 0x1F000000u;

        uint alpha = (srcA >> 24) + 1u;
        if (dstA != 0u && (DispCnt & (1u << 3)) != 0u)
        {
            srcRB = ((srcRB * alpha) + (dstRB * (32u - alpha))) >> 5;
            srcG = ((srcG * alpha) + (dstG * (32u - alpha))) >> 5;
        }

        color = (srcRB & 0x3F003Fu) | (srcG & 0x003F00u) | max(dstA, srcA);
    }
}

void ProcessCoarseMask(int linearTile, uint coarseMask, uint coarseOffset, uint2 localId,
    inout uint2 color, inout uint2 depth, inout uint2 attr, inout uint2 winner,
    bool trackCoverage, inout uint stencil,
    inout bool prevIsShadowMask)
{
    uint tileInnerOffset = localId.x + localId.y * uint(TileSize);

    while (coarseMask != 0u)
    {
        uint coarseBit = FindLSB(coarseMask);
        coarseMask &= ~(1u << coarseBit);

        int tileOffset = linearTile * BinStride + int(coarseBit + coarseOffset);

        uint fineMask = LoadBinMask(BinningMaskStart + tileOffset);
        uint workIdx = LoadBinMask(BinningWorkOffsetsStart + tileOffset);

        while (fineMask != 0u)
        {
            uint fineIdx = FindLSB(fineMask);
            fineMask &= ~(1u << fineIdx);

            uint pixelindex = tileInnerOffset + workIdx * uint(TileSize * TileSize);
            uint tileColor = ColorTiles[pixelindex];
            workIdx++;

            uint polygonIdx = CurVariant + fineIdx + (coarseBit + coarseOffset) * 32u;

            if (tileColor != 0u)
            {
                uint polygonAttr = Polygons[polygonIdx].Attr;
                bool facingView = Polygons[polygonIdx].FacingView != 0u;

                bool isShadowMask = ((polygonAttr & 0x3F000030u) == 0x00000030u);
                bool prevIsShadowMaskOld = prevIsShadowMask;
                prevIsShadowMask = isShadowMask;

                bool equalDepthTest = (polygonAttr & (1u << 14)) != 0u;

                uint tileDepth = DepthTiles[pixelindex];
                uint tileAttr = AttrTiles[pixelindex];

                uint dstattr = attr.x;

                if (!isShadowMask)
                {
                    bool isShadow = (polygonAttr & 0x30u) == 0x30u;

                    bool writeSecondLayer = false;

                    if (isShadow)
                    {
                        if (stencil == 0u)
                            continue;
                        if ((stencil & 1u) == 0u)
                            writeSecondLayer = true;
                        if ((stencil & 2u) == 0u)
                            dstattr &= ~0x3u;
                    }

                    uint dstDepth = writeSecondLayer ? depth.y : depth.x;
                    if (!DepthTestPasses(equalDepthTest, facingView, dstDepth, tileDepth, dstattr))
                    {
                        if ((dstattr & 0xFu) == 0u || writeSecondLayer)
                            continue;

                        writeSecondLayer = true;
                        dstattr = attr.y;
                        if (!DepthTestPasses(equalDepthTest, facingView, depth.y, tileDepth, dstattr))
                            continue;
                    }

                    uint srcAttr = (polygonAttr & 0x3F008000u);
                    if (!facingView)
                        srcAttr |= 1u << 4;

                    uint srcA = tileColor & 0x1F000000u;
                    if (srcA == 0x1F000000u)
                    {
                        srcAttr |= tileAttr;

                        if (!writeSecondLayer)
                        {
                            if ((srcAttr & 0xFu) != 0u)
                            {
                                color.y = color.x;
                                depth.y = depth.x;
                                attr.y = attr.x;
                                if (trackCoverage)
                                    winner.y = winner.x;
                            }

                            color.x = tileColor;
                            depth.x = tileDepth;
                            attr.x = srcAttr;
                            if (trackCoverage)
                                winner.x = polygonIdx;
                        }
                        else
                        {
                            color.y = tileColor;
                            depth.y = tileDepth;
                            attr.y = srcAttr;
                            if (trackCoverage)
                                winner.y = polygonIdx;
                        }
                        if (trackCoverage)
                            AttrTiles[pixelindex] = tileAttr | 0x80000000u;
                    }
                    else
                    {
                        bool writeDepth = (polygonAttr & (1u << 11)) != 0u;

                        if (!writeSecondLayer)
                            PlotTranslucent(color.x, depth.x, attr.x, isShadow, tileColor, srcA, tileDepth, srcAttr, writeDepth);
                        if (writeSecondLayer || (dstattr & 0xFu) != 0u)
                            PlotTranslucent(color.y, depth.y, attr.y, isShadow, tileColor, srcA, tileDepth, srcAttr, writeDepth);
                    }
                }
                else
                {
                    if (!prevIsShadowMaskOld)
                        stencil = 0u;

                    if (!DepthTestPasses(equalDepthTest, facingView, depth.x, tileDepth, attr.x))
                        stencil = 0x1u;

                    if ((dstattr & 0xFu) != 0u)
                    {
                        if (!DepthTestPasses(equalDepthTest, facingView, depth.y, tileDepth, attr.y))
                            stencil |= 0x2u;
                    }
                }
            }
        }
    }
}

[numthreads(TileSize, TileSize, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    int linearTile = int(groupId.x + (groupId.y * uint(TilesPerLine)));

    uint coarseMaskLo = LoadBinMask(BinningCoarseMaskStart + linearTile * CoarseBinStride + 0);
    uint coarseMaskHi = LoadBinMask(BinningCoarseMaskStart + linearTile * CoarseBinStride + 1);

    uint resultOffset = id.x + id.y * ScreenWidth;
    uint2 color, depth, attr;
    uint2 winner = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
    bool trackCoverage = ScreenWidth == 256u && (DispCnt & (1u << 4)) != 0u;
    uint stencil = 0u;
    bool prevIsShadowMask = false;

    if (TexHeight != 0u)
    {
        color = uint2(
            ResultValue[ResultColorStart + resultOffset],
            ResultValue[ResultColorStart + resultOffset + FramebufferStride]);
        depth = uint2(
            ResultValue[ResultDepthStart + resultOffset],
            ResultValue[ResultDepthStart + resultOffset + FramebufferStride]);
        attr = uint2(
            ResultValue[ResultAttrStart + resultOffset],
            ResultValue[ResultAttrStart + resultOffset + FramebufferStride]);
        if (trackCoverage)
        {
            winner = uint2(
                ResultWinner[resultOffset],
                ResultWinner[resultOffset + FramebufferStride]);
        }
        uint continuation = BlendContinuationState[resultOffset];
        stencil = continuation & 0x3u;
        prevIsShadowMask = (continuation & 0x4u) != 0u;
    }
    else if ((DispCnt & (1u << 14)) != 0u)
    {
        attr = uint2(ClearAttr, 0u);
        // The GL renderer divides both axes by ScreenWidth so the 256x256
        // bitmap keeps square texels over the 256x192 screen. GL_REPEAT plus
        // GL_NEAREST is reproduced with an explicit wrap and a Load().
        float scale = 1.0f / float(ScreenWidth);
        float2 pos = (float2(id.xy) * scale) + ClearBitmapOffset;
        int2 texel = int2(floor(pos * 256.0f)) & 255;

        color = uint2(ClearBitmapColor.Load(int3(texel, 0)), 0u);
        depth = uint2(ClearBitmapDepth.Load(int3(texel, 0)), 0u);
        attr.x = (attr.x & ~0x8000u) | ((depth.x >> 9) & 0x8000u);
        depth.x &= 0xFFFFFFu;
    }
    else
    {
        color = uint2(ClearColor, 0u);
        depth = uint2(ClearDepth, 0u);
        attr = uint2(ClearAttr, 0u);
    }

    if (TexHeight == 0u)
    {
        stencil = 0u;
        prevIsShadowMask = false;
    }

    ProcessCoarseMask(linearTile, coarseMaskLo, 0u, localId.xy, color, depth, attr, winner, trackCoverage, stencil, prevIsShadowMask);
    ProcessCoarseMask(linearTile, coarseMaskHi, uint(BinStride / 2), localId.xy, color, depth, attr, winner, trackCoverage, stencil, prevIsShadowMask);

    ResultValue[ResultColorStart + resultOffset] = color.x;
    ResultValue[ResultColorStart + resultOffset + FramebufferStride] = color.y;
    ResultValue[ResultDepthStart + resultOffset] = depth.x;
    ResultValue[ResultDepthStart + resultOffset + FramebufferStride] = depth.y;
    ResultValue[ResultAttrStart + resultOffset] = attr.x;
    ResultValue[ResultAttrStart + resultOffset + FramebufferStride] = attr.y;
    if (trackCoverage)
    {
        ResultWinner[resultOffset] = winner.x;
        ResultWinner[resultOffset + FramebufferStride] = winner.y;
    }
    BlendContinuationState[resultOffset] = stencil | (prevIsShadowMask ? 0x4u : 0u);
}
)";

// ---------------------------------------------------------------------------
// Native-resolution accepted-pixel AA coverage correction
// ---------------------------------------------------------------------------
inline const std::string CorrectCoverage = R"(

bool CoverageWasAccepted(uint polygonIndex, uint x, uint y)
{
    uint tileX = x / uint(TileSize);
    uint tileY = y / uint(TileSize);
    uint linearTile = tileX + tileY * TilesPerLine;
    uint localPolygon = polygonIndex - CurVariant;
    uint group = localPolygon >> 5u;
    uint bit = localPolygon & 31u;
    int maskIndex = int(linearTile * uint(BinStride) + group);
    uint mask = LoadBinMask(BinningMaskStart + maskIndex);
    bool accepted = false;
    if ((mask & (1u << bit)) != 0u)
    {
        uint lowerMask = bit == 0u ? 0u : ((1u << bit) - 1u);
        uint ordinal = countbits(mask & lowerMask);
        uint workIndex = LoadBinMask(BinningWorkOffsetsStart + maskIndex) + ordinal;
        if (workIndex < MaxWorkTiles)
        {
            uint tileInner =
                (y % uint(TileSize)) * uint(TileSize) + (x % uint(TileSize));
            uint tilePixel = workIndex * uint(TileSize * TileSize) + tileInner;
            accepted = ColorTiles[tilePixel] != 0u
                && (AttrTiles[tilePixel] & 0x80000000u) != 0u;
        }
    }
    return accepted;
}

void WriteWinningCoverage(uint polygonIndex, uint x, uint y, uint coverage)
{
    uint pixel = y * ScreenWidth + x;
    uint target = 0xFFFFFFFFu;
    if (ResultWinner[pixel] == polygonIndex)
        target = pixel;
    else if (ResultWinner[FramebufferStride + pixel] == polygonIndex)
        target = FramebufferStride + pixel;

    if (target != 0xFFFFFFFFu)
    {
        uint attr = ResultValue[ResultAttrStart + target];
        ResultValue[ResultAttrStart + target] =
            (attr & ~0x1F00u) | ((coverage & 0x1Fu) << 8u);
    }
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint setupIndex = id.x;
    if (setupIndex >= TexHeight)
        return;

    uint4 setup = SetupIndices[setupIndex];
    uint polygonIndex = setup.x;
    if (polygonIndex < CurVariant || polygonIndex >= CurVariant + TexWidth)
        return;

    XSpanSetup span = XSpanSetups[setupIndex];
    uint y = setup.w;
    if (y >= ScreenHeight)
        return;

    int leftStart = max(span.X0, 0);
    int screenEnd = int(ScreenWidth);
    int leftEnd = min(min(span.InsideStart, span.X1), screenEnd);
    if (span.EdgeCovL < 0)
    {
        int xcov = span.CovLInitial;
        for (int x = leftStart; x < leftEnd; ++x)
        {
            if (CoverageWasAccepted(polygonIndex, uint(x), y))
            {
                WriteWinningCoverage(
                    polygonIndex, uint(x), y, min(uint(xcov >> 5), 31u));
                xcov += span.EdgeCovL & 0x3FF;
            }
        }
    }

    int bodyEnd = min(min(span.InsideEnd, span.X1), screenEnd);
    int rightStart = max(max(leftStart, leftEnd), bodyEnd);
    int rightEnd = min(span.X1, screenEnd);
    if (span.EdgeCovR < 0)
    {
        int xcov = span.CovRInitial;
        for (int x = rightStart; x < rightEnd; ++x)
        {
            if (CoverageWasAccepted(polygonIndex, uint(x), y))
            {
                // Hex literals are unsigned in HLSL. Keep the subtraction
                // signed so coverage below zero clamps to zero instead of
                // wrapping to 0xFFFFFFFF (whose low five bits are 31).
                WriteWinningCoverage(
                    polygonIndex, uint(x), y,
                    uint(max(31 - (xcov >> 5), 0)));
                xcov += span.EdgeCovR & 0x3FF;
            }
        }
    }
}
)";

// ---------------------------------------------------------------------------
// Final pass: edge marking, fog, anti-aliasing resolve
// ---------------------------------------------------------------------------
inline const std::string FinalPass = R"(

uint BlendFog(uint color, uint depth)
{
    uint densityid = 0u, densityfrac = 0u;

    if (depth >= FogOffset)
    {
        depth -= FogOffset;
        depth = (depth >> 2) << FogShift;

        densityid = depth >> 17;
        if (densityid >= 32u)
        {
            densityid = 32u;
            densityfrac = 0u;
        }
        else
        {
            densityfrac = depth & 0x1FFFFu;
        }
    }

    uint density =
        ((ToonTable[densityid].y * (0x20000u - densityfrac)) +
         (ToonTable[densityid + 1u].y * densityfrac)) >> 17;
    density = min(density, 128u);

    uint colorR = color & 0x3Fu;
    uint colorG = (color >> 8) & 0x3Fu;
    uint colorB = (color >> 16) & 0x3Fu;
    uint colorA = (color >> 24) & 0x1Fu;

    uint fogR = FogColor & 0x3Fu;
    uint fogG = (FogColor >> 8) & 0x3Fu;
    uint fogB = (FogColor >> 16) & 0x3Fu;
    uint fogA = (FogColor >> 24) & 0x1Fu;

    if ((DispCnt & (1u << 6)) == 0u)
    {
        colorR = ((colorR * (128u - density)) + (fogR * density)) >> 7;
        colorG = ((colorG * (128u - density)) + (fogG * density)) >> 7;
        colorB = ((colorB * (128u - density)) + (fogB * density)) >> 7;
    }
    colorA = ((colorA * (128u - density)) + (fogA * density)) >> 7;

    return colorR | (colorG << 8) | (colorB << 16) | (colorA << 24);
}

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ScreenWidth || id.y >= ScreenHeight)
        return;

    uint srcX = id.x;
    uint resultOffset = srcX + id.y * ScreenWidth;

    uint2 color = uint2(
        ResultValue[ResultColorStart + resultOffset],
        ResultValue[ResultColorStart + resultOffset + FramebufferStride]);
    uint2 depth = uint2(
        ResultValue[ResultDepthStart + resultOffset],
        ResultValue[ResultDepthStart + resultOffset + FramebufferStride]);
    uint2 attr = uint2(
        ResultValue[ResultAttrStart + resultOffset],
        ResultValue[ResultAttrStart + resultOffset + FramebufferStride]);

#ifdef EdgeMarking
    if ((attr.x & 0xFu) != 0u)
    {
        uint4 otherAttr = uint4(ClearAttr, ClearAttr, ClearAttr, ClearAttr);
        uint4 otherDepth = uint4(ClearDepth, ClearDepth, ClearDepth, ClearDepth);

        if (srcX > 0u)
        {
            otherAttr.x = ResultValue[resultOffset - 1u + ResultAttrStart];
            otherDepth.x = ResultValue[resultOffset - 1u + ResultDepthStart];
        }
        if (srcX < ScreenWidth - 1u)
        {
            otherAttr.y = ResultValue[resultOffset + 1u + ResultAttrStart];
            otherDepth.y = ResultValue[resultOffset + 1u + ResultDepthStart];
        }
        if (id.y > 0u)
        {
            otherAttr.z = ResultValue[resultOffset - ScreenWidth + ResultAttrStart];
            otherDepth.z = ResultValue[resultOffset - ScreenWidth + ResultDepthStart];
        }
        if (id.y < ScreenHeight - 1u)
        {
            otherAttr.w = ResultValue[resultOffset + ScreenWidth + ResultAttrStart];
            otherDepth.w = ResultValue[resultOffset + ScreenWidth + ResultDepthStart];
        }

        uint polyId = (attr.x >> 24) & 0x3Fu;
        uint4 otherPolyId = (otherAttr >> 24) & 0x3Fu;

        bool4 polyIdMismatch = otherPolyId != uint4(polyId, polyId, polyId, polyId);
        bool4 nearer = uint4(depth.x, depth.x, depth.x, depth.x) < otherDepth;

        if ((polyIdMismatch.x && nearer.x)
            || (polyIdMismatch.y && nearer.y)
            || (polyIdMismatch.z && nearer.z)
            || (polyIdMismatch.w && nearer.w))
        {
            color.x = ToonTable[polyId >> 3].z | (color.x & 0xFF000000u);
            attr.x = (attr.x & 0xFFFFE0FFu) | 0x00001000u;
        }
    }
#endif

#ifdef Fog
    if ((attr.x & (1u << 15)) != 0u)
        color.x = BlendFog(color.x, depth.x);

    if ((attr.x & 0xFu) != 0u && (attr.y & (1u << 15)) != 0u)
        color.y = BlendFog(color.y, depth.y);
#endif

#ifdef AntiAliasing
    if ((attr.x & 0x3u) != 0u)
    {
        uint coverage = (attr.x >> 8) & 0x1Fu;

        if (coverage != 0u)
        {
            uint topRB = color.x & 0x3F003Fu;
            uint topG = color.x & 0x003F00u;
            uint topA = (color.x >> 24) & 0x1Fu;

            uint botRB = color.y & 0x3F003Fu;
            uint botG = color.y & 0x003F00u;
            uint botA = (color.y >> 24) & 0x1Fu;

            coverage++;

            if (botA > 0u)
            {
                topRB = ((topRB * coverage) + (botRB * (32u - coverage))) >> 5;
                topG = ((topG * coverage) + (botG * (32u - coverage))) >> 5;

                topRB &= 0x3F003Fu;
                topG &= 0x003F00u;
            }

            topA = ((topA * coverage) + (botA * (32u - coverage))) >> 5;

            color.x = topRB | topG | (topA << 24);
        }
        else
        {
            color.x = color.y;
        }
    }
#endif

    // Unlike the OpenGL renderer this writes the packed r6g6b6a5 word straight
    // out: the software 2D compositor consumes exactly that encoding, so no
    // UNORM round-trip is involved.
    FinalFB[resultOffset] = color.x;
}
)";

// ---------------------------------------------------------------------------
// Downscale to the DS's native resolution
// ---------------------------------------------------------------------------
inline const std::string Resolve = R"(
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 256u || id.y >= 192u)
        return;

    uint outOffset = id.x + id.y * 256u;

    // Match OpenGL CaptureDownscaleFS: GL_NEAREST at the native output pixel
    // centre, before RGB5551 conversion by the software capture path.
    uint centre = ScaleFactor >> 1u;
    uint sourceX = id.x * ScaleFactor + centre;
    uint sourceY = id.y * ScaleFactor + centre;
    ResolveOut[outOffset] = FinalFB[sourceX + sourceY * ScreenWidth];
}
)";

// ---------------------------------------------------------------------------
// High-resolution Display Capture sidecar
// ---------------------------------------------------------------------------
inline const std::string CaptureSidecar = R"(
static const uint CapPixelCount = 256u * 192u;
static const uint CapSourceBase = CapPixelCount * 8u;
static const uint CapSourceBNativeBase = CapPixelCount * 12u;
static const uint CapSourceBReferenceBase = CapPixelCount * 13u;
static const uint CapLineMetaBase = CapPixelCount * 14u;
static const uint CapCommandBase = CapLineMetaBase + 384u;

uint CapR(uint c) { return c & 0x3Fu; }
uint CapG(uint c) { return (c >> 8u) & 0x3Fu; }
uint CapB(uint c) { return (c >> 16u) & 0x3Fu; }
uint CapPack(uint r, uint g, uint b, uint a)
{
    return min(r, 63u) | (min(g, 63u) << 8u) | (min(b, 63u) << 16u) | (a << 24u);
}
uint CapBlend4(uint a, uint b, uint eva, uint evb)
{
    return CapPack(((CapR(a)*eva)+(CapR(b)*evb)+8u)>>4u,
        ((CapG(a)*eva)+(CapG(b)*evb)+8u)>>4u,
        ((CapB(a)*eva)+(CapB(b)*evb)+8u)>>4u, 0xFFu);
}
uint CapBlend5(uint a, uint b)
{
    uint eva = ((a >> 24u) & 0x1Fu) + 1u;
    uint evb = 32u - eva;
    return CapPack(((CapR(a)*eva)+(CapR(b)*evb)+16u)>>5u,
        ((CapG(a)*eva)+(CapG(b)*evb)+16u)>>5u,
        ((CapB(a)*eva)+(CapB(b)*evb)+16u)>>5u, 0xFFu);
}
uint CapBrightnessUp(uint c, uint f)
{
    return CapPack(CapR(c)+((((63u-CapR(c))*f)+8u)>>4u),
        CapG(c)+((((63u-CapG(c))*f)+8u)>>4u),
        CapB(c)+((((63u-CapB(c))*f)+8u)>>4u), 0xFFu);
}
uint CapBrightnessDown(uint c, uint f)
{
    return CapPack(CapR(c)-(((CapR(c)*f)+7u)>>4u),
        CapG(c)-(((CapG(c)*f)+7u)>>4u),
        CapB(c)-(((CapB(c)*f)+7u)>>4u), 0xFFu);
}
uint CapLoad(uint reference, uint2 within)
{
    uint address = reference & 0xFFFFu;
    uint bank = (reference >> 28u) & 3u;
    uint version = (reference >> 30u) & 1u;
    uint spp = ScaleFactor * ScaleFactor;
    uint cell = ((version * 4u + bank) * 65536u) + address;
    return CaptureSidecarBuffer[cell * spp + within.y * ScaleFactor + within.x];
}
uint CapLoadSource3D(uint2 position, uint lineMeta)
{
    uint xpos = (lineMeta >> 23u) & 0x1FFu;
    int sx = (xpos & 0x100u) != 0u
        ? int(position.x) - int((512u - xpos) * ScaleFactor)
        : int(position.x) + int(xpos * ScaleFactor);
    return TexWidth != 0u && sx >= 0 && sx < int(ScreenWidth)
        ? FinalFB[position.y * ScreenWidth + uint(sx)]
        : 0u;
}
uint CapComposeSourceA(
    uint2 position,
    uint nativeIndex,
    uint lineMeta,
    bool source3DValid)
{
    uint below = ResultValue[CapSourceBase + nativeIndex];
    uint above = ResultValue[CapSourceBase + CapPixelCount + nativeIndex];
    uint control = ResultValue[CapSourceBase + CapPixelCount * 2u + nativeIndex];
    uint reference = ResultValue[CapSourceBase + CapPixelCount * 3u + nativeIndex];
    uint flags = control >> 24u;
    uint result = below;
    if ((flags & 0x40u) != 0u)
    {
        uint slot = 0u;
        if ((reference & 0x80000000u) != 0u)
            slot = CapLoad(reference, position % ScaleFactor);
        else if (source3DValid)
            slot = CapLoadSource3D(position, lineMeta);
        if (((slot >> 24u) & 0x1Fu) != 0u)
        {
            uint mode = flags & 0xFu;
            uint eva = (control >> 8u) & 0x1Fu;
            uint evb = (control >> 16u) & 0x1Fu;
            if (mode == 1u && (flags & 0x80u) != 0u)
                result = CapBlend4(above, slot, eva, evb);
            else if (mode == 2u)
                result = CapBrightnessUp(slot, eva);
            else if (mode == 3u)
                result = CapBrightnessDown(slot, eva);
            else if (mode == 4u)
                result = CapBlend5(slot, below);
            else
                result = slot;
        }
    }
    return result;
}
uint CapNormalizeCapturedPixel(uint color)
{
    uint r = ((color & 0x3Fu) >> 1u) << 1u;
    uint g = ((((color >> 8u) & 0x3Fu) >> 1u) << 1u);
    uint b = ((((color >> 16u) & 0x3Fu) >> 1u) << 1u);
    uint a = (color >> 24u) != 0u ? 31u : 0u;
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ScreenWidth || id.y >= ScaleFactor || TexHeight >= 192u)
        return;
    uint nativeY = TexHeight;
    uint scaledY = nativeY * ScaleFactor + id.y;
    uint nativeX = id.x / ScaleFactor;
    uint nativeIndex = nativeX + nativeY * 256u;
    uint commandBase = CapCommandBase + nativeY * 4u;
    uint captureCnt = ResultValue[commandBase];
    uint command = ResultValue[commandBase + 1u];
    uint width = ResultValue[commandBase + 3u];
    if ((command & 0x80000000u) == 0u || nativeX >= width)
        return;
    uint sourceScreen = (command >> 3u) & 1u;
    bool source3DValid = (command & 0x10u) != 0u;
    uint lineMeta = ResultValue[CapLineMetaBase + sourceScreen * 192u + nativeY];
    uint2 position = uint2(id.x, scaledY);
    uint sourceA = 0u;
    if ((captureCnt & (1u << 24u)) != 0u)
        sourceA = source3DValid ? CapLoadSource3D(position, lineMeta) : 0u;
    else
        sourceA = CapComposeSourceA(position, nativeIndex, lineMeta, source3DValid);
    uint refB = ResultValue[CapSourceBReferenceBase + nativeIndex];
    uint sourceB = (refB & 0x80000000u) != 0u
        ? CapLoad(refB, uint2(id.x % ScaleFactor, id.y))
        : ResultValue[CapSourceBNativeBase + nativeIndex];
    uint mode = (captureCnt >> 29u) & 3u;
    uint result = sourceA;
    if (mode == 1u)
        result = sourceB;
    else if (mode >= 2u)
    {
        uint eva = min(captureCnt & 0x1Fu, 16u);
        uint evb = min((captureCnt >> 8u) & 0x1Fu, 16u);
        uint aa = (sourceA >> 24u) != 0u ? 1u : 0u;
        uint ab = (sourceB >> 24u) != 0u ? 1u : 0u;
        uint r = min(((((CapR(sourceA)>>1u)*aa*eva)+((CapR(sourceB)>>1u)*ab*evb)+8u)>>4u),31u)<<1u;
        uint g = min(((((CapG(sourceA)>>1u)*aa*eva)+((CapG(sourceB)>>1u)*ab*evb)+8u)>>4u),31u)<<1u;
        uint b = min(((((CapB(sourceA)>>1u)*aa*eva)+((CapB(sourceB)>>1u)*ab*evb)+8u)>>4u),31u)<<1u;
        uint alpha = ((eva != 0u ? aa : 0u) | (evb != 0u ? ab : 0u)) * 0xFFu;
        result = r | (g << 8u) | (b << 16u) | (alpha << 24u);
    }
    uint address = (ResultValue[commandBase + 2u] + nativeX) & 0xFFFFu;
    uint bank = command & 3u;
    uint version = (command >> 2u) & 1u;
    uint spp = ScaleFactor * ScaleFactor;
    uint cell = ((version * 4u + bank) * 65536u) + address;
    CaptureSidecarBuffer[cell * spp + id.y * ScaleFactor + (id.x % ScaleFactor)] =
        CapNormalizeCapturedPixel(result);
}
 )";

// ---------------------------------------------------------------------------
// High-resolution software-2D / DX12-3D compositor
// ---------------------------------------------------------------------------
// ResultValue is rebound to the packed structured-2D input and ResolveOut to
// the two-screen BGRA output for this dispatch. FinalFB remains the native
// high-resolution 3D source produced by FinalPass.
inline const std::string Compositor = R"(
static const uint StructuredPixelCount = 256u * 192u;
static const uint StructuredLineMetaBase = StructuredPixelCount * 14u;

uint Color6R(uint color) { return color & 0x3Fu; }
uint Color6G(uint color) { return (color >> 8u) & 0x3Fu; }
uint Color6B(uint color) { return (color >> 16u) & 0x3Fu; }

uint PackColor6(uint r, uint g, uint b, uint alpha)
{
    return min(r, 63u) | (min(g, 63u) << 8u) | (min(b, 63u) << 16u) | (alpha << 24u);
}

uint Blend4(uint first, uint second, uint eva, uint evb)
{
    return PackColor6(
        ((Color6R(first) * eva) + (Color6R(second) * evb) + 8u) >> 4u,
        ((Color6G(first) * eva) + (Color6G(second) * evb) + 8u) >> 4u,
        ((Color6B(first) * eva) + (Color6B(second) * evb) + 8u) >> 4u,
        0xFFu);
}

uint Blend5(uint first, uint second)
{
    uint eva = ((first >> 24u) & 0x1Fu) + 1u;
    uint evb = 32u - eva;
    return PackColor6(
        ((Color6R(first) * eva) + (Color6R(second) * evb) + 16u) >> 5u,
        ((Color6G(first) * eva) + (Color6G(second) * evb) + 16u) >> 5u,
        ((Color6B(first) * eva) + (Color6B(second) * evb) + 16u) >> 5u,
        0xFFu);
}

uint BrightnessUp(uint color, uint factor, uint bias)
{
    return PackColor6(
        Color6R(color) + ((((63u - Color6R(color)) * factor) + bias) >> 4u),
        Color6G(color) + ((((63u - Color6G(color)) * factor) + bias) >> 4u),
        Color6B(color) + ((((63u - Color6B(color)) * factor) + bias) >> 4u),
        0xFFu);
}

uint BrightnessDown(uint color, uint factor, uint bias)
{
    return PackColor6(
        Color6R(color) - (((Color6R(color) * factor) + bias) >> 4u),
        Color6G(color) - (((Color6G(color) * factor) + bias) >> 4u),
        Color6B(color) - (((Color6B(color) * factor) + bias) >> 4u),
        0xFFu);
}

uint ToBgra8(uint color)
{
    uint r6 = Color6R(color);
    uint g6 = Color6G(color);
    uint b6 = Color6B(color);
    uint r8 = (r6 << 2u) | (r6 >> 4u);
    uint g8 = (g6 << 2u) | (g6 >> 4u);
    uint b8 = (b6 << 2u) | (b6 >> 4u);
    return b8 | (g8 << 8u) | (r8 << 16u) | 0xFF000000u;
}

uint LoadStructuredCapture(uint reference, uint2 within)
{
    uint address = reference & 0xFFFFu;
    uint bank = (reference >> 28u) & 3u;
    uint version = (reference >> 30u) & 1u;
    uint samplesPerPixel = ScaleFactor * ScaleFactor;
    uint cell = ((version * 4u + bank) * 65536u) + address;
    return CaptureSidecarBuffer[
        cell * samplesPerPixel + within.y * ScaleFactor + within.x];
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ScreenWidth || id.y >= ScreenHeight * 2u)
        return;

    uint screen = id.y / ScreenHeight;
    uint scaledY = id.y - screen * ScreenHeight;
    uint nativeX = id.x / ScaleFactor;
    uint nativeY = scaledY / ScaleFactor;
    uint nativeIndex = nativeX + nativeY * 256u;
    uint planeBase = screen * StructuredPixelCount * 4u;

    uint below = ResultValue[planeBase + nativeIndex];
    uint above = ResultValue[planeBase + StructuredPixelCount + nativeIndex];
    uint control = ResultValue[planeBase + StructuredPixelCount * 2u + nativeIndex];
    uint captureReference = ResultValue[
        planeBase + StructuredPixelCount * 3u + nativeIndex];
    uint controlAlpha = control >> 24u;
    uint color = below;

    uint lineMeta = ResultValue[StructuredLineMetaBase + screen * 192u + nativeY];
    uint displayMode = (lineMeta >> 16u) & 0x3u;

    // VRAM display presents captured RGB directly. Its RGBA5551 alpha bit is
    // capture provenance rather than a visibility test, so this path must not
    // use the ordinary 3D-slot transparent-pixel fallback.
    if (displayMode == 2u && (captureReference & 0x80000000u) != 0u)
        color = LoadStructuredCapture(
            captureReference, uint2(id.x % ScaleFactor, scaledY % ScaleFactor));
    else if ((controlAlpha & 0x40u) != 0u)
    {
        // The 3D X scroll is published per scanline, because that is where the
        // DS applies it and where SoftRenderer3D::GetLine() reads it.
        uint xPosition = (lineMeta >> 23u) & 0x1FFu;
        int sourceX = (xPosition & 0x100u) != 0u
            ? int(id.x) - int((512u - xPosition) * ScaleFactor)
            : int(id.x) + int(xPosition * ScaleFactor);
        uint pixel3D = 0u;
        if ((captureReference & 0x80000000u) != 0u)
            pixel3D = LoadStructuredCapture(
                captureReference, uint2(id.x % ScaleFactor, scaledY % ScaleFactor));
        else if (TexWidth != 0u && sourceX >= 0 && sourceX < int(ScreenWidth))
            pixel3D = FinalFB[uint(sourceX) + scaledY * ScreenWidth];

        if (((pixel3D >> 24u) & 0x1Fu) != 0u)
        {
            uint compositionMode = controlAlpha & 0xFu;
            uint eva = (control >> 8u) & 0x1Fu;
            uint evb = (control >> 16u) & 0x1Fu;
            if (compositionMode == 1u && (controlAlpha & 0x80u) != 0u)
                color = Blend4(above, pixel3D, eva, evb);
            else if (compositionMode == 2u)
                color = BrightnessUp(pixel3D, eva, 8u);
            else if (compositionMode == 3u)
                color = BrightnessDown(pixel3D, eva, 7u);
            else if (compositionMode == 4u)
                color = Blend5(pixel3D, below);
            else
                color = pixel3D;
        }
    }

    if (displayMode != 0u)
    {
        uint brightnessMode = (lineMeta >> 8u) & 0x3u;
        uint brightnessFactor = min(lineMeta & 0x1Fu, 16u);
        if (brightnessMode == 1u)
            color = BrightnessUp(color, brightnessFactor, 0u);
        else if (brightnessMode == 2u)
            color = BrightnessDown(color, brightnessFactor, 15u);
    }

    uint bgra8 = ToBgra8(color);
    if (DispatchPad != 0u)
    {
        // ToBgra8 is the packed CPU/presenter word (B,G,R,A in memory). The
        // direct texture is RGBA8, so preserve the existing channel order by
        // expanding the packed bytes into normalized texture channels.
        DirectOutput[uint3(id.x, scaledY, screen)] = float4(
            float((bgra8 >> 16u) & 0xFFu) / 255.0,
            float((bgra8 >> 8u) & 0xFFu) / 255.0,
            float(bgra8 & 0xFFu) / 255.0,
            float((bgra8 >> 24u) & 0xFFu) / 255.0);
    }
    else
    {
        ResolveOut[screen * FramebufferStride + scaledY * ScreenWidth + id.x] = bgra8;
    }
}
)";

} // namespace melonDS::DX12Shaders

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_DX12_SHADERS_H
