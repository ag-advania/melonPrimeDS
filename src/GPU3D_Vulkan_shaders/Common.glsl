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
    MelonPrimeDS Vulkan compute rasterizer -- shared prologue.

    This is a 1:1 re-expression of the `Common` block of
    src/GPU3D_Compute_shaders.h as standalone Vulkan GLSL. Every integer width,
    shift, rounding step and the 64/32 division emulation are transcribed
    unchanged; the DS rasterizer's output depends on all of them bit-exactly.

    Only the resource plumbing differs from the GL original:

      * GL's three independent binding namespaces (UBO / SSBO / texture-image)
        collapse into one Vulkan descriptor-set binding space, so every buffer
        got a new binding number. See the table below.
      * GL default-block uniforms (`layout(location=N) uniform ...`) become a
        single push-constant block.
      * `uimageBuffer` + `imageLoad` becomes `utextureBuffer` + `texelFetch`,
        i.e. a Vulkan uniform texel buffer, which is what the GL texture-buffer
        object actually was.

    Descriptor binding contract (must match the C++ pipeline layout exactly):

      set 0
        0   uniform buffer            MetaUniform        (std140)
        1   storage buffer readonly   PolygonBuffer      (std430)
        2   storage buffer            XSpanSetupsBuffer  (std430)
        3   storage buffer            YSpanSetupsBuffer  (std430)
        4   storage buffer            ColorTileBuffer    (std430)
        5   storage buffer            DepthTileBuffer    (std430)
        6   storage buffer            AttrTileBuffer     (std430)
        7   storage buffer            ResultBuffer       (std430)
        8   storage buffer            BinResultBuffer    (std430)
        9   storage buffer            WorkDescBuffer     (std430)
        10  uniform texel buffer      SetupIndices       (rgba16ui)
        11  storage image             FinalFB            (rgba8)
        12  storage buffer readonly   StructuredInput    (std430)
        13  storage buffer            PresentationOutput (std430)
        14  storage buffer            CaptureSidecar     (std430)
        15  storage buffer            BlendState         (std430)
      set 1
        0   usampler2DArray           CurrentTexture
        1   sampler2DArray            Capture128Texture
        2   sampler2DArray            Capture256Texture
        3   usampler2D                ClearBitmapColor
        4   usampler2D                ClearBitmapDepth

    ------------------------------------------------------------------------
    Scale-dependent constants
    ------------------------------------------------------------------------

    The GL renderer bakes every geometry constant into the shader text at
    compile time, and recompiles the whole set whenever the internal resolution
    changes. Vulkan compiles offline, so the constants are split into two
    classes:

    1. Constants that select a workgroup size or are otherwise required to be
       literal at SPIR-V generation time are injected as `-D` defines:
           TileSize, CoarseTileCountY, CoarseTileArea,
           ClearCoarseBinMaskLocalSize
       These derive *only* from `range = (scale>=5) + (scale>=9)` (see
       ComputeRenderer3D::SetRenderSettings), so scales 1..16 collapse into
       exactly three compiled buckets:
           range 0 (scale 1-4):  TileSize  8, CoarseTileCountY 4,
                                 CoarseTileArea 32, ClearCoarseBinMaskLocalSize 64
           range 1 (scale 5-8):  TileSize 16, CoarseTileCountY 4,
                                 CoarseTileArea 32, ClearCoarseBinMaskLocalSize 64
           range 2 (scale 9-16): TileSize 32, CoarseTileCountY 6,
                                 CoarseTileArea 48, ClearCoarseBinMaskLocalSize 48

    2. Constants that are only ever used as plain scalars in arithmetic become
       specialization constants, so one SPIR-V module per bucket covers every
       scale in that bucket:
           constant_id 0  ScreenWidth   = 256 * scale
           constant_id 1  ScreenHeight  = 192 * scale
           constant_id 2  MaxWorkTiles  = TilesPerLine * TileLines * 16

       Everything derived from them (FramebufferStride, TilesPerLine,
       TileLines, CoarseTileW/H, the BinningMaskAndOffset section offsets and
       the ResultValue section offsets) folds into OpSpecConstantOp and is
       resolved by the driver at pipeline-creation time, so there is no runtime
       cost relative to the GL renderer's baked literals.

    NOTE for the host side: `local_size_x/y = TileSize` means a 32x32 = 1024
    invocation workgroup in the range-2 bucket. Vulkan only guarantees
    maxComputeWorkGroupInvocations >= 128, so the host must check
    VkPhysicalDeviceLimits before offering scales >= 9.
