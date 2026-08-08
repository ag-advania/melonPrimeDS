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
    The two storage buffers the presentation stages added to the set 0 binding
    contract. Declared in one place so Resolve.comp and Compositor.comp cannot
    disagree about the binding numbers, and so VulkanDescriptors.h has a single
    GLSL counterpart to be checked against.

    set 0 binding 12  StructuredInput     readonly, written by the host
    set 0 binding 13  PresentationOutput  written by whichever stage is running

    PresentationOutput is one binding rather than two because the two stages
    never run in the same dispatch and their outputs have different sizes and
    different lifetimes: the host binds the native-resolution capture buffer for
    Resolve and the two-screen composed buffer for Compositor. Giving them
    separate bindings would force every descriptor set to reference both.
*/

#ifndef MELONPRIME_VULKAN_PRESENTATIONBUFFERS_GLSL
#define MELONPRIME_VULKAN_PRESENTATIONBUFFERS_GLSL

// The structured 2D frame the software renderer published, packed by the host
// in exactly this order (VulkanRenderer3D::ComposeStructuredOutput):
//
//   [0 .. 3*NativePixelCount)                 screen 0, planes below/above/control
//   [3*NativePixelCount .. 6*NativePixelCount) screen 1, same three planes
//   [6*NativePixelCount .. +192)              screen 0 line metadata
//   [6*NativePixelCount+192 .. +192)          screen 1 line metadata
//
// Screen 0 is the top LCD and screen 1 the bottom, already resolved:
// SoftRenderer::DrawScanline() maps engine -> screen through GPU.ScreenSwap
// before it fills these planes, so the compositor never sees POWCNT1 bit 15.
layout (std430, set = 0, binding = 12) readonly buffer StructuredInputBuffer
{
    uint StructuredInput[];
};

layout (std430, set = 0, binding = 13) buffer PresentationOutputBuffer
{
    uint PresentationOutput[];
};

const uint StructuredPlaneStride = uint(NativePixelCount);
const uint StructuredScreenStride = 3u * StructuredPlaneStride;
const uint StructuredLineMetaBase = 6u * StructuredPlaneStride;

#endif // MELONPRIME_VULKAN_PRESENTATIONBUFFERS_GLSL
