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
    GENERATED FILE -- DO NOT EDIT.

    Produced by tools/vulkan/compile-shaders.py from the GLSL sources in
    src/GPU3D_Vulkan_shaders/. Edit the .comp/.glsl sources and rerun the
    generator; commit source and generated output together.

    Holds every compute pipeline of the Vulkan rasterizer, compiled for
    SPIR-V 1.3 (vulkan1.1), for each of the three tile-geometry buckets.
    Pipeline indices match ComputeRenderer3D::ShaderCompileStep() in
    src/GPU3D_Compute.cpp one for one.

    The SPIR-V words live in per-stage translation units rather than in the
    header so that including the public header stays cheap: the full module set
    is a few megabytes of integer literals and would otherwise be re-parsed by
    every consumer.
*/

#include "GPU3D_Vulkan_ShaderModules.h"
#include "GPU3D_Vulkan_ShaderBlobs.h"

namespace melonDS
{
namespace VulkanShaders
{

const ShaderModule Modules[TileGeometryBucketCount][Pipeline_Count] =
{
    // bucket 0: TileSize 8, CoarseTileCountY 4, CoarseTileArea 32, ClearCoarseBinMaskLocalSize 64
    {
        { detail::Blob000, detail::Blob000_Words }, // InterpSpansZ
        { detail::Blob001, detail::Blob001_Words }, // InterpSpansW
        { detail::Blob002, detail::Blob002_Words }, // BinCombined
        { detail::Blob003, detail::Blob003_Words }, // DepthBlendZ
        { detail::Blob004, detail::Blob004_Words }, // DepthBlendW
        { detail::Blob005, detail::Blob005_Words }, // RasteriseNoTextureZ
        { detail::Blob006, detail::Blob006_Words }, // RasteriseNoTextureW
        { detail::Blob007, detail::Blob007_Words }, // RasteriseNoTextureToonZ
        { detail::Blob008, detail::Blob008_Words }, // RasteriseNoTextureToonW
        { detail::Blob009, detail::Blob009_Words }, // RasteriseNoTextureHighlightZ
        { detail::Blob010, detail::Blob010_Words }, // RasteriseNoTextureHighlightW
        { detail::Blob011, detail::Blob011_Words }, // RasteriseUseTextureDecalZ
        { detail::Blob012, detail::Blob012_Words }, // RasteriseUseTextureDecalW
        { detail::Blob013, detail::Blob013_Words }, // RasteriseUseTextureModulateZ
        { detail::Blob014, detail::Blob014_Words }, // RasteriseUseTextureModulateW
        { detail::Blob015, detail::Blob015_Words }, // RasteriseUseTextureToonZ
        { detail::Blob016, detail::Blob016_Words }, // RasteriseUseTextureToonW
        { detail::Blob017, detail::Blob017_Words }, // RasteriseUseTextureHighlightZ
        { detail::Blob018, detail::Blob018_Words }, // RasteriseUseTextureHighlightW
        { detail::Blob019, detail::Blob019_Words }, // RasteriseShadowMaskZ
        { detail::Blob020, detail::Blob020_Words }, // RasteriseShadowMaskW
        { detail::Blob021, detail::Blob021_Words }, // ClearCoarseBinMask
        { detail::Blob022, detail::Blob022_Words }, // ClearIndirectWorkCount
        { detail::Blob023, detail::Blob023_Words }, // CalculateWorkOffsets
        { detail::Blob024, detail::Blob024_Words }, // SortWork
        { detail::Blob025, detail::Blob025_Words }, // FinalPass0
        { detail::Blob026, detail::Blob026_Words }, // FinalPass1
        { detail::Blob027, detail::Blob027_Words }, // FinalPass2
        { detail::Blob028, detail::Blob028_Words }, // FinalPass3
        { detail::Blob029, detail::Blob029_Words }, // FinalPass4
        { detail::Blob030, detail::Blob030_Words }, // FinalPass5
        { detail::Blob031, detail::Blob031_Words }, // FinalPass6
        { detail::Blob032, detail::Blob032_Words }, // FinalPass7
        { detail::Blob033, detail::Blob033_Words }, // Resolve
        { detail::Blob034, detail::Blob034_Words }, // CaptureSidecar
        { detail::Blob035, detail::Blob035_Words }, // Compositor
    },
    // bucket 1: TileSize 16, CoarseTileCountY 4, CoarseTileArea 32, ClearCoarseBinMaskLocalSize 64
    {
        { detail::Blob036, detail::Blob036_Words }, // InterpSpansZ
        { detail::Blob037, detail::Blob037_Words }, // InterpSpansW
        { detail::Blob038, detail::Blob038_Words }, // BinCombined
        { detail::Blob039, detail::Blob039_Words }, // DepthBlendZ
        { detail::Blob040, detail::Blob040_Words }, // DepthBlendW
        { detail::Blob041, detail::Blob041_Words }, // RasteriseNoTextureZ
        { detail::Blob042, detail::Blob042_Words }, // RasteriseNoTextureW
        { detail::Blob043, detail::Blob043_Words }, // RasteriseNoTextureToonZ
        { detail::Blob044, detail::Blob044_Words }, // RasteriseNoTextureToonW
        { detail::Blob045, detail::Blob045_Words }, // RasteriseNoTextureHighlightZ
        { detail::Blob046, detail::Blob046_Words }, // RasteriseNoTextureHighlightW
        { detail::Blob047, detail::Blob047_Words }, // RasteriseUseTextureDecalZ
        { detail::Blob048, detail::Blob048_Words }, // RasteriseUseTextureDecalW
        { detail::Blob049, detail::Blob049_Words }, // RasteriseUseTextureModulateZ
        { detail::Blob050, detail::Blob050_Words }, // RasteriseUseTextureModulateW
        { detail::Blob051, detail::Blob051_Words }, // RasteriseUseTextureToonZ
        { detail::Blob052, detail::Blob052_Words }, // RasteriseUseTextureToonW
        { detail::Blob053, detail::Blob053_Words }, // RasteriseUseTextureHighlightZ
        { detail::Blob054, detail::Blob054_Words }, // RasteriseUseTextureHighlightW
        { detail::Blob055, detail::Blob055_Words }, // RasteriseShadowMaskZ
        { detail::Blob056, detail::Blob056_Words }, // RasteriseShadowMaskW
        { detail::Blob057, detail::Blob057_Words }, // ClearCoarseBinMask
        { detail::Blob058, detail::Blob058_Words }, // ClearIndirectWorkCount
        { detail::Blob059, detail::Blob059_Words }, // CalculateWorkOffsets
        { detail::Blob060, detail::Blob060_Words }, // SortWork
        { detail::Blob061, detail::Blob061_Words }, // FinalPass0
        { detail::Blob062, detail::Blob062_Words }, // FinalPass1
        { detail::Blob063, detail::Blob063_Words }, // FinalPass2
        { detail::Blob064, detail::Blob064_Words }, // FinalPass3
        { detail::Blob065, detail::Blob065_Words }, // FinalPass4
        { detail::Blob066, detail::Blob066_Words }, // FinalPass5
        { detail::Blob067, detail::Blob067_Words }, // FinalPass6
        { detail::Blob068, detail::Blob068_Words }, // FinalPass7
        { detail::Blob069, detail::Blob069_Words }, // Resolve
        { detail::Blob070, detail::Blob070_Words }, // CaptureSidecar
        { detail::Blob071, detail::Blob071_Words }, // Compositor
    },
    // bucket 2: TileSize 32, CoarseTileCountY 6, CoarseTileArea 48, ClearCoarseBinMaskLocalSize 48
    {
        { detail::Blob072, detail::Blob072_Words }, // InterpSpansZ
        { detail::Blob073, detail::Blob073_Words }, // InterpSpansW
        { detail::Blob074, detail::Blob074_Words }, // BinCombined
        { detail::Blob075, detail::Blob075_Words }, // DepthBlendZ
        { detail::Blob076, detail::Blob076_Words }, // DepthBlendW
        { detail::Blob077, detail::Blob077_Words }, // RasteriseNoTextureZ
        { detail::Blob078, detail::Blob078_Words }, // RasteriseNoTextureW
        { detail::Blob079, detail::Blob079_Words }, // RasteriseNoTextureToonZ
        { detail::Blob080, detail::Blob080_Words }, // RasteriseNoTextureToonW
        { detail::Blob081, detail::Blob081_Words }, // RasteriseNoTextureHighlightZ
        { detail::Blob082, detail::Blob082_Words }, // RasteriseNoTextureHighlightW
        { detail::Blob083, detail::Blob083_Words }, // RasteriseUseTextureDecalZ
        { detail::Blob084, detail::Blob084_Words }, // RasteriseUseTextureDecalW
        { detail::Blob085, detail::Blob085_Words }, // RasteriseUseTextureModulateZ
        { detail::Blob086, detail::Blob086_Words }, // RasteriseUseTextureModulateW
        { detail::Blob087, detail::Blob087_Words }, // RasteriseUseTextureToonZ
        { detail::Blob088, detail::Blob088_Words }, // RasteriseUseTextureToonW
        { detail::Blob089, detail::Blob089_Words }, // RasteriseUseTextureHighlightZ
        { detail::Blob090, detail::Blob090_Words }, // RasteriseUseTextureHighlightW
        { detail::Blob091, detail::Blob091_Words }, // RasteriseShadowMaskZ
        { detail::Blob092, detail::Blob092_Words }, // RasteriseShadowMaskW
        { detail::Blob093, detail::Blob093_Words }, // ClearCoarseBinMask
        { detail::Blob094, detail::Blob094_Words }, // ClearIndirectWorkCount
        { detail::Blob095, detail::Blob095_Words }, // CalculateWorkOffsets
        { detail::Blob096, detail::Blob096_Words }, // SortWork
        { detail::Blob097, detail::Blob097_Words }, // FinalPass0
        { detail::Blob098, detail::Blob098_Words }, // FinalPass1
        { detail::Blob099, detail::Blob099_Words }, // FinalPass2
        { detail::Blob100, detail::Blob100_Words }, // FinalPass3
        { detail::Blob101, detail::Blob101_Words }, // FinalPass4
        { detail::Blob102, detail::Blob102_Words }, // FinalPass5
        { detail::Blob103, detail::Blob103_Words }, // FinalPass6
        { detail::Blob104, detail::Blob104_Words }, // FinalPass7
        { detail::Blob105, detail::Blob105_Words }, // Resolve
        { detail::Blob106, detail::Blob106_Words }, // CaptureSidecar
        { detail::Blob107, detail::Blob107_Words }, // Compositor
    },
};

const char* const PipelineNames[Pipeline_Count] =
{
    "InterpSpansZ",
    "InterpSpansW",
    "BinCombined",
    "DepthBlendZ",
    "DepthBlendW",
    "RasteriseNoTextureZ",
    "RasteriseNoTextureW",
    "RasteriseNoTextureToonZ",
    "RasteriseNoTextureToonW",
    "RasteriseNoTextureHighlightZ",
    "RasteriseNoTextureHighlightW",
    "RasteriseUseTextureDecalZ",
    "RasteriseUseTextureDecalW",
    "RasteriseUseTextureModulateZ",
    "RasteriseUseTextureModulateW",
    "RasteriseUseTextureToonZ",
    "RasteriseUseTextureToonW",
    "RasteriseUseTextureHighlightZ",
    "RasteriseUseTextureHighlightW",
    "RasteriseShadowMaskZ",
    "RasteriseShadowMaskW",
    "ClearCoarseBinMask",
    "ClearIndirectWorkCount",
    "CalculateWorkOffsets",
    "SortWork",
    "FinalPass0",
    "FinalPass1",
    "FinalPass2",
    "FinalPass3",
    "FinalPass4",
    "FinalPass5",
    "FinalPass6",
    "FinalPass7",
    "Resolve",
    "CaptureSidecar",
    "Compositor",
};

} // namespace VulkanShaders
} // namespace melonDS