*/

#ifndef MELONPRIME_VULKAN_COMMON_GLSL
#define MELONPRIME_VULKAN_COMMON_GLSL

#ifndef TileSize
#error "TileSize must be provided by the shader build (-DTileSize=...)"
#endif
#ifndef CoarseTileCountY
#error "CoarseTileCountY must be provided by the shader build"
#endif

// Scale-derived scalars. Defaults match internal resolution 1x so the modules
// stay meaningful (and validatable) without a VkSpecializationInfo.
layout (constant_id = 0) const int ScreenWidth = 256;
layout (constant_id = 1) const int ScreenHeight = 192;
layout (constant_id = 2) const int MaxWorkTiles = 12288;

const int CoarseTileCountX = 8;
const int CoarseTileW = (CoarseTileCountX * TileSize);
const int CoarseTileH = (CoarseTileCountY * TileSize);

const int FramebufferStride = ScreenWidth*ScreenHeight;
const int TilesPerLine = ScreenWidth/TileSize;
const int TileLines = ScreenHeight/TileSize;

const int BinStride = 2048/32;
const int CoarseBinStride = BinStride/32;

const int MaxVariants = 2048;

layout (std140, set = 0, binding = 0) uniform MetaUniform
{
    uint NumPolygons;
    uint NumVariants;

    int AlphaRef;

    uint DispCnt;

    // r = Toon
    // g = Fog Density
    // b = Edge Color
    uvec4 ToonTable[34];

    uint ClearColor, ClearDepth, ClearAttr;

    uint FogOffset, FogShift, FogColor;

    vec2 ClearBitmapOffset;
};

/*
    Replaces the GL default-block uniforms of the rasterise stage:
        location 0  uniform uint  CurVariant
        location 1  uniform vec2  InvTextureSize
        location 2  uniform int   TexIsCapture
        location 3  uniform float CaptureYOffset

    The two float uniforms are carried as the integer texture size and the
    integer capture Y offset instead; the rasterise shader performs the same
    two divisions the GL host performed. DS texture dimensions are powers of
    two, so 1/w and 1/h are exact and the result is bit-identical.

    TexWrapS / TexWrapT are part of the fixed 32-byte push-constant contract
    but are deliberately *not* consumed here: the GL renderer expressed the
    four DS wrap modes through sampler objects (GL_CLAMP_TO_EDGE / GL_REPEAT /
    GL_MIRRORED_REPEAT), which map 1:1 onto VkSamplerAddressMode. Doing the
    wrap in the shader instead would change the filtering path and is not a
    faithful port, so the host must keep selecting the sampler bound to
    set 1 binding 0.

    The block is declared in full (32 bytes) in every stage so that one
    VkPipelineLayout with a single VK_SHADER_STAGE_COMPUTE_BIT push-constant
    range serves all 33 pipelines.
*/
layout (push_constant) uniform PushConstants
{
    uint CurVariant;
    uint TexWidth;
    uint TexHeight;
    uint TexWrapS;
    uint TexWrapT;
    uint TexIsCapture;
    int CaptureYOffset;
    uint _pad;
} pc;

#ifdef InterpSpans
const int YFactorShift = 9;
#else
const int YFactorShift = 8;
#endif

#if defined(InterpSpans) || defined(Rasterise)
uint Umulh(uint a, uint b)
{
    uint lo, hi;
    umulExtended(a, b, hi, lo);
    return hi;
}

