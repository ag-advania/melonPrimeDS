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
    set 0 binding 14  CaptureSidecar      persistent high-resolution capture data

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
//   [0 .. 4*NativePixelCount)                  screen 0, below/above/control/capture-ref
//   [4*NativePixelCount .. 8*NativePixelCount) screen 1, same four planes
//   [8*NativePixelCount .. 12*NativePixelCount) engine-A capture source planes
//   [12*NativePixelCount .. 13*NativePixelCount) capture source-B native colour
//   [13*NativePixelCount .. 14*NativePixelCount) capture source-B references
//   [14*NativePixelCount .. +384)              two screen line-metadata arrays
//   [.. +768)                                  192 four-word capture commands
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

layout (std430, set = 0, binding = 14) buffer CaptureSidecarBuffer
{
    uint CaptureSidecarPixels[];
};

// The compositor uses these only when the host sets pc._pad to one. Resolve,
// CaptureSidecar, and the fallback compositor path still receive valid image
// descriptors, but do not touch them. Separate 2D images keep the existing
// presenter layer model (one view per LCD) and avoid an array-layer change in
// the graphics presenter.
layout (rgba8, set = 0, binding = 17) writeonly uniform image2D DirectOutputTop;
layout (rgba8, set = 0, binding = 18) writeonly uniform image2D DirectOutputBottom;

const uint StructuredPlaneStride = uint(NativePixelCount);
const uint StructuredScreenStride = 4u * StructuredPlaneStride;
const uint StructuredCaptureSourceBase = 8u * StructuredPlaneStride;
const uint StructuredCaptureSourceBNativeBase = 12u * StructuredPlaneStride;
const uint StructuredCaptureSourceBReferenceBase = 13u * StructuredPlaneStride;
const uint StructuredLineMetaBase = 14u * StructuredPlaneStride;
const uint StructuredCaptureCommandBase = StructuredLineMetaBase + 384u;
const uint StructuredCaptureCommandIndependent = 1u << 5u;

uint LoadCaptureSidecar(uint reference, uvec2 scaledWithinPixel)
{
    uint address = reference & 0xFFFFu;
    uint bank = (reference >> 28u) & 0x3u;
    uint version = (reference >> 30u) & 0x1u;
    uint samplesPerPixel = uint(ScaleFactor) * uint(ScaleFactor);
    uint cell = ((version * 4u + bank) * 65536u) + address;
    uint sampleIndex = scaledWithinPixel.y * uint(ScaleFactor) + scaledWithinPixel.x;
    return CaptureSidecarPixels[cell * samplesPerPixel + sampleIndex];
}

#endif // MELONPRIME_VULKAN_PRESENTATIONBUFFERS_GLSL
