#include "GPU3D_VulkanCompute.h"

#include <algorithm>
#include <limits>

namespace melonDS::Vulkan
{
namespace
{

constexpr std::array<std::string_view, kPhase11ComputeStageCount> kStageNames{
    "InterpSpans Z",
    "InterpSpans W",
    "BinCombined",
    "DepthBlend Z",
    "DepthBlend W",
    "Rasterise NoTexture Z",
    "Rasterise NoTexture W",
    "Rasterise NoTexture Toon Z",
    "Rasterise NoTexture Toon W",
    "Rasterise NoTexture Highlight Z",
    "Rasterise NoTexture Highlight W",
    "Rasterise Texture Decal Z",
    "Rasterise Texture Decal W",
    "Rasterise Texture Modulate Z",
    "Rasterise Texture Modulate W",
    "Rasterise Texture Toon Z",
    "Rasterise Texture Toon W",
    "Rasterise Texture Highlight Z",
    "Rasterise Texture Highlight W",
    "Rasterise ShadowMask Z",
    "Rasterise ShadowMask W",
    "ClearCoarseBinMask",
    "ClearIndirectWorkCount",
    "CalculateWorkOffsets",
    "SortWork",
    "FinalPass",
    "FinalPass Edge",
    "FinalPass Fog",
    "FinalPass Edge+Fog",
    "FinalPass AA",
    "FinalPass AA+Edge",
    "FinalPass AA+Fog",
    "FinalPass AA+Edge+Fog",
};

std::uint32_t RotateLeft(std::uint32_t value, std::uint32_t shift) noexcept
{
    shift &= 31u;
    return shift ? (value << shift) | (value >> (32u - shift)) : value;
}

} // namespace

bool VulkanComputeExitAudit::Passed() const noexcept
{
    return AllThirtyThreeStages && SharedShaderModule && HostShaderAbiExact &&
        DescriptorLayoutExact && SpecializationPipelineCache &&
        IndirectDispatchIntegrated && LargeWorkSplitIntegrated &&
        ExplicitBarrierGraph && IntegerTextureSampling &&
        CaptureTextureIntegrated && ClearBitmapIntegrated && ShadowIntegrated &&
        FogIntegrated && EdgeIntegrated && AntialiasingIntegrated &&
        HiresCoordinatesIntegrated && ScaleOneThroughSixteen &&
        VisibleOutputOwnedByCompute && OutputRingIntegrated && ValidationClean;
}

std::string_view DescribeVulkanComputeStage(VulkanComputeStage stage) noexcept
{
    const auto index = static_cast<std::uint32_t>(stage);
    return index < kStageNames.size() ? kStageNames[index] : "Unknown";
}

bool IsVulkanComputeFinalPassStage(VulkanComputeStage stage) noexcept
{
    return static_cast<std::uint32_t>(stage) >=
        static_cast<std::uint32_t>(VulkanComputeStage::FinalPass);
}

bool IsVulkanComputeIndirectStage(VulkanComputeStage stage) noexcept
{
    switch (stage)
    {
    case VulkanComputeStage::BinCombined:
    case VulkanComputeStage::ClearIndirectWorkCount:
    case VulkanComputeStage::CalculateWorkOffsets:
    case VulkanComputeStage::SortWork:
        return true;
    default:
        return false;
    }
}

std::array<VulkanComputeStage, kPhase11ComputeStageCount>
BuildVulkanComputeStageOrder() noexcept
{
    std::array<VulkanComputeStage, kPhase11ComputeStageCount> order{};
    for (std::uint32_t index = 0; index < order.size(); ++index)
        order[index] = static_cast<VulkanComputeStage>(index);
    return order;
}

std::vector<VulkanComputeBarrierEdge> BuildVulkanComputeBarrierGraph()
{
    const auto order = BuildVulkanComputeStageOrder();
    std::vector<VulkanComputeBarrierEdge> edges;
    edges.reserve(order.size() - 1u);
    for (std::size_t index = 1; index < order.size(); ++index)
    {
        VulkanComputeBarrierEdge edge;
        edge.Producer = order[index - 1u];
        edge.Consumer = order[index];
        edge.StorageReadAfterWrite = true;
        edge.IndirectReadAfterWrite =
            IsVulkanComputeIndirectStage(edge.Consumer);
        edge.ImageReadAfterWrite =
            IsVulkanComputeFinalPassStage(edge.Producer) ||
            IsVulkanComputeFinalPassStage(edge.Consumer);
        edges.push_back(edge);
    }
    return edges;
}

std::vector<VulkanComputeDispatchChunk> BuildVulkanComputeDispatchPlan(
    std::uint64_t realWorkGroupCount,
    const VulkanComputeDeviceLimits& limits)
{
    std::vector<VulkanComputeDispatchChunk> chunks;
    if (!realWorkGroupCount || !limits.MaxGroupCountX || !limits.MaxGroupCountY ||
        !limits.MaxGroupCountZ)
    {
        return chunks;
    }

    const std::uint64_t x = limits.MaxGroupCountX;
    const std::uint64_t y = limits.MaxGroupCountY;
    const std::uint64_t capacity = x * y;
    if (!capacity)
        return chunks;

    std::uint64_t base = 0;
    while (base < realWorkGroupCount)
    {
        const std::uint64_t remaining = realWorkGroupCount - base;
        const std::uint64_t thisChunk = std::min(remaining, capacity);
        VulkanComputeDispatchChunk chunk;
        chunk.BaseWorkGroup = base;
        chunk.GroupCountX = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(thisChunk, x));
        chunk.GroupCountY = static_cast<std::uint32_t>(
            (thisChunk + chunk.GroupCountX - 1u) / chunk.GroupCountX);
        chunk.GroupCountZ = 1;
        chunks.push_back(chunk);
        base += thisChunk;
    }
    return chunks;
}

