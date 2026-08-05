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
// (GPU3D_Compute_shaders.h). Sources are compiled at runtime with
// d3dcompiler_47.dll, so the MinGW build needs no shader toolchain.
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

// Prepended to every shader. The renderer appends its own `#define`s for
// ScreenWidth/ScreenHeight/TileSize/... before this block.
inline const std::string Common = R"(

static const int CoarseTileCountX = 8;
static const int CoarseTileW = (CoarseTileCountX * TileSize);
static const int CoarseTileH = (CoarseTileCountY * TileSize);

static const uint FramebufferStride = ScreenWidth * ScreenHeight;
static const int TilesPerLine = ScreenWidth / TileSize;
static const int TileLines = ScreenHeight / TileSize;

static const int BinStride = 2048 / 32;
static const int CoarseBinStride = BinStride / 32;

static const int MaxVariants = 256;

// Must stay <= 65535: matches the OpenGL renderer's split dispatch for the
// driver group-count limit (melonDS issue #2047). Kept identical so the
// per-variant indirect arguments the CPU side writes mean the same thing.
static const uint RasteriseChunkSize = 32768u;

static const uint ResultColorStart = 0u;
static const uint ResultDepthStart = ResultColorStart + FramebufferStride * 2u;
static const uint ResultAttrStart  = ResultDepthStart + FramebufferStride * 2u;

// Byte offsets into the bin-result buffer. It mixes a fixed header with a
// trailing flexible array, so it is bound as a RWByteAddressBuffer rather than
// a structured buffer. Layout matches BinResultHeader in GPU3D_DX12.h.
static const uint BinVariantWorkCountBase     = 0u;                  // uint4[256]
static const uint BinSortedWorkOffsetBase     = 4096u;               // uint[256]
static const uint BinVariantWorkRealCountBase = 5120u;               // uint[256]
static const uint BinSortWorkWorkCountBase    = 6144u;               // uint4
static const uint BinMaskAndOffsetBase        = 6160u;               // uint[]

static const int BinningCoarseMaskStart = 0;
static const int BinningMaskStart = BinningCoarseMaskStart + TilesPerLine * TileLines * CoarseBinStride;
static const int BinningWorkOffsetsStart = BinningMaskStart + TilesPerLine * TileLines * BinStride;

static const uint WorkDescsUnsortedStart = 0u;
static const uint WorkDescsSortedStart = WorkDescsUnsortedStart + MaxWorkTiles;

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
    uint DispatchPad0;
    uint DispatchPad1;
    uint DispatchPad2;
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
    if (y0 == y1)
        return y0;

    if (y0 < y1)
        return y0 + (((y1 - y0) * ifactor) >> YFactorShift);
    else
        return y1 + (((y0 - y1) * ((1 << YFactorShift) - ifactor)) >> YFactorShift);
}

int InterpolateAttrLinear(int y0, int y1, int i, int irecip, int idiff)
{
    if (y0 == y1)
        return y0;

#ifndef Rasterise
    irecip = abs(irecip);
#endif

    uint mulLo, mulHi;
    if (y0 < y1)
    {
#ifndef Rasterise
        uint offset = uint(abs(i));
#else
        uint offset = uint(i);
#endif
        UMul64(uint(y1 - y0) * offset, uint(irecip), mulHi, mulLo);
        uint sum = mulLo + (3u << 24);
        if (sum < mulLo) mulHi += 1u;
        mulLo = sum;
        return y0 + int((mulLo >> 30) | (mulHi << (32 - 30)));
    }
    else
    {
#ifndef Rasterise
        uint offset = uint(abs(idiff - i));
#else
        uint offset = uint(idiff - i);
#endif
        UMul64(uint(y0 - y1) * offset, uint(irecip), mulHi, mulLo);
        uint sum = mulLo + (3u << 24);
        if (sum < mulLo) mulHi += 1u;
        mulLo = sum;
        return y1 + int((mulLo >> 30) | (mulHi << (32 - 30)));
    }
}

uint InterpolateZZBuffer(int z0, int z1, int i, int irecip, int idiff)
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

    return base + (((mulLo >> shiftr) | (mulHi << (32 - shiftr))) << shiftl);
}

