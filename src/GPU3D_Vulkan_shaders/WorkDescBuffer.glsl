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
    1:1 re-expression of ComputeRendererShaders::WorkDescBuffer
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only: GL SSBO binding 7 -> set 0 binding 9.

    structure of each WorkDesc item:
        x:
            bits 0-10: polygon idx
            bits 11-31: tile idx (before sorting within variant after sorting within all tiles)
        y:
            bits 0-15: X position on screen
            bits 15-31: Y position on screen
*/

#ifndef MELONPRIME_VULKAN_WORKDESCBUFFER_GLSL
#define MELONPRIME_VULKAN_WORKDESCBUFFER_GLSL

layout (std430, set = 0, binding = 9) buffer WorkDescBuffer
{
    //uvec2 UnsortedWorkDescs[MaxWorkTiles];
    //uvec2 SortedWorkDescs[MaxWorkTiles];
    uvec2 WorkDescs[];
};

const uint WorkDescsUnsortedStart = 0;
// MaxWorkTiles is a specialization constant (see Common.glsl), so the cast is
// spelled explicitly: OpSpecConstantOp has no implicit int->uint conversion.
const uint WorkDescsSortedStart = WorkDescsUnsortedStart+uint(MaxWorkTiles);

#endif // MELONPRIME_VULKAN_WORKDESCBUFFER_GLSL
