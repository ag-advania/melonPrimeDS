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

/*
    1:1 re-expression of ComputeRendererShaders::YSpanSetupBuffer
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only: GL SSBO binding 2 -> set 0 binding 3.

    std430 layout is byte-identical to ComputeRenderer3D::SpanSetupY. `Linear`
    and `IsDummy` stay declared as `bool`, matching the GL original; glslang
    lowers a bool block member to a 32-bit integer with "non-zero is true",
    which is exactly what the C++ side writes (s32 Linear / u32 IsDummy).
*/

#ifndef MELONPRIME_VULKAN_YSPANSETUPBUFFER_GLSL
#define MELONPRIME_VULKAN_YSPANSETUPBUFFER_GLSL

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
    bool Linear;
    int IRecip;
    int W0n, W0d, W1d;

    // Slope
    int Increment;

    int X0, X1, Y0, Y1;
    int XMin, XMax;
    int DxInitial;

    int XCovIncr;

    bool IsDummy;
};

#if defined(InterpSpans)
int CalcYFactorY(YSpanSetup span, int i)
{
    /*
        maybe it would be better to do use a 32x32=64 multiplication?
    */
    uint numLo = uint(abs(i)) * uint(span.W0n);
    uint numHi = 0U;
    numHi |= numLo >> (32U-YFactorShift);
    numLo <<= YFactorShift;

    uint den = uint(abs(i)) * uint(span.W0d) + uint(abs(span.I1 - span.I0 - i)) * span.W1d;

    if (den == 0)
    {
        return 0;
    }
    else
    {
        return int(Div64_32_32(numHi, numLo, den));
    }
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

void EdgeParams_XMajor(bool side, bool swapped, int dx, YSpanSetup span, out int edgelen, out int edgecov)
{
    bool negative = span.X1 < span.X0;
    int len = 1;
    if (!swapped || side)
    {
        if (side != negative)
            len = (dx >> 18) - ((dx-span.Increment) >> 18);
        else
            len = ((dx+span.Increment) >> 18) - (dx >> 18);
    }
    edgelen = len;

    int xlen = span.XMax + 1 - span.XMin;
    int startx = dx >> 18;
    if (negative) startx = xlen - startx;
    if (side) startx = startx - len + 1;

    uint r;
    int startcov = int(Div(uint(((startx << 10) + 0x1FF) * (span.Y1 - span.Y0)), uint(xlen), r));
    edgecov = (1<<31) | ((startcov & 0x3FF) << 12) | (span.XCovIncr & 0x3FF);
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
    if (span.Increment > 0x40000)
        EdgeParams_XMajor(side, swapped, dx, span, edgelen, edgecov);
    else
        EdgeParams_YMajor(side, swapped, dx, span, edgelen, edgecov);
}
#endif

layout (std430, set = 0, binding = 3) buffer YSpanSetupsBuffer
{
    YSpanSetup YSpanSetups[];
};

#endif // MELONPRIME_VULKAN_YSPANSETUPBUFFER_GLSL
