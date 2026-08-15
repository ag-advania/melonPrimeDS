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
    Shared prologue of the two *presentation* stages, Resolve.comp and
    Compositor.comp.

    Unlike every other module in this directory these two have no counterpart in
    src/GPU3D_Compute.cpp: the OpenGL compute renderer hands its FinalFB texture
    to the GL 2D engine, which does the equivalent work with fixed-function
    blending. This backend is paired with the *software* 2D renderer instead, so
    the same two jobs -- downscale to the DS's native resolution for display
    capture, and combine the structured 2D planes with the internal-resolution 3D
    image for display -- are compute stages here.

    Everything below is derived from the software renderer, which is the ground
    truth for DS pixel output:

      * src/GPU_ColorOp.h              ColorBlend4 / ColorBlend5 /
                                       ColorBrightnessUp / ColorBrightnessDown
      * src/GPU2D_Soft.cpp             SoftRenderer2D::ColorComposite()
      * src/GPU_Soft.cpp               ApplyMasterBrightness() / ExpandColor()
      * src/MelonPrimeStructuredComposition.h
                                       the plane / control-word / line-metadata
                                       bit layout, which is a repo-owned contract
                                       shared with the DX12 backend

    Extra descriptor bindings these two stages add to the set 0 contract:

      12  storage buffer readonly  StructuredInput      (std430)
      13  storage buffer           PresentationOutput   (std430)

    Both are appended after FinalFB (11); no existing binding number moved.
*/

#ifndef MELONPRIME_VULKAN_PRESENTATION_GLSL
#define MELONPRIME_VULKAN_PRESENTATION_GLSL

// Internal resolution, recovered from the ScreenWidth specialization constant.
// The whole expression folds through OpSpecConstantOp at pipeline creation, so
// the loop bounds below are as good as literal for the driver's optimizer.
const int ScaleFactor = ScreenWidth / 256;

const int NativeWidth = 256;
const int NativeHeight = 192;
const int NativePixelCount = NativeWidth * NativeHeight;

// ---------------------------------------------------------------------------
// FinalFB, the internal-resolution 3D image FinalPass.comp produced
// ---------------------------------------------------------------------------
// Same descriptor as FinalPass's, read instead of written. FinalPass stores
// `vec4(r6, g6, b6, a5) / vec4(63, 63, 63, 31)` into a VK_FORMAT_R8G8B8A8_UNORM
// image, so the DS's packed r6g6b6a5 word has to be reconstructed here.
//
// The rgba8 format qualifier is mandatory: without it, reading a storage image
// would require the shaderStorageImageReadWithoutFormat feature, which Vulkan
// does not guarantee.
layout (set = 0, binding = 11, rgba8) readonly uniform image2D FinalFB;

// n-bit -> UNORM8 -> n-bit round trips exactly for n = 5 and n = 6, because
// v8 = round(v * 255/n_max) is strictly increasing with a step of at least 4,
// so the nearest-integer inverse cannot land on a neighbour. This is the same
// inverse the host applies in VulkanRenderer3D (Unorm8ToRgb6 / Unorm8ToAlpha5),
// spelled the same way so the two cannot drift.
uint Unorm8ToRgb6(uint value) { return (value * 63u + 127u) / 255u; }
uint Unorm8ToAlpha5(uint value) { return (value * 31u + 127u) / 255u; }

// The packed r6g6b6a5 word at one internal-resolution pixel of the 3D image.
uint LoadFinalFB(ivec2 position)
{
    vec4 texel = imageLoad(FinalFB, position);

    // round() rather than a truncating cast: an UNORM8 load yields k/255, which
    // is not exactly representable in binary floating point, so truncation
    // would occasionally return k-1.
    uvec4 quantized = uvec4(round(texel * 255.0));

    return Unorm8ToRgb6(quantized.r)
        | (Unorm8ToRgb6(quantized.g) << 8)
        | (Unorm8ToRgb6(quantized.b) << 16)
        | (Unorm8ToAlpha5(quantized.a) << 24);
}

