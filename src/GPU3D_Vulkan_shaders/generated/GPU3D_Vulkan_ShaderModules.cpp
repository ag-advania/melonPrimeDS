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
        { detail::Blob036, detail::Blob036_Words }, // CorrectCoverage
    },
    // bucket 1: TileSize 16, CoarseTileCountY 4, CoarseTileArea 32, ClearCoarseBinMaskLocalSize 64
    {
        { detail::Blob037, detail::Blob037_Words }, // InterpSpansZ
        { detail::Blob038, detail::Blob038_Words }, // InterpSpansW
        { detail::Blob039, detail::Blob039_Words }, // BinCombined
        { detail::Blob040, detail::Blob040_Words }, // DepthBlendZ
        { detail::Blob041, detail::Blob041_Words }, // DepthBlendW
        { detail::Blob042, detail::Blob042_Words }, // RasteriseNoTextureZ
        { detail::Blob043, detail::Blob043_Words }, // RasteriseNoTextureW
        { detail::Blob044, detail::Blob044_Words }, // RasteriseNoTextureToonZ
        { detail::Blob045, detail::Blob045_Words }, // RasteriseNoTextureToonW
        { detail::Blob046, detail::Blob046_Words }, // RasteriseNoTextureHighlightZ
        { detail::Blob047, detail::Blob047_Words }, // RasteriseNoTextureHighlightW
        { detail::Blob048, detail::Blob048_Words }, // RasteriseUseTextureDecalZ
        { detail::Blob049, detail::Blob049_Words }, // RasteriseUseTextureDecalW
        { detail::Blob050, detail::Blob050_Words }, // RasteriseUseTextureModulateZ
        { detail::Blob051, detail::Blob051_Words }, // RasteriseUseTextureModulateW
        { detail::Blob052, detail::Blob052_Words }, // RasteriseUseTextureToonZ
        { detail::Blob053, detail::Blob053_Words }, // RasteriseUseTextureToonW
        { detail::Blob054, detail::Blob054_Words }, // RasteriseUseTextureHighlightZ
        { detail::Blob055, detail::Blob055_Words }, // RasteriseUseTextureHighlightW
        { detail::Blob056, detail::Blob056_Words }, // RasteriseShadowMaskZ
        { detail::Blob057, detail::Blob057_Words }, // RasteriseShadowMaskW
        { detail::Blob058, detail::Blob058_Words }, // ClearCoarseBinMask
        { detail::Blob059, detail::Blob059_Words }, // ClearIndirectWorkCount
        { detail::Blob060, detail::Blob060_Words }, // CalculateWorkOffsets
        { detail::Blob061, detail::Blob061_Words }, // SortWork
        { detail::Blob062, detail::Blob062_Words }, // FinalPass0
        { detail::Blob063, detail::Blob063_Words }, // FinalPass1
        { detail::Blob064, detail::Blob064_Words }, // FinalPass2
        { detail::Blob065, detail::Blob065_Words }, // FinalPass3
        { detail::Blob066, detail::Blob066_Words }, // FinalPass4
        { detail::Blob067, detail::Blob067_Words }, // FinalPass5
        { detail::Blob068, detail::Blob068_Words }, // FinalPass6
        { detail::Blob069, detail::Blob069_Words }, // FinalPass7
        { detail::Blob070, detail::Blob070_Words }, // Resolve
        { detail::Blob071, detail::Blob071_Words }, // CaptureSidecar
        { detail::Blob072, detail::Blob072_Words }, // Compositor
        { detail::Blob073, detail::Blob073_Words }, // CorrectCoverage
    },
    // bucket 2: TileSize 32, CoarseTileCountY 6, CoarseTileArea 48, ClearCoarseBinMaskLocalSize 48
    {
        { detail::Blob074, detail::Blob074_Words }, // InterpSpansZ
        { detail::Blob075, detail::Blob075_Words }, // InterpSpansW
        { detail::Blob076, detail::Blob076_Words }, // BinCombined
        { detail::Blob077, detail::Blob077_Words }, // DepthBlendZ
        { detail::Blob078, detail::Blob078_Words }, // DepthBlendW
        { detail::Blob079, detail::Blob079_Words }, // RasteriseNoTextureZ
        { detail::Blob080, detail::Blob080_Words }, // RasteriseNoTextureW
        { detail::Blob081, detail::Blob081_Words }, // RasteriseNoTextureToonZ
        { detail::Blob082, detail::Blob082_Words }, // RasteriseNoTextureToonW
        { detail::Blob083, detail::Blob083_Words }, // RasteriseNoTextureHighlightZ
        { detail::Blob084, detail::Blob084_Words }, // RasteriseNoTextureHighlightW
        { detail::Blob085, detail::Blob085_Words }, // RasteriseUseTextureDecalZ
        { detail::Blob086, detail::Blob086_Words }, // RasteriseUseTextureDecalW
        { detail::Blob087, detail::Blob087_Words }, // RasteriseUseTextureModulateZ
        { detail::Blob088, detail::Blob088_Words }, // RasteriseUseTextureModulateW
        { detail::Blob089, detail::Blob089_Words }, // RasteriseUseTextureToonZ
        { detail::Blob090, detail::Blob090_Words }, // RasteriseUseTextureToonW
        { detail::Blob091, detail::Blob091_Words }, // RasteriseUseTextureHighlightZ
        { detail::Blob092, detail::Blob092_Words }, // RasteriseUseTextureHighlightW
        { detail::Blob093, detail::Blob093_Words }, // RasteriseShadowMaskZ
        { detail::Blob094, detail::Blob094_Words }, // RasteriseShadowMaskW
        { detail::Blob095, detail::Blob095_Words }, // ClearCoarseBinMask
        { detail::Blob096, detail::Blob096_Words }, // ClearIndirectWorkCount
        { detail::Blob097, detail::Blob097_Words }, // CalculateWorkOffsets
        { detail::Blob098, detail::Blob098_Words }, // SortWork
        { detail::Blob099, detail::Blob099_Words }, // FinalPass0
        { detail::Blob100, detail::Blob100_Words }, // FinalPass1
        { detail::Blob101, detail::Blob101_Words }, // FinalPass2
        { detail::Blob102, detail::Blob102_Words }, // FinalPass3
        { detail::Blob103, detail::Blob103_Words }, // FinalPass4
        { detail::Blob104, detail::Blob104_Words }, // FinalPass5
        { detail::Blob105, detail::Blob105_Words }, // FinalPass6
        { detail::Blob106, detail::Blob106_Words }, // FinalPass7
        { detail::Blob107, detail::Blob107_Words }, // Resolve
        { detail::Blob108, detail::Blob108_Words }, // CaptureSidecar
        { detail::Blob109, detail::Blob109_Words }, // Compositor
        { detail::Blob110, detail::Blob110_Words }, // CorrectCoverage
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
    "CorrectCoverage",
};

} // namespace VulkanShaders
} // namespace melonDS
