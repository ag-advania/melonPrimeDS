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
    1:1 re-expression of ComputeRendererShaders::ResultBuffer
    (src/GPU3D_Compute_shaders.h).

    Vulkan plumbing only: GL SSBO binding 5 -> set 0 binding 7.

    ScreenWidth/ScreenHeight are specialization constants, so the section
    offsets fold into OpSpecConstantOp and are resolved at pipeline creation.
    The casts are explicit because OpSpecConstantOp performs no implicit
    int->uint conversion.
*/

#ifndef MELONPRIME_VULKAN_RESULTBUFFER_GLSL
#define MELONPRIME_VULKAN_RESULTBUFFER_GLSL

layout (std430, set = 0, binding = 7) buffer ResultBuffer
{
    uint ResultValue[];
};

const uint ResultColorStart = 0;
const uint ResultDepthStart = ResultColorStart+uint(ScreenWidth*ScreenHeight*2);
const uint ResultAttrStart = ResultDepthStart+uint(ScreenWidth*ScreenHeight*2);

#endif // MELONPRIME_VULKAN_RESULTBUFFER_GLSL
