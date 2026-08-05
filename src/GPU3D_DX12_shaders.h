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
// The internal pixel encoding is deliberately identical to the OpenGL compute
// renderer's: color words are r6 | g6<<8 | b6<<16 | a5<<24, which is also the
// exact layout the software 2D compositor expects from Renderer3D::GetLine().
namespace melonDS::DX12Shaders
{

// Prepended to every shader. The renderer appends its own `#define`s for
// ScreenWidth/ScreenHeight/TileSize etc. before this block.
inline const std::string Common = R"(
cbuffer MetaUniform : register(b1)
{
    uint NumPolygons;
    uint NumVariants;
    uint AlphaRef;
    uint DispCnt;

    // .x toon color, .y fog density, .z edge color -- same packing as the
    // OpenGL compute renderer's ToonTable.
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
    uint TexIsCapture;
    float2 TextureSize;
    float CaptureYOffset;
    uint DispatchPad0;
    uint DispatchPad1;
    uint DispatchPad2;
};

static const uint ResultColorStart = 0u;
static const uint ResultDepthStart = ResultColorStart + ScreenWidth * ScreenHeight * 2u;
static const uint ResultAttrStart  = ResultDepthStart + ScreenWidth * ScreenHeight * 2u;
static const uint FramebufferStride = ScreenWidth * ScreenHeight;
)";

// Writes the clear plane (flat color or the VRAM clear bitmap) into the
// color/depth/attr result buffer. This is the ProcessCoarseMask-free subset of
// the OpenGL DepthBlend shader; the binned polygon merge is layered on top of
// it by the rasterizer stage.
inline const std::string ClearPlane = R"(
RWStructuredBuffer<uint> ResultValue : register(u0);

Texture2D<uint> ClearBitmapColor : register(t0);
Texture2D<uint> ClearBitmapDepth : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ScreenWidth || id.y >= ScreenHeight)
        return;

    uint2 color;
    uint2 depth;
    uint2 attr = uint2(ClearAttr, 0u);

    if ((DispCnt & (1u << 14)) != 0u)
    {
        // The OpenGL renderer divides both axes by ScreenWidth so the 256x256
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

    uint resultOffset = id.x + id.y * ScreenWidth;
    ResultValue[ResultColorStart + resultOffset] = color.x;
    ResultValue[ResultColorStart + resultOffset + FramebufferStride] = color.y;
    ResultValue[ResultDepthStart + resultOffset] = depth.x;
    ResultValue[ResultDepthStart + resultOffset + FramebufferStride] = depth.y;
    ResultValue[ResultAttrStart + resultOffset] = attr.x;
    ResultValue[ResultAttrStart + resultOffset + FramebufferStride] = attr.y;
}
)";

// Port of the OpenGL FinalPass: edge marking, fog and anti-aliasing resolve.
// Compiled in eight variants selected by the EdgeMarking/Fog/AntiAliasing
// defines, exactly like the GL renderer's ShaderFinalPass[8].
inline const std::string FinalPass = R"(
RWStructuredBuffer<uint> ResultValue : register(u0);
RWStructuredBuffer<uint> FinalFB : register(u1);

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

// Downscales the internal-resolution 3D output to the DS's native 256x192 and
// writes it in the format Renderer3D::GetLine() must return. At 1x this is a
// straight copy; above that it is an alpha-weighted box filter, which is what
// makes internal resolution behave as supersampling for the software 2D path.
inline const std::string Resolve = R"(
StructuredBuffer<uint> FinalFB : register(t0);
RWStructuredBuffer<uint> ResolveOut : register(u0);

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
        uint samples = ScaleFactor * ScaleFactor;
        uint r = min(sumR / sumA, 63u);
        uint g = min(sumG / sumA, 63u);
        uint b = min(sumB / sumA, 63u);
        uint a = min((sumA + (samples >> 1)) / samples, 31u);
        result = r | (g << 8) | (b << 16) | (a << 24);
    }

    ResolveOut[outOffset] = result;
#endif
}
)";

} // namespace melonDS::DX12Shaders

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_DX12_SHADERS_H