uint InterpolateZWBuffer(int z0, int z1, int ifactor)
{
    if (z0 == z1)
        return uint(z0);

#ifdef Rasterise
    // along x spans the precision is only 8 bit, so the result always fits
    if (z0 < z1)
        return uint(z0) + uint(((z1 - z0) * ifactor) >> YFactorShift);
    else
        return uint(z1) + uint(((z0 - z1) * ((1 << YFactorShift) - ifactor)) >> YFactorShift);
#else
    uint mulLo, mulHi;
    if (z0 < z1)
    {
        UMul64(uint(z1 - z0), uint(ifactor), mulHi, mulLo);
        return uint(z0) + ((mulLo >> YFactorShift) | (mulHi << (32u - YFactorShift)));
    }
    else
    {
        UMul64(uint(z0 - z1), uint((1 << YFactorShift) - ifactor), mulHi, mulLo);
        return uint(z1) + ((mulLo >> YFactorShift) | (mulHi << (32u - YFactorShift)));
    }
#endif
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

    if (den == 0u)
        return 0;
    return int(Div64_32_32(numHi, numLo, den));
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

void EdgeParams_XMajor(bool side, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    bool negative = span.X1 < span.X0;
    int len;
    if (side != negative)
        len = (dx >> 18) - ((dx - span.Increment) >> 18);
    else
        len = ((dx + span.Increment) >> 18) - (dx >> 18);
    edgelen = len;

    int xlen = span.XMax + 1 - span.XMin;
    int startx = dx >> 18;
    if (negative) startx = xlen - startx;
    if (side) startx = startx - len + 1;

    uint r;
    int startcov = int(Div(uint(((startx << 10) + 0x1FF) * (span.Y1 - span.Y0)), uint(xlen), r));
    edgecov = int(1u << 31) | ((startcov & 0x3FF) << 12) | (span.XCovIncr & 0x3FF);
}

void EdgeParams_YMajor(bool side, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    bool negative = span.X1 < span.X0;
    edgelen = 1;

    if (span.Increment == 0)
    {
        edgecov = 31;
    }
    else
    {
        int cov = ((dx >> 9) + (span.Increment >> 10)) >> 4;
        if ((cov >> 5) != (dx >> 18)) cov = 31;
        cov &= 0x1F;
        if (side == negative) cov = 0x1F - cov;

        edgecov = cov;
    }
}

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint4 setup = SetupIndices.Load(int(id.x));

    YSpanSetup spanL = YSpanSetups[setup.y];
    YSpanSetup spanR = YSpanSetups[setup.z];

    XSpanSetup xspan;
    xspan.Flags = 0u;

    int y = int(setup.w);

    int dxl = CalculateDx(y, spanL);
    int dxr = CalculateDx(y, spanR);

    int xl = CalculateX(dxl, spanL);
    int xr = CalculateX(dxr, spanR);

    Polygon polygon = Polygons[setup.x];

    int edgeLenL, edgeLenR;

    if (xl > xr)
    {
        YSpanSetup tmpSpan = spanL;
        spanL = spanR;
        spanR = tmpSpan;

        int tmp = xl;
        xl = xr;
        xr = tmp;

        EdgeParams_YMajor(false, dxr, spanL, edgeLenL, xspan.EdgeCovL);
        EdgeParams_YMajor(true, dxl, spanR, edgeLenR, xspan.EdgeCovR);
    }
    else
    {
        if (spanL.Increment > 0x40000)
            EdgeParams_XMajor(false, dxl, spanL, edgeLenL, xspan.EdgeCovL);
        else
            EdgeParams_YMajor(false, dxl, spanL, edgeLenL, xspan.EdgeCovL);
        if (spanR.Increment > 0x40000)
            EdgeParams_XMajor(true, dxr, spanR, edgeLenR, xspan.EdgeCovR);
        else
            EdgeParams_YMajor(true, dxr, spanR, edgeLenR, xspan.EdgeCovR);
    }

    xspan.CovLInitial = (xspan.EdgeCovL >> 12) & 0x3FF;
    if (xspan.CovLInitial == 0x3FF)
        xspan.CovLInitial = 0;
    xspan.CovRInitial = (xspan.EdgeCovR >> 12) & 0x3FF;
    if (xspan.CovRInitial == 0x3FF)
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

    bool fillAllEdges = polyalpha < 31u || (DispCnt & (3u << 4)) != 0u;

    if (fillAllEdges || spanL.X1 < spanL.X0 || spanL.Increment <= 0x40000)
        xspan.Flags |= XSpanSetup_FillLeft;
    if (fillAllEdges || (spanR.X1 >= spanR.X0 && spanR.Increment > 0x40000) || spanR.Increment == 0)
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
        int i = (spanL.Increment > 0x40000 ? xl : y) - spanL.I0;
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
        int i = (spanR.Increment > 0x40000 ? xr : y) - spanR.I0;
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

    XSpanSetups[id.x] = xspan;
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
    if (polygon.YTop > botRight.y || polygon.YBot <= topLeft.y)
        return false;

    int polygonHeight = polygon.YBot - polygon.YTop;

    // Convex polygons: within a tile that does not contain the direction
    // change, sampling the top-most and bottom-most span is enough to bound
    // the polygon horizontally.
    int polyInnerTopY = clamp(topLeft.y - polygon.YTop, 0, max(polygonHeight - 1, 0));
    int polyInnerBotY = clamp(botRight.y - polygon.YTop, 0, max(polygonHeight - 1, 0));

    XSpanSetup xspanTop = XSpanSetups[polygon.FirstXSpan + polyInnerTopY];
    XSpanSetup xspanBot = XSpanSetups[polygon.FirstXSpan + polyInnerBotY];

    int minXL;
    if (polygon.XMinY >= topLeft.y && polygon.XMinY <= botRight.y)
        minXL = polygon.XMin;
    else
        minXL = min(xspanTop.X0, xspanBot.X0);

    if (minXL > botRight.x)
        return false;

    int maxXR;
    if (polygon.XMaxY >= topLeft.y && polygon.XMaxY <= botRight.y)
        maxXR = polygon.XMax;
    else
        maxXR = max(xspanTop.X1, xspanBot.X1) - 1;

    if (maxXR < topLeft.x)
        return false;

    return true;
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

    int polygonIdx = groupIdx * 32 + localIdx;

    int2 coarseTopLeft = coarseTile * int2(CoarseTileW, CoarseTileH);
    int2 coarseBotRight = coarseTopLeft + int2(CoarseTileW - 1, CoarseTileH - 1);

    bool binned = false;
    if (uint(polygonIdx) < NumPolygons)
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

        int binPolygonIdx = groupIdx * 32 + bit;

        if (BinPolygon(Polygons[binPolygonIdx], fineTileTopLeft, fineTileBotRight))
            binnedMask |= 1u << uint(bit);
    }

    int linearTile = fineTile.x + fineTile.y * TilesPerLine
        + coarseTile.x * CoarseTileCountX + coarseTile.y * TilesPerLine * CoarseTileCountY;

    // Clamp the work-tile allocation to the MaxWorkTiles budget. The tile
    // buffers and WorkDescs are sized for MaxWorkTiles entries, but the
    // tiles*16 heuristic can be exceeded when many screen-filling translucent
    // polygons stack up. Trimming before the mask/offset stores keeps every
    // downstream consumer seeing a consistent set; excess polygons in a tile
    // just drop a layer instead of corrupting the frame.
    uint workOffset = 0u;
    if (binnedMask != 0u)
    {
        BinResult.InterlockedAdd(BinVariantWorkCountBase + 12u, uint(countbits(binnedMask)), workOffset);
        if (workOffset >= uint(MaxWorkTiles))
        {
            binnedMask = 0u;
        }
        else
        {
            uint keepCount = uint(MaxWorkTiles) - workOffset;
            while (uint(countbits(binnedMask)) > keepCount)
                binnedMask &= ~(1u << FindMSB(binnedMask));
        }
    }

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

            int workPolygonIdx = groupIdx * 32 + bit;
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
            // [0].w can overshoot MaxWorkTiles when the binning shader clamped
            // the allocation; only the first MaxWorkTiles entries were written.
            uint total = min(BinResult.Load(BinVariantWorkCountBase + 12u), uint(MaxWorkTiles));
            BinResult.Store4(BinSortWorkWorkCountBase, uint4((total + 31u) / 32u, 1u, 1u, 0u));
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
    }
}
)";

