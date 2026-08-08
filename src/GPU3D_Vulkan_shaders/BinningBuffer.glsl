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
    1:1 re-expression of ComputeRendererShaders::BinningBuffer
    (src/GPU3D_Compute_shaders.h), including the MelonPrimeDS members that the
    GL header emits under `#ifdef MELONPRIME_DS`. MELONPRIME_DS is defined
    unconditionally on the core target (src/CMakeLists.txt), so the
    MelonPrimeDS spelling is the only live one and is transcribed directly.

    Vulkan plumbing only: GL SSBO binding 6 -> set 0 binding 8.

    std430 offsets match ComputeRenderer3D::BinResultHeader byte for byte:
        VariantWorkCount[256]      uvec4  0     .. 4095   (stride 16)
        SortedWorkOffset[256]      uint   4096  .. 5119
        VariantWorkRealCount[256]  uint   5120  .. 6143
        SortWorkWorkCount          uvec4  6144  .. 6159   (16-byte aligned)
        BinningMaskAndOffset[]     uint   6160  ..
*/

#ifndef MELONPRIME_VULKAN_BINNINGBUFFER_GLSL
#define MELONPRIME_VULKAN_BINNINGBUFFER_GLSL

layout (std430, set = 0, binding = 8) buffer BinResultBuffer
{
    uvec4 VariantWorkCount[MaxVariants];
    uint SortedWorkOffset[MaxVariants];
    // MelonPrimeDS: see BinResultHeader in GPU3D_Compute.h - keeps the real work
    // count while VariantWorkCount[v].y/.z carry the split dispatch pair.
    uint VariantWorkRealCount[MaxVariants];

    uvec4 SortWorkWorkCount;

    uint BinningMaskAndOffset[];
    //uint BinnedMaskCoarse[TilesPerLine*TileLines*CoarseBinStride];
    //uint BinnedMask[TilesPerLine*TileLines*BinStride];
    //uint WorkOffsets[TilesPerLine*TileLines*BinStride];
};

// MelonPrimeDS: max Z group count per rasterise dispatch. Must stay <= 65535
// (GL minimum / NVIDIA hard limit for the Y and Z dimensions). Vulkan's
// maxComputeWorkGroupCount minimum is 65535 per dimension as well, so the
// same split applies unchanged.
const uint RasteriseChunkSize = 32768U;

const int BinningCoarseMaskStart = 0;
const int BinningMaskStart = BinningCoarseMaskStart+TilesPerLine*TileLines*CoarseBinStride;
const int BinningWorkOffsetsStart = BinningMaskStart+TilesPerLine*TileLines*BinStride;

#endif // MELONPRIME_VULKAN_BINNINGBUFFER_GLSL
