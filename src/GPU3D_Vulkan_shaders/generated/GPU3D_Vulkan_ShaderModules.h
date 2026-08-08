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

#ifndef GPU3D_VULKAN_SHADERMODULES_H
#define GPU3D_VULKAN_SHADERMODULES_H

#include <cstdint>

namespace melonDS
{
namespace VulkanShaders
{

// Descriptor set 0 bindings (see src/GPU3D_Vulkan_shaders/Common.glsl).
enum Set0Binding : uint32_t
{
    Set0Binding_MetaUniform = 0,
    Set0Binding_PolygonBuffer = 1,
    Set0Binding_XSpanSetupsBuffer = 2,
    Set0Binding_YSpanSetupsBuffer = 3,
    Set0Binding_ColorTileBuffer = 4,
    Set0Binding_DepthTileBuffer = 5,
    Set0Binding_AttrTileBuffer = 6,
    Set0Binding_ResultBuffer = 7,
    Set0Binding_BinResultBuffer = 8,
    Set0Binding_WorkDescBuffer = 9,
    Set0Binding_SetupIndices = 10,
    Set0Binding_FinalFB = 11,
    Set0Binding_StructuredInput = 12,
    Set0Binding_PresentationOutput = 13,
    Set0Binding_Count = 14,
};

// Descriptor set 1 bindings (sampled images).
enum Set1Binding : uint32_t
{
    Set1Binding_CurrentTexture = 0,
    Set1Binding_Capture128Texture = 1,
    Set1Binding_Capture256Texture = 2,
    Set1Binding_ClearBitmapColor = 3,
    Set1Binding_ClearBitmapDepth = 4,
    Set1Binding_Count = 5,
};

// Specialization constant ids declared by Common.glsl.
enum SpecConstantId : uint32_t
{
    SpecConstantId_ScreenWidth = 0,
    SpecConstantId_ScreenHeight = 1,
    SpecConstantId_MaxWorkTiles = 2,
};

// Push constant block, 32 bytes, VK_SHADER_STAGE_COMPUTE_BIT.
struct PushConstants
{
    uint32_t CurVariant;
    uint32_t TexWidth;
    uint32_t TexHeight;
    uint32_t TexWrapS;
    uint32_t TexWrapT;
    uint32_t TexIsCapture;
    int32_t  CaptureYOffset;
    uint32_t Pad;
};
static_assert(sizeof(PushConstants) == 32, "push constant block must stay 32 bytes");

enum Pipeline : uint32_t
{
    Pipeline_InterpSpansZ = 0,
    Pipeline_InterpSpansW = 1,
    Pipeline_BinCombined = 2,
    Pipeline_DepthBlendZ = 3,
    Pipeline_DepthBlendW = 4,
    Pipeline_RasteriseNoTextureZ = 5,
    Pipeline_RasteriseNoTextureW = 6,
    Pipeline_RasteriseNoTextureToonZ = 7,
    Pipeline_RasteriseNoTextureToonW = 8,
    Pipeline_RasteriseNoTextureHighlightZ = 9,
    Pipeline_RasteriseNoTextureHighlightW = 10,
    Pipeline_RasteriseUseTextureDecalZ = 11,
    Pipeline_RasteriseUseTextureDecalW = 12,
    Pipeline_RasteriseUseTextureModulateZ = 13,
    Pipeline_RasteriseUseTextureModulateW = 14,
    Pipeline_RasteriseUseTextureToonZ = 15,
    Pipeline_RasteriseUseTextureToonW = 16,
    Pipeline_RasteriseUseTextureHighlightZ = 17,
    Pipeline_RasteriseUseTextureHighlightW = 18,
    Pipeline_RasteriseShadowMaskZ = 19,
    Pipeline_RasteriseShadowMaskW = 20,
    Pipeline_ClearCoarseBinMask = 21,
    Pipeline_ClearIndirectWorkCount = 22,
    Pipeline_CalculateWorkOffsets = 23,
    Pipeline_SortWork = 24,
    Pipeline_FinalPass0 = 25,
    Pipeline_FinalPass1 = 26,
    Pipeline_FinalPass2 = 27,
    Pipeline_FinalPass3 = 28,
    Pipeline_FinalPass4 = 29,
    Pipeline_FinalPass5 = 30,
    Pipeline_FinalPass6 = 31,
    Pipeline_FinalPass7 = 32,
    Pipeline_Resolve = 33,
    Pipeline_Compositor = 34,
    Pipeline_Count = 35,
};

// Tile geometry bucket. Mirrors `range` in
// ComputeRenderer3D::SetRenderSettings(): the four constants below must be
// compile-time literals, so one module set exists per bucket.
struct TileGeometry
{
    uint32_t TileSize;
    uint32_t CoarseTileCountY;
    uint32_t CoarseTileArea;
    uint32_t ClearCoarseBinMaskLocalSize;
};

inline constexpr uint32_t TileGeometryBucketCount = 3;
inline constexpr TileGeometry TileGeometryBuckets[TileGeometryBucketCount] =
{
    { 8, 4, 32, 64 },
    { 16, 4, 32, 64 },
    { 32, 6, 48, 48 },
};

inline constexpr uint32_t TileGeometryBucketForScale(uint32_t scale)
{
    return (scale >= 5 ? 1u : 0u) + (scale >= 9 ? 1u : 0u);
}

struct ShaderModule
{
    const uint32_t* Words;
    uint32_t WordCount;
};

// [tile geometry bucket][pipeline]. Defined in
// generated/GPU3D_Vulkan_ShaderModules.cpp; identical modules are shared, so
// two entries may point at the same words.
extern const ShaderModule Modules[TileGeometryBucketCount][Pipeline_Count];
extern const char* const PipelineNames[Pipeline_Count];

} // namespace VulkanShaders
} // namespace melonDS

#endif // GPU3D_VULKAN_SHADERMODULES_H
