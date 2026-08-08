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
    1:1 re-expression of ComputeRendererShaders::XSpanSetupBuffer
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only: GL SSBO binding 1 -> set 0 binding 2.

    std430 layout is byte-identical to ComputeRenderer3D::SpanSetupX: 24
    4-byte scalars, base alignment 4, array stride 96. Note that the GLSL
    view names slots 2 and 3 InsideStart/InsideEnd while the C++ view names
    them EdgeLenL/EdgeLenR -- the interp stage overwrites them, exactly as in
    the GL renderer.
*/

#ifndef MELONPRIME_VULKAN_XSPANSETUPBUFFER_GLSL
#define MELONPRIME_VULKAN_XSPANSETUPBUFFER_GLSL

const uint XSpanSetup_Linear = 1U << 0;
const uint XSpanSetup_FillInside = 1U << 1;
const uint XSpanSetup_FillLeft = 1U << 2;
const uint XSpanSetup_FillRight = 1U << 3;

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

#if defined(Rasterise)
int CalcYFactorX(XSpanSetup span, int x)
{
    x -= span.X0;

    if (span.X0 != span.X1)
    {
        uint numLo = uint(x) * uint(span.W0);
        uint numHi = 0U;
        numHi |= numLo >> (32U-YFactorShift);
        numLo <<= YFactorShift;

        uint den = uint(x) * uint(span.W0) + uint(span.X1 - span.X0 - x) * uint(span.W1);

        if (den == 0)
            return 0;
        else
            return int(Div64_32_32(numHi, numLo, den));
    }
    else
    {
        return 0;
    }
}
#endif

layout (std430, set = 0, binding = 2) buffer XSpanSetupsBuffer
{
    XSpanSetup XSpanSetups[];
};

#endif // MELONPRIME_VULKAN_XSPANSETUPBUFFER_GLSL