const uint startTable[256] = uint[256](
    254, 252, 250, 248, 246, 244, 242, 240, 238, 236, 234, 233, 231, 229, 227, 225, 224, 222, 220, 218, 217, 215, 213, 212, 210, 208, 207, 205, 203, 202, 200, 199, 197, 195, 194, 192, 191, 189, 188, 186, 185, 183, 182, 180, 179, 178, 176, 175, 173, 172, 170, 169, 168, 166, 165, 164, 162, 161, 160, 158,
157, 156, 154, 153, 152, 151, 149, 148, 147, 146, 144, 143, 142, 141, 139, 138, 137, 136, 135, 134, 132, 131, 130, 129, 128, 127, 126, 125, 123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 88, 87, 86, 85, 84, 83, 82, 81, 80, 80, 79, 78, 77, 76, 75, 74, 74, 73, 72, 71, 70, 70, 69, 68, 67, 66, 66, 65, 64, 63, 62, 62, 61, 60, 59, 59, 58, 57, 56, 56, 55, 54, 53, 53, 52, 51, 50, 50, 49, 48, 48, 47, 46, 46, 45, 44, 43, 43, 42, 41, 41, 40, 39, 39, 38, 37, 37, 36, 35, 35, 34, 33, 33, 32, 32, 31, 30, 30, 29, 28, 28, 27, 27, 26, 25, 25, 24, 24, 23, 22, 22, 21, 21, 20, 19, 19, 18, 18, 17, 17, 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0
);

uint Div(uint x, uint y, out uint r)
{
    // https://www.microsoft.com/en-us/research/publication/software-integer-division/
    uint k = 31 - findMSB(y);
    uint ty = (y << k) >> (32 - 9);
    uint t = startTable[ty - 256] + 256;
    uint z = (t << (32 - 9)) >> (32 - k - 1);
    uint my = 0 - y;

    z += Umulh(z, my * z);
    z += Umulh(z, my * z);

    uint q = Umulh(x, z);
    r = x - y * q;
    if(r >= y)
    {
        r = r - y;
        q = q + 1;
        if(r >= y)
        {
            r = r - y;
            q = q + 1;
        }
    }

    return q;
}

uint Div64_32_32(uint numHi, uint numLo, uint den)
{
    // based on https://github.com/ridiculousfish/libdivide/blob/3bd34388573681ce563348cdf04fe15d24770d04/libdivide.h#L469
    // modified to work with half the size 64/32=32 instead of 128/64=64
    // for further details see https://ridiculousfish.com/blog/posts/labor-of-division-episode-iv.html

    // We work in base 2**16.
    // A uint32 holds a single digit (in the lower 16 bit). A uint32 holds two digits.
    // Our numerator is conceptually [num3, num2, num1, num0].
    // Our denominator is [den1, den0].
    const uint b = (1U << 16);

    // Determine the normalization factor. We multiply den by this, so that its leading digit is at
    // least half b. In binary this means just shifting left by the number of leading zeros, so that
    // there's a 1 in the MSB.
    // We also shift numer by the same amount. This cannot overflow because numHi < den.
    // The expression (-shift & 63) is the same as (64 - shift), except it avoids the UB of shifting
    // by 64. (it's also UB in GLSL!!!!)
    uint shift = 31 - findMSB(den);
    den <<= shift;
    numHi <<= shift;
    numHi |= (numLo >> (-shift & 31U)) & uint(-int(shift) >> 31);
    numLo <<= shift;

    // Extract the low digits of the numerator and both digits of the denominator.
    uint num1 = (numLo >> 16);
    uint num0 = (numLo & 0xFFFFU);
    uint den1 = (den >> 16);
    uint den0 = (den & 0xFFFFU);

    // We wish to compute q1 = [n3 n2 n1] / [d1 d0].
    // Estimate q1 as [n3 n2] / [d1], and then correct it.
    // Note while qhat may be 2 digits, q1 is always 1 digit.

    uint rhat;
    uint qhat = Div(numHi, den1, rhat);
    uint c1 = qhat * den0;
    uint c2 = rhat * b + num1;
    if (c1 > c2) qhat -= (c1 - c2 > den) ? 2 : 1;
    uint q1 = qhat & 0xFFFFU;

    // Compute the true (partial) remainder.
    uint rem = numHi * b + num1 - q1 * den;

    // We wish to compute q0 = [rem1 rem0 n0] / [d1 d0].
    // Estimate q0 as [rem1 rem0] / [d1] and correct it.
    qhat = Div(rem, den1, rhat);
    c1 = qhat * den0;
    c2 = rhat * b + num0;
    if (c1 > c2) qhat -= (c1 - c2 > den) ? 2 : 1;

    return bitfieldInsert(qhat, q1, 16, 16);
}