inline const std::string SortWork = R"(
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Entries past MaxWorkTiles were dropped by the binning shader and must
    // not be read.
    uint total = min(BinResult.Load(BinVariantWorkCountBase + 12u), uint(MaxWorkTiles));
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

    if (span.X0 != span.X1)
    {
        uint numLo = uint(x) * uint(span.W0);
        uint numHi = 0u;
        numHi |= numLo >> (32u - YFactorShift);
        numLo <<= YFactorShift;

        uint den = uint(x) * uint(span.W0) + uint(span.X1 - span.X0 - x) * uint(span.W1);

        if (den == 0u)
            return 0;
        return int(Div64_32_32(numHi, numLo, den));
    }

    return 0;
}

#ifdef UseTexture
// HLSL cannot Sample() a UINT texture, so GL_REPEAT / GL_MIRRORED_REPEAT /
// GL_CLAMP_TO_EDGE are reproduced here with integer math. Every texture the
// cache hands out is power-of-two sized, which is what makes the masking form
// exact.
int WrapTexCoord(int c, int size, uint mode)
{
    if (mode == 1u)
        return c & (size - 1);
    if (mode == 2u)
    {
        int m = c & ((size << 1) - 1);
        return (m >= size) ? ((size << 1) - 1 - m) : m;
    }
    return clamp(c, 0, size - 1);
}

