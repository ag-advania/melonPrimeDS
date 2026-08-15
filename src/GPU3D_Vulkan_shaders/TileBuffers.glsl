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
    1:1 re-expression of ComputeRendererShaders::Tilebuffers
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only. The GL renderer reused SSBO bindings 2/3/4 for the
    tile buffers, rebinding over XSpanSetups/YSpanSetups between passes
    (GPU3D_Compute.cpp: glBindBufferBase(..., 2+i, TileMemory[i])). Vulkan has a
    single binding space and one descriptor set for all 33 pipelines, so the
    tile buffers get their own bindings and nothing is ever rebound:
        ColorTiles -> set 0 binding 4
        DepthTiles -> set 0 binding 5
        AttrTiles  -> set 0 binding 6
*/

#ifndef MELONPRIME_VULKAN_TILEBUFFERS_GLSL
#define MELONPRIME_VULKAN_TILEBUFFERS_GLSL

layout (std430, set = 0, binding = 4) buffer ColorTileBuffer
{
    uint ColorTiles[];
};
layout (std430, set = 0, binding = 5) buffer DepthTileBuffer
{
    uint DepthTiles[];
};
layout (std430, set = 0, binding = 6) buffer AttrTileBuffer
{
    uint AttrTiles[];
};

#endif // MELONPRIME_VULKAN_TILEBUFFERS_GLSL