int InterpolateAttrPersp(int y0, int y1, int ifactor)
{
    if (y0 == y1)
        return y0;

    if (y0 < y1)
        return y0 + (((y1-y0) * ifactor) >> YFactorShift);
    else
        return y1 + (((y0-y1) * ((1<<YFactorShift)-ifactor)) >> YFactorShift);
}

int InterpolateAttrLinear(int y0, int y1, int i, int irecip, int idiff)
{
    if (y0 == y1)
        return y0;

    uint numeratorLo, numeratorHi;
    uint denominator = uint(abs(idiff));
    if (y0 < y1)
    {
#ifndef Rasterise
        uint offset = uint(abs(i));
#else
        uint offset = uint(i);
#endif
        umulExtended(uint(y1 - y0), offset, numeratorHi, numeratorLo);
        uint quotient;
        if (numeratorHi == 0U)
        {
            uint remainder;
            quotient = Div(numeratorLo, denominator, remainder);
        }
        else
        {
            quotient = Div64_32_32(numeratorHi, numeratorLo, denominator);
        }
        return y0 + int(quotient);
    }
    else
    {
#ifndef Rasterise
        uint offset = uint(abs(idiff-i));
#else
        uint offset = uint(idiff-i);
#endif
        umulExtended(uint(y0 - y1), offset, numeratorHi, numeratorLo);
        uint quotient;
        if (numeratorHi == 0U)
        {
            uint remainder;
            quotient = Div(numeratorLo, denominator, remainder);
        }
        else
        {
            quotient = Div64_32_32(numeratorHi, numeratorLo, denominator);
        }
        return y1 + int(quotient);
    }
}

uint InterpolateZZBuffer(int z0, int z1, int i, int irecip, int idiff)
{
    if (z0 == z1)
        return z0;

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
        disp = uint(z0 - z1),
        factor = uint(abs(idiff - i));
    }

#ifdef InterpSpans
    int shiftl = 0;
    const int shiftr = 22;
    if (disp > 0x3FF)
    {
        shiftl = findMSB(disp) - 9;
        disp >>= shiftl;
    }
#else
    disp >>= 9;
    const int shiftl = 0;
    const int shiftr = 13;
#endif
    uint mulLo, mulHi;

    umulExtended(disp * factor, abs(irecip) >> 8, mulHi, mulLo);

    return base + (((mulLo >> shiftr) | (mulHi << (32 - shiftr))) << shiftl);
}

uint InterpolateZWBuffer(int z0, int z1, int ifactor)
{
    if (z0 == z1)
        return z0;

#ifdef Rasterise
    // since the precision along x spans is only 8 bit the result will always fit in 32-bit
    if (z0 < z1)
    {
        return uint(z0) + (((z1-z0) * ifactor) >> YFactorShift);
    }
    else
    {
        return uint(z1) + (((z0-z1) * ((1<<YFactorShift)-ifactor)) >> YFactorShift);
    }
#else
    uint mulLo, mulHi;
    if (z0 < z1)
    {
        umulExtended(z1-z0, ifactor, mulHi, mulLo);
        // 64-bit shift
        return uint(z0) + ((mulLo >> YFactorShift) | (mulHi << (32-YFactorShift)));
    }
    else
    {
        umulExtended(z0-z1, (1<<YFactorShift)-ifactor, mulHi, mulLo);
        return uint(z1) + ((mulLo >> YFactorShift) | (mulHi << (32-YFactorShift)));
    }
#endif
}
#endif

#endif // MELONPRIME_VULKAN_COMMON_GLSL