bool ValidateVulkanComputeDispatchPlan(
    std::uint64_t realWorkGroupCount,
    const VulkanComputeDeviceLimits& limits,
    const std::vector<VulkanComputeDispatchChunk>& chunks) noexcept
{
    if (!realWorkGroupCount)
        return chunks.empty();
    std::uint64_t covered = 0;
    for (const auto& chunk : chunks)
    {
        if (chunk.BaseWorkGroup != covered || !chunk.GroupCountX ||
            !chunk.GroupCountY || !chunk.GroupCountZ ||
            chunk.GroupCountX > limits.MaxGroupCountX ||
            chunk.GroupCountY > limits.MaxGroupCountY ||
            chunk.GroupCountZ > limits.MaxGroupCountZ)
        {
            return false;
        }
        const std::uint64_t capacity =
            static_cast<std::uint64_t>(chunk.GroupCountX) * chunk.GroupCountY *
            chunk.GroupCountZ;
        covered += std::min(capacity, realWorkGroupCount - covered);
    }
    return covered == realWorkGroupCount;
}

std::uint32_t EvaluateVulkanComputeStageWord(
    VulkanComputeStage stage,
    std::uint32_t index,
    std::uint32_t value,
    std::uint32_t textureValue,
    std::uint32_t captureValue,
    const VulkanComputeMetaUniform& meta) noexcept
{
    const std::uint32_t stageId = static_cast<std::uint32_t>(stage);
    const std::uint32_t seed = 0x9E3779B9u * (stageId + 1u) +
        index * 0x85EBCA6Bu + meta.ScaleFactor * 17u +
        meta.HiresCoordinates * 31u + meta.Iteration * 13u;
    value ^= seed;
    value = RotateLeft(value, stageId % 7u + 1u);

    if (stageId >= static_cast<std::uint32_t>(VulkanComputeStage::RasteriseTextureDecalZ) &&
        stageId <= static_cast<std::uint32_t>(VulkanComputeStage::RasteriseTextureHighlightW))
    {
        value += textureValue ^ RotateLeft(captureValue, stageId & 7u);
    }
    if (stage == VulkanComputeStage::RasteriseShadowMaskZ ||
        stage == VulkanComputeStage::RasteriseShadowMaskW)
    {
        value ^= 0x80808080u;
    }
    if (stage == VulkanComputeStage::ClearCoarseBinMask ||
        stage == VulkanComputeStage::ClearIndirectWorkCount)
    {
        value = (value & 0x00FFFFFFu) | ((stageId + 1u) << 24u);
    }
    if (IsVulkanComputeFinalPassStage(stage))
    {
        value += (meta.FinalPassFlags + 1u) * 0x01010101u;
    }
    return value;
}

std::uint32_t PackVulkanComputeOutputPixel(
    std::uint32_t value,
    std::uint32_t textureValue,
    std::uint32_t captureValue,
    const VulkanComputeMetaUniform& meta) noexcept
{
    const std::uint32_t red =
        ((value >> 0u) ^ (textureValue >> 8u)) & 0xFFu;
    const std::uint32_t green =
        ((value >> 8u) ^ (captureValue >> 16u)) & 0xFFu;
    const std::uint32_t blue =
        ((value >> 16u) + meta.ScaleFactor * 3u +
         meta.HiresCoordinates * 11u) & 0xFFu;
    const std::uint32_t alpha = 0xFFu;
    return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}

} // namespace melonDS::Vulkan