uint4 SampleTexture(int u, int v, uint layer)
{
    // The GL renderer normalizes by the texture size and relies on NEAREST
    // filtering; floor(u/16) is the same texel and avoids the float round trip.
    int iu = WrapTexCoord(u >> 4, int(TexWidth), TexWrapS);
    int iv = WrapTexCoord(v >> 4, int(TexHeight), TexWrapT);
    return CurrentTexture.Load(int4(iu, iv, int(layer), 0));
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
                    int xcov = xspan.CovLInitial + (xspan.EdgeCovL & 0x3FF) * (position.x - xspan.X0);
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
                    int xcov = xspan.CovRInitial + (xspan.EdgeCovR & 0x3FF) * (position.x - xspan.InsideEnd);
                    cov = max(0x1F - (xcov >> 5), 0);
                }

                attr |= uint(cov) << 8;
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

bool DepthTestPasses(bool equalDepthTest, uint dstDepth, uint tileDepth)
{
    return equalDepthTest ? DepthTestEqual(dstDepth, tileDepth) : (tileDepth < dstDepth);
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
        if (dstA != 0u)
        {
            srcRB = ((srcRB * alpha) + (dstRB * (32u - alpha))) >> 5;
            srcG = ((srcG * alpha) + (dstG * (32u - alpha))) >> 5;
        }

        color = (srcRB & 0x3F003Fu) | (srcG & 0x003F00u) | max(dstA, srcA);
    }
}