// ---------------------------------------------------------------------------
// DS colour arithmetic
// ---------------------------------------------------------------------------
// GPU_ColorOp.h performs these packed, three channels at a time, relying on the
// fact that no intermediate can carry into the next channel's bits. Spelled per
// channel here because the shaders need the components separately anyway; the
// clamps below are the same ones the packed form applies through its masks.

uint Color6R(uint color) { return color & 0x3Fu; }
uint Color6G(uint color) { return (color >> 8) & 0x3Fu; }
uint Color6B(uint color) { return (color >> 16) & 0x3Fu; }

uint PackColor6(uint r, uint g, uint b, uint alpha)
{
    return min(r, 63u) | (min(g, 63u) << 8) | (min(b, 63u) << 16) | (alpha << 24);
}

// ColorBlend4(val1, val2, eva, evb).
uint Blend4(uint first, uint second, uint eva, uint evb)
{
    return PackColor6(
        ((Color6R(first) * eva) + (Color6R(second) * evb) + 8u) >> 4,
        ((Color6G(first) * eva) + (Color6G(second) * evb) + 8u) >> 4,
        ((Color6B(first) * eva) + (Color6B(second) * evb) + 8u) >> 4,
        0xFFu);
}

// ColorBlend5(val1, val2): the first operand's own 5-bit alpha is the weight.
// GPU_ColorOp.h short-circuits eva == 32 by returning val1 unchanged; the
// arithmetic below already produces val1's colour in that case ((c*32 + 16) >> 5
// == c), and the alpha byte it differs in is discarded by ToBgra8().
uint Blend5(uint first, uint second)
{
    uint eva = ((first >> 24) & 0x1Fu) + 1u;
    uint evb = 32u - eva;
    return PackColor6(
        ((Color6R(first) * eva) + (Color6R(second) * evb) + 16u) >> 5,
        ((Color6G(first) * eva) + (Color6G(second) * evb) + 16u) >> 5,
        ((Color6B(first) * eva) + (Color6B(second) * evb) + 16u) >> 5,
        0xFFu);
}

// ColorBrightnessUp(val, factor, bias). The bias differs by caller: 0x8 for the
// 2D colour effect, 0x0 for master brightness (GPU_Soft.cpp
// ApplyMasterBrightness).
uint BrightnessUp(uint color, uint factor, uint bias)
{
    return PackColor6(
        Color6R(color) + ((((63u - Color6R(color)) * factor) + bias) >> 4),
        Color6G(color) + ((((63u - Color6G(color)) * factor) + bias) >> 4),
        Color6B(color) + ((((63u - Color6B(color)) * factor) + bias) >> 4),
        0xFFu);
}

// ColorBrightnessDown(val, factor, bias): 0x7 for the 2D colour effect, 0xF for
// master brightness.
uint BrightnessDown(uint color, uint factor, uint bias)
{
    return PackColor6(
        Color6R(color) - (((Color6R(color) * factor) + bias) >> 4),
        Color6G(color) - (((Color6G(color) * factor) + bias) >> 4),
        Color6B(color) - (((Color6B(color) * factor) + bias) >> 4),
        0xFFu);
}

// SoftRenderer::ExpandColor(): 6-bit channels widened to 8 by replicating the
// top two bits, packed BGRA with an opaque alpha byte. This is the exact word
// layout the Qt/present path expects from a CpuBgra renderer output.
uint ToBgra8(uint color)
{
    uint r6 = Color6R(color);
    uint g6 = Color6G(color);
    uint b6 = Color6B(color);
    uint r8 = (r6 << 2) | (r6 >> 4);
    uint g8 = (g6 << 2) | (g6 >> 4);
    uint b8 = (b6 << 2) | (b6 >> 4);
    return b8 | (g8 << 8) | (r8 << 16) | 0xFF000000u;
}

#endif // MELONPRIME_VULKAN_PRESENTATION_GLSL
