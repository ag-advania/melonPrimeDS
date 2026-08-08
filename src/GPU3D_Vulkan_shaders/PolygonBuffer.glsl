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
    1:1 re-expression of ComputeRendererShaders::PolygonBuffer
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only: GL SSBO binding 0 -> set 0 binding 1.

    std430 layout is byte-identical to ComputeRenderer3D::RenderPolygon:
    ten 4-byte scalars, base alignment 4, array stride 40.
*/

#ifndef MELONPRIME_VULKAN_POLYGONBUFFER_GLSL
#define MELONPRIME_VULKAN_POLYGONBUFFER_GLSL

struct Polygon
{
    int FirstXSpan;
    int YTop, YBot;

    int XMin, XMax;
    int XMinY, XMaxY;

    int Variant;

    uint Attr;

    float TextureLayer;
};

layout (std430, set = 0, binding = 1) readonly buffer PolygonBuffer
{
    Polygon Polygons[];
};

#endif // MELONPRIME_VULKAN_POLYGONBUFFER_GLSL