void ProcessCoarseMask(int linearTile, uint coarseMask, uint coarseOffset, uint2 localId,
    inout uint2 color, inout uint2 depth, inout uint2 attr, inout uint stencil,
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

            uint polygonIdx = fineIdx + (coarseBit + coarseOffset) * 32u;

            if (tileColor != 0u)
            {
                uint polygonAttr = Polygons[polygonIdx].Attr;

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
                    if (!DepthTestPasses(equalDepthTest, dstDepth, tileDepth))
                    {
                        if ((dstattr & 0x3u) == 0u || writeSecondLayer)
                            continue;

                        writeSecondLayer = true;
                        dstattr = attr.y;
                        if (!DepthTestPasses(equalDepthTest, depth.y, tileDepth))
                            continue;
                    }

                    uint srcAttr = (polygonAttr & 0x3F008000u);

                    uint srcA = tileColor & 0x1F000000u;
                    if (srcA == 0x1F000000u)
                    {
                        srcAttr |= tileAttr;

                        if (!writeSecondLayer)
                        {
                            if ((srcAttr & 0x3u) != 0u)
                            {
                                color.y = color.x;
                                depth.y = depth.x;
                                attr.y = attr.x;
                            }

                            color.x = tileColor;
                            depth.x = tileDepth;
                            attr.x = srcAttr;
                        }
                        else
                        {
                            color.y = tileColor;
                            depth.y = tileDepth;
                            attr.y = srcAttr;
                        }
                    }
                    else
                    {
                        bool writeDepth = (polygonAttr & (1u << 11)) != 0u;

                        if (!writeSecondLayer)
                            PlotTranslucent(color.x, depth.x, attr.x, isShadow, tileColor, srcA, tileDepth, srcAttr, writeDepth);
                        if (writeSecondLayer || (dstattr & 0x3u) != 0u)
                            PlotTranslucent(color.y, depth.y, attr.y, isShadow, tileColor, srcA, tileDepth, srcAttr, writeDepth);
                    }
                }
                else
                {
                    if (!prevIsShadowMaskOld)
                        stencil = 0u;

                    if (!DepthTestPasses(equalDepthTest, depth.x, tileDepth))
                        stencil = 0x1u;

                    if ((dstattr & 0x3u) != 0u)
                    {
                        if (!DepthTestPasses(equalDepthTest, depth.y, tileDepth))
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

    uint2 color, depth;
    uint2 attr = uint2(ClearAttr, 0u);
    if ((DispCnt & (1u << 14)) != 0u)
    {
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
    }

    uint stencil = 0u;
    bool prevIsShadowMask = false;

    ProcessCoarseMask(linearTile, coarseMaskLo, 0u, localId.xy, color, depth, attr, stencil, prevIsShadowMask);
    ProcessCoarseMask(linearTile, coarseMaskHi, uint(BinStride / 2), localId.xy, color, depth, attr, stencil, prevIsShadowMask);

    uint resultOffset = id.x + id.y * ScreenWidth;
    ResultValue[ResultColorStart + resultOffset] = color.x;
    ResultValue[ResultColorStart + resultOffset + FramebufferStride] = color.y;
    ResultValue[ResultDepthStart + resultOffset] = depth.x;
    ResultValue[ResultDepthStart + resultOffset + FramebufferStride] = depth.y;
    ResultValue[ResultAttrStart + resultOffset] = attr.x;
    ResultValue[ResultAttrStart + resultOffset + FramebufferStride] = attr.y;
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

#if ScaleFactor == 1
    ResolveOut[outOffset] = FinalFB[outOffset];
#else
    uint baseX = id.x * ScaleFactor;
    uint baseY = id.y * ScaleFactor;

    uint sumR = 0u, sumG = 0u, sumB = 0u, sumA = 0u;

    [loop] for (uint sy = 0u; sy < ScaleFactor; sy++)
    {
        [loop] for (uint sx = 0u; sx < ScaleFactor; sx++)
        {
            uint texel = FinalFB[(baseX + sx) + (baseY + sy) * ScreenWidth];
            uint a = (texel >> 24) & 0x1Fu;

            // Weight color by alpha so pixels the 3D layer does not cover
            // cannot darken the edge of geometry that does.
            sumR += (texel & 0x3Fu) * a;
            sumG += ((texel >> 8) & 0x3Fu) * a;
            sumB += ((texel >> 16) & 0x3Fu) * a;
            sumA += a;
        }
    }

    uint result = 0u;
    if (sumA != 0u)
    {
        // Round to nearest rather than truncating: at high scale factors the
        // truncation bias is a visible darkening of every supersampled edge.
        uint half = sumA >> 1;
        uint samples = ScaleFactor * ScaleFactor;
        uint r = min((sumR + half) / sumA, 63u);
        uint g = min((sumG + half) / sumA, 63u);
        uint b = min((sumB + half) / sumA, 63u);
        uint a = min((sumA + (samples >> 1)) / samples, 31u);
        result = r | (g << 8) | (b << 16) | (a << 24);
    }

    ResolveOut[outOffset] = result;
#endif
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
static const uint StructuredCaptureMaskBase = StructuredPixelCount * 6u;
static const uint StructuredNativeScreenBase = StructuredPixelCount * 8u;
static const uint StructuredLineMetaBase = StructuredPixelCount * 10u;

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
    uint planeBase = screen * StructuredPixelCount * 3u;

    uint below = ResultValue[planeBase + nativeIndex];
    uint above = ResultValue[planeBase + StructuredPixelCount + nativeIndex];
    uint control = ResultValue[planeBase + StructuredPixelCount * 2u + nativeIndex];
    uint controlAlpha = control >> 24u;
    uint color = below;
    uint sourceIndex = screen * StructuredPixelCount + nativeIndex;

    if ((controlAlpha & 0x40u) != 0u)
    {
        // OpenGL keeps display-capture textures separate from the current 3D
        // render target. The software renderer is the authoritative retained
        // capture store for DX12, so a capture-backed 3D slot must use its
        // already composed physical-LCD pixel instead of the current FinalFB.
        if (ResultValue[StructuredCaptureMaskBase + sourceIndex] != 0u)
        {
            ResolveOut[screen * FramebufferStride + scaledY * ScreenWidth + id.x] =
                ResultValue[StructuredNativeScreenBase + sourceIndex];
            return;
        }

        uint xPosition = CurVariant & 0x1FFu;
        int sourceX = (xPosition & 0x100u) != 0u
            ? int(id.x) - int((512u - xPosition) * ScaleFactor)
            : int(id.x) + int(xPosition * ScaleFactor);
        uint pixel3D = 0u;
        if (TexWidth != 0u && sourceX >= 0 && sourceX < ScreenWidth)
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

    uint lineMeta = ResultValue[StructuredLineMetaBase + screen * 192u + nativeY];
    uint displayMode = (lineMeta >> 16u) & 0x3u;
    if (displayMode != 0u)
    {
        uint brightnessMode = (lineMeta >> 8u) & 0x3u;
        uint brightnessFactor = min(lineMeta & 0x1Fu, 16u);
        if (brightnessMode == 1u)
            color = BrightnessUp(color, brightnessFactor, 0u);
        else if (brightnessMode == 2u)
            color = BrightnessDown(color, brightnessFactor, 15u);
    }

    ResolveOut[screen * FramebufferStride + scaledY * ScreenWidth + id.x] = ToBgra8(color);
}
)";

} // namespace melonDS::DX12Shaders

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_DX12_SHADERS_H
