#include "GPU3D_Vulkan.h"

#include <algorithm>
#include <cmath>

#include <cstring>
#include <limits>

#include "GPU3D.h"
#include "GPU3D_Texcache.h"

namespace melonDS::Vulkan
{
namespace
{

void SetFailure(std::string* failureReason, const char* value)
{
    if (failureReason)
        *failureReason = value;
}

std::uint32_t PackU16Pair(std::uint32_t low, std::uint32_t high) noexcept
{
    return (low & 0xFFFFu) | ((high & 0xFFFFu) << 16);
}

std::uint32_t BuildPolygonFlags(const Polygon& polygon, std::uint32_t textureLayer) noexcept
{
    std::uint32_t flags = VulkanRasterPolygonFlag_None;
    if (polygon.WBuffer)
        flags |= VulkanRasterPolygonFlag_WBuffer;
    if (polygon.FacingView)
        flags |= VulkanRasterPolygonFlag_FacingView;
    if (polygon.Translucent)
        flags |= VulkanRasterPolygonFlag_Translucent;
    if (polygon.IsShadowMask)
        flags |= VulkanRasterPolygonFlag_ShadowMask;
    if (polygon.IsShadow)
        flags |= VulkanRasterPolygonFlag_Shadow;
    if (polygon.Type == 1)
        flags |= VulkanRasterPolygonFlag_Line;
    if (((polygon.TexParam >> 26) & 0x7u) != 0 && textureLayer != 0xFFFFFFFFu)
        flags |= VulkanRasterPolygonFlag_Textured;
    return flags;
}

bool AppendIndex(std::vector<std::uint16_t>& indices, std::size_t value, std::string* failureReason)
{
    if (value > std::numeric_limits<std::uint16_t>::max())
    {
        SetFailure(failureReason, "Vulkan raster upload exceeded 16-bit index range");
        return false;
    }
    indices.push_back(static_cast<std::uint16_t>(value));
    return true;
}

template <typename T>
bool ByteEqual(const T& left, const T& right) noexcept
{
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

VulkanRasterRenderMode RenderModeFor(const VulkanPackedPolygon& polygon) noexcept
{
    if (polygon.Flags & VulkanRasterPolygonFlag_ShadowMask)
        return VulkanRasterRenderMode::ShadowMask;
    if (polygon.Flags & VulkanRasterPolygonFlag_Shadow)
        return VulkanRasterRenderMode::Shadow;
    if (polygon.Flags & VulkanRasterPolygonFlag_Translucent)
        return VulkanRasterRenderMode::Translucent;
    return VulkanRasterRenderMode::Opaque;
}

VulkanRasterSamplerAxisMode SamplerModeFor(
    std::uint32_t repeatBits,
    std::uint32_t repeatBit,
    std::uint32_t mirrorBit) noexcept
{
    if ((repeatBits & repeatBit) == 0)
        return VulkanRasterSamplerAxisMode::Clamp;
    return (repeatBits & mirrorBit) != 0
        ? VulkanRasterSamplerAxisMode::Mirror
        : VulkanRasterSamplerAxisMode::Repeat;
}

bool PolygonRangesValid(
    const VulkanPackedPolygon& polygon,
    const VulkanRasterUpload& upload) noexcept
{
    const std::uint64_t vertexEnd =
        static_cast<std::uint64_t>(polygon.VertexOffset) + polygon.VertexCount;
    const std::uint64_t indexEnd =
        static_cast<std::uint64_t>(polygon.IndexOffset) + polygon.IndexCount;
    const std::uint64_t edgeEnd =
        static_cast<std::uint64_t>(polygon.EdgeIndexOffset) + polygon.EdgeIndexCount;
    return vertexEnd <= upload.Vertices.size() &&
        indexEnd <= upload.Indices.size() &&
        edgeEnd <= upload.EdgeIndices.size();
}

} // namespace

RasterTargetContract BuildRasterTargetContract(
    int scaleFactor,
    VkFormat colorFormat,
    VkFormat depthStencilFormat) noexcept
{
    RasterTargetContract contract;
    if (scaleFactor < 1 || scaleFactor > 16 ||
        colorFormat == VK_FORMAT_UNDEFINED ||
        depthStencilFormat == VK_FORMAT_UNDEFINED)
    {
        return contract;
    }

    contract.Extent = {
        static_cast<std::uint32_t>(256 * scaleFactor),
        static_cast<std::uint32_t>(192 * scaleFactor)};
    contract.ColorFormat = colorFormat;
    contract.DepthStencilFormat = depthStencilFormat;
    contract.ColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    contract.AttributeUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    contract.DepthStencilUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    contract.ScaleFactor = static_cast<std::uint32_t>(scaleFactor);
    contract.Valid = true;
    return contract;
}

ClearPlaneState DecodeClearPlaneState(
    std::uint32_t renderClearAttr1,
    std::uint32_t renderClearAttr2) noexcept
{
    ClearPlaneState state;
    state.RawAttr1 = renderClearAttr1;
    state.RawAttr2 = renderClearAttr2;
    state.Color5[0] = static_cast<std::uint8_t>(renderClearAttr1 & 0x1Fu);
    state.Color5[1] = static_cast<std::uint8_t>((renderClearAttr1 >> 5) & 0x1Fu);
    state.Color5[2] = static_cast<std::uint8_t>((renderClearAttr1 >> 10) & 0x1Fu);
    state.Fog = ((renderClearAttr1 >> 15) & 0x1u) != 0;
    state.Color5[3] = static_cast<std::uint8_t>((renderClearAttr1 >> 16) & 0x1Fu);
    state.OpaquePolyId = static_cast<std::uint8_t>((renderClearAttr1 >> 24) & 0x3Fu);
    state.Depth24 = ((renderClearAttr2 & 0x7FFFu) * 0x200u) + 0x1FFu;

    for (std::size_t index = 0; index < state.Color.size(); ++index)
        state.Color[index] = static_cast<float>(state.Color5[index]) / 31.0f;
    state.Attributes = {
        static_cast<float>(state.OpaquePolyId) / 63.0f,
        0.0f,
        state.Fog ? 1.0f : 0.0f,
        1.0f};

    // OpenGL's clear shader emits NDC z = depth / 2^23 - 1 and OpenGL then
    // maps NDC [-1, 1] to depth [0, 1]. Vulkan uses [0, 1] directly.
    state.Depth = static_cast<float>(state.Depth24) / 16777216.0f;
    return state;
}

std::array<VkClearValue, 3> BuildClearPlaneAttachmentValues(
    const ClearPlaneState& state) noexcept
{
    std::array<VkClearValue, 3> values{};
    for (std::size_t index = 0; index < state.Color.size(); ++index)
    {
        values[0].color.float32[index] = state.Color[index];
        values[1].color.float32[index] = state.Attributes[index];
    }
    values[2].depthStencil.depth = state.Depth;
    values[2].depthStencil.stencil = state.Stencil;
    return values;
}

ClearBitmapState DecodeClearBitmapState(
    std::uint32_t renderDispCnt,
    std::uint32_t renderClearAttr1,
    std::uint32_t renderClearAttr2) noexcept
{
    ClearBitmapState state;
    state.RenderDispCnt = renderDispCnt;
    state.RawAttr1 = renderClearAttr1;
    state.RawAttr2 = renderClearAttr2;
    state.Enabled = (renderDispCnt & (1u << 14)) != 0;
    state.OffsetX = static_cast<std::uint8_t>((renderClearAttr2 >> 16) & 0xFFu);
    state.OffsetY = static_cast<std::uint8_t>((renderClearAttr2 >> 24) & 0xFFu);
    state.OpaquePolyId = static_cast<std::uint8_t>((renderClearAttr1 >> 24) & 0x3Fu);
    state.Offset = {
        static_cast<float>(state.OffsetX) / 256.0f,
        static_cast<float>(state.OffsetY) / 256.0f};
    state.PushConstants.Offset = state.Offset;
    state.PushConstants.OpaquePolyId = state.OpaquePolyId;
    return state;
}

std::array<std::uint8_t, 4> DecodeClearBitmapColorTexel(
    std::uint16_t color) noexcept
{
    std::uint32_t red = (static_cast<std::uint32_t>(color) << 1) & 0x3Eu;
    std::uint32_t green = (static_cast<std::uint32_t>(color) >> 4) & 0x3Eu;
    std::uint32_t blue = (static_cast<std::uint32_t>(color) >> 9) & 0x3Eu;
    if (red)
        ++red;
    if (green)
        ++green;
    if (blue)
        ++blue;
    const std::uint32_t alpha = (color & 0x8000u) ? 31u : 0u;
    return {{
        static_cast<std::uint8_t>(red),
        static_cast<std::uint8_t>(green),
        static_cast<std::uint8_t>(blue),
        static_cast<std::uint8_t>(alpha)}};
}

std::uint32_t DecodeClearBitmapDepthTexel(std::uint16_t value) noexcept
{
    const std::uint32_t depth =
        ((static_cast<std::uint32_t>(value) & 0x7FFFu) * 0x200u) + 0x1FFu;
    const std::uint32_t fog =
        (static_cast<std::uint32_t>(value) & 0x8000u) << 9;
    return depth | fog;
}

void ClearBitmapDirtyTracker::Reset() noexcept
{
    DirtyMask = ClearBitmapDirty_All;
}

void ClearBitmapDirtyTracker::MarkDirty(std::uint32_t mask) noexcept
{
    DirtyMask |= mask & ClearBitmapDirty_All;
}

std::uint32_t ClearBitmapDirtyTracker::PendingMask() const noexcept
{
    return DirtyMask;
}

std::uint32_t ClearBitmapDirtyTracker::ConsumeIfEnabled(bool enabled) noexcept
{
    if (!enabled)
        return ClearBitmapDirty_None;
    const std::uint32_t consumed = DirtyMask;
    DirtyMask = ClearBitmapDirty_None;
    return consumed;
}

void VulkanRasterUpload::Clear() noexcept
{
    Vertices.clear();
    Indices.clear();
    EdgeIndices.clear();
    Polygons.clear();
    SourcePolygonCount = 0;
    SkippedDegenerateCount = 0;
    Valid = false;
}

VulkanPackedVertex PackVulkanRasterVertex(
    const Polygon& polygon,
    std::uint32_t vertexIndex,
    int scaleFactor,
    std::uint32_t textureLayer) noexcept
{
    VulkanPackedVertex packed;
    if (vertexIndex >= polygon.NumVertices || !polygon.Vertices[vertexIndex])
        return packed;

    const Vertex& vertex = *polygon.Vertices[vertexIndex];
    std::uint32_t z = static_cast<std::uint32_t>(polygon.FinalZ[vertexIndex]);
    const std::uint32_t w = static_cast<std::uint32_t>(polygon.FinalW[vertexIndex]);
    const std::uint32_t alpha = (polygon.Attr >> 16) & 0x1Fu;

    std::uint32_t zShift = 0;
    while (z > 0xFFFFu)
    {
        z >>= 1;
        ++zShift;
    }

    std::int32_t x = vertex.FinalPosition[0];
    std::int32_t y = vertex.FinalPosition[1];
    if (scaleFactor > 1)
    {
        x = (vertex.HiresPosition[0] * scaleFactor) >> 4;
        y = (vertex.HiresPosition[1] * scaleFactor) >> 4;
    }

    packed.PositionXY = PackU16Pair(
        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    packed.DepthZW = PackU16Pair(z, w);
    packed.ColorRgba =
        (static_cast<std::uint32_t>(vertex.FinalColor[0] >> 1) & 0xFFu) |
        ((static_cast<std::uint32_t>(vertex.FinalColor[1] >> 1) & 0xFFu) << 8) |
        ((static_cast<std::uint32_t>(vertex.FinalColor[2] >> 1) & 0xFFu) << 16) |
        (alpha << 24);
    packed.TexcoordST = PackU16Pair(
        static_cast<std::uint16_t>(vertex.TexCoords[0]),
        static_cast<std::uint16_t>(vertex.TexCoords[1]));

    std::uint32_t vertexFlags = polygon.Attr & 0x1F00C8F0u;
    if (polygon.FacingView)
        vertexFlags |= 1u << 8;
    if (polygon.WBuffer)
        vertexFlags |= 1u << 9;
    packed.PolygonFlags = vertexFlags | (zShift << 16);
    packed.TextureLayer = textureLayer;
    packed.TextureSize = PackU16Pair(
        TextureWidth(polygon.TexParam), TextureHeight(polygon.TexParam));
    return packed;
}

bool BuildVulkanRasterUpload(
    const Polygon* const* polygons,
    std::size_t polygonCount,
    const VulkanRasterBuildOptions& options,
    VulkanRasterUpload& upload,
    std::string* failureReason)
{
    upload.Clear();
    if (failureReason)
        failureReason->clear();

    if ((!polygons && polygonCount != 0) || options.ScaleFactor < 1 || options.ScaleFactor > 16)
    {
        SetFailure(failureReason, "invalid Vulkan raster upload input");
        return false;
    }

    upload.SourcePolygonCount = static_cast<std::uint32_t>(polygonCount);
    for (std::size_t sourceOrder = 0; sourceOrder < polygonCount; ++sourceOrder)
    {
        const Polygon* polygon = polygons[sourceOrder];
        if (!polygon || polygon->Degenerate)
        {
            ++upload.SkippedDegenerateCount;
            continue;
        }

        if (polygon->NumVertices > 10 ||
            (polygon->Type == 1 && polygon->NumVertices < 2) ||
            (polygon->Type != 1 && polygon->NumVertices < 3))
        {
            SetFailure(failureReason, "invalid polygon vertex count for Vulkan upload");
            upload.Clear();
            return false;
        }

        const std::uint32_t textureLayer =
            options.TextureLayers && sourceOrder < options.TextureLayerCount
                ? options.TextureLayers[sourceOrder]
                : 0xFFFFFFFFu;

        VulkanPackedPolygon record;
        record.SourceOrder = static_cast<std::uint32_t>(sourceOrder);
        record.Primitive = static_cast<std::uint32_t>(
            polygon->Type == 1 ? VulkanRasterPrimitive::Lines : VulkanRasterPrimitive::Triangles);
        record.VertexOffset = static_cast<std::uint32_t>(upload.Vertices.size());
        record.IndexOffset = static_cast<std::uint32_t>(upload.Indices.size());
        record.EdgeIndexOffset = static_cast<std::uint32_t>(upload.EdgeIndices.size());
        record.Attr = polygon->Attr;
        record.TexParam = polygon->TexParam;
        record.TexPalette = polygon->TexPalette;
        record.TextureLayer = textureLayer;
        record.TextureRepeat = (polygon->TexParam >> 16) & 0xFu;
        record.Flags = BuildPolygonFlags(*polygon, textureLayer);

        if (polygon->Type == 1)
        {
            std::uint32_t selected[2]{};
            std::uint32_t selectedCount = 0;
            std::int32_t lastX = 0;
            std::int32_t lastY = 0;
            bool haveLast = false;
            for (std::uint32_t vertexIndex = 0;
                 vertexIndex < polygon->NumVertices && selectedCount < 2;
                 ++vertexIndex)
            {
                const Vertex* vertex = polygon->Vertices[vertexIndex];
                if (!vertex)
                {
                    SetFailure(failureReason, "line polygon contains a null vertex");
                    upload.Clear();
                    return false;
                }
                if (haveLast && lastX == vertex->FinalPosition[0] &&
                    lastY == vertex->FinalPosition[1])
                {
                    continue;
                }
                selected[selectedCount++] = vertexIndex;
                lastX = vertex->FinalPosition[0];
                lastY = vertex->FinalPosition[1];
                haveLast = true;
            }
            if (selectedCount != 2)
            {
                SetFailure(failureReason, "line polygon has fewer than two distinct vertices");
                upload.Clear();
                return false;
            }

            for (std::uint32_t index = 0; index < selectedCount; ++index)
            {
                upload.Vertices.push_back(PackVulkanRasterVertex(
                    *polygon, selected[index], options.ScaleFactor, textureLayer));
            }
            if (!AppendIndex(upload.Indices, record.VertexOffset, failureReason) ||
                !AppendIndex(upload.Indices, record.VertexOffset + 1u, failureReason) ||
                !AppendIndex(upload.EdgeIndices, record.VertexOffset, failureReason) ||
                !AppendIndex(upload.EdgeIndices, record.VertexOffset + 1u, failureReason))
            {
                upload.Clear();
                return false;
            }
        }
        else
        {
            for (std::uint32_t vertexIndex = 0; vertexIndex < polygon->NumVertices; ++vertexIndex)
            {
                if (!polygon->Vertices[vertexIndex])
                {
                    SetFailure(failureReason, "polygon contains a null vertex");
                    upload.Clear();
                    return false;
                }
                upload.Vertices.push_back(PackVulkanRasterVertex(
                    *polygon, vertexIndex, options.ScaleFactor, textureLayer));
                if (vertexIndex >= 2)
                {
                    if (!AppendIndex(upload.Indices, record.VertexOffset, failureReason) ||
                        !AppendIndex(upload.Indices,
                            record.VertexOffset + vertexIndex - 1u, failureReason) ||
                        !AppendIndex(upload.Indices,
                            record.VertexOffset + vertexIndex, failureReason))
                    {
                        upload.Clear();
                        return false;
                    }
                }
            }

            for (std::uint32_t vertexIndex = 0; vertexIndex < polygon->NumVertices; ++vertexIndex)
            {
                const std::uint32_t next = (vertexIndex + 1u) % polygon->NumVertices;
                if (!AppendIndex(upload.EdgeIndices,
                        record.VertexOffset + vertexIndex, failureReason) ||
                    !AppendIndex(upload.EdgeIndices,
                        record.VertexOffset + next, failureReason))
                {
                    upload.Clear();
                    return false;
                }
            }
        }

        record.VertexCount = static_cast<std::uint32_t>(upload.Vertices.size()) - record.VertexOffset;
        record.IndexCount = static_cast<std::uint32_t>(upload.Indices.size()) - record.IndexOffset;
        record.EdgeIndexCount =
            static_cast<std::uint32_t>(upload.EdgeIndices.size()) - record.EdgeIndexOffset;
        upload.Polygons.push_back(record);
    }

    upload.Valid = true;
    return true;
}

void VulkanRasterBatchPlan::Clear() noexcept
{
    Batches.clear();
    SourcePolygonCount = 0;
    SourceOrderPreserved = false;
    AdjacentOnly = false;
    Valid = false;
}

VulkanRasterPipelineKey BuildVulkanRasterPipelineKey(
    const VulkanPackedPolygon& polygon,
    const VulkanRasterBatchOptions& options) noexcept
{
    VulkanRasterPipelineKey key;
    const VulkanRasterRenderMode renderMode = RenderModeFor(polygon);
    const bool textured = (polygon.Flags & VulkanRasterPolygonFlag_Textured) != 0;
    const std::uint32_t textureMode = (polygon.Attr >> 4) & 0x3u;
    const std::uint32_t alpha = (polygon.Attr >> 16) & 0x1Fu;

    key.Primitive = polygon.Primitive;
    key.RenderMode = static_cast<std::uint32_t>(renderMode);
    key.TextureMode = textured
        ? textureMode
        : static_cast<std::uint32_t>(VulkanRasterTextureMode::None);
    if (textureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::ToonHighlight))
    {
        key.ToonMode = static_cast<std::uint32_t>(
            (options.RenderDispCnt & (1u << 1)) != 0
                ? VulkanRasterToonMode::Highlight
                : VulkanRasterToonMode::Toon);
    }
    key.WBuffer = (polygon.Flags & VulkanRasterPolygonFlag_WBuffer) != 0;
    key.DepthEqual = (polygon.Attr >> 14) & 0x1u;
    key.DepthWrite = renderMode == VulkanRasterRenderMode::Opaque
        ? 1u
        : (renderMode == VulkanRasterRenderMode::ShadowMask
            ? 0u
            : ((polygon.Attr >> 11) & 0x1u));
    if (renderMode == VulkanRasterRenderMode::Translucent ||
        renderMode == VulkanRasterRenderMode::Shadow)
    {
        key.FogAttrWrite =
            ((options.RenderDispCnt & (1u << 7)) != 0 &&
             (polygon.Attr & (1u << 15)) == 0) ? 1u : 0u;
    }
    else
    {
        key.FogAttrWrite = (polygon.Attr >> 15) & 0x1u;
    }
    key.ShadowStage = renderMode == VulkanRasterRenderMode::ShadowMask
        ? 1u
        : (renderMode == VulkanRasterRenderMode::Shadow ? 2u : 0u);
    key.NeedsOpaquePass =
        (renderMode == VulkanRasterRenderMode::Translucent ||
         renderMode == VulkanRasterRenderMode::Shadow) && alpha == 0x1Fu;
    key.AlphaZero = alpha == 0;
    key.Textured = textured ? 1u : 0u;
    key.ColorFormat = static_cast<std::uint32_t>(options.ColorFormat);
    key.AttributeFormat = static_cast<std::uint32_t>(options.AttributeFormat);
    key.DepthStencilFormat = static_cast<std::uint32_t>(options.DepthStencilFormat);
    key.StencilReference = (polygon.Attr >> 24) & 0x3Fu;
    return key;
}

VulkanRasterTextureKey BuildVulkanRasterTextureKey(
    const VulkanPackedPolygon& polygon) noexcept
{
    VulkanRasterTextureKey key;
    const bool textured = (polygon.Flags & VulkanRasterPolygonFlag_Textured) != 0;
    key.Enabled = textured ? 1u : 0u;
    if (!textured)
        return key;

    key.NormalizedTexParam = polygon.TexParam & ~0xC00F0000u;
    key.TexPalette = polygon.TexPalette;
    key.TextureLayer = polygon.TextureLayer;
    key.TextureRepeat = polygon.TextureRepeat & 0xFu;
    key.SamplerS = static_cast<std::uint32_t>(SamplerModeFor(
        key.TextureRepeat, 1u << 0, 1u << 2));
    key.SamplerT = static_cast<std::uint32_t>(SamplerModeFor(
        key.TextureRepeat, 1u << 1, 1u << 3));
    key.TextureFormat = (polygon.TexParam >> 26) & 0x7u;
    return key;
}

bool BuildVulkanRasterBatchPlan(
    const VulkanRasterUpload& upload,
    const VulkanRasterBatchOptions& options,
    VulkanRasterBatchPlan& plan,
    std::string* failureReason)
{
    plan.Clear();
    if (failureReason)
        failureReason->clear();

    if (!upload.Valid ||
        options.ColorFormat == VK_FORMAT_UNDEFINED ||
        options.AttributeFormat == VK_FORMAT_UNDEFINED ||
        options.DepthStencilFormat == VK_FORMAT_UNDEFINED)
    {
        SetFailure(failureReason, "invalid Vulkan raster batch input");
        return false;
    }

    plan.SourcePolygonCount = upload.SourcePolygonCount;
    if (upload.Polygons.empty())
    {
        plan.SourceOrderPreserved = true;
        plan.AdjacentOnly = true;
        plan.Valid = true;
        return true;
    }

    std::uint32_t previousSourceOrder = 0;
    bool havePrevious = false;
    for (std::size_t polygonIndex = 0; polygonIndex < upload.Polygons.size(); ++polygonIndex)
    {
        const VulkanPackedPolygon& polygon = upload.Polygons[polygonIndex];
        if (!PolygonRangesValid(polygon, upload))
        {
            SetFailure(failureReason, "Vulkan raster polygon range exceeds uploaded payload");
            plan.Clear();
            return false;
        }
        if (havePrevious && polygon.SourceOrder <= previousSourceOrder)
        {
            SetFailure(failureReason, "Vulkan raster polygon source order is not strictly increasing");
            plan.Clear();
            return false;
        }

        const VulkanRasterPipelineKey pipelineKey =
            BuildVulkanRasterPipelineKey(polygon, options);
        const VulkanRasterTextureKey textureKey =
            BuildVulkanRasterTextureKey(polygon);

        bool extend = false;
        if (!plan.Batches.empty() && havePrevious)
        {
            VulkanRasterBatch& batch = plan.Batches.back();
            extend = polygon.SourceOrder == previousSourceOrder + 1u &&
                polygon.IndexOffset == batch.IndexOffset + batch.IndexCount &&
                polygon.EdgeIndexOffset == batch.EdgeIndexOffset + batch.EdgeIndexCount &&
                ByteEqual(batch.PipelineKey, pipelineKey) &&
                ByteEqual(batch.TextureKey, textureKey);
            if (extend)
            {
                ++batch.PolygonCount;
                batch.LastSourceOrder = polygon.SourceOrder;
                batch.IndexCount += polygon.IndexCount;
                batch.EdgeIndexCount += polygon.EdgeIndexCount;
            }
        }

        if (!extend)
        {
            VulkanRasterBatch batch;
            batch.FirstPolygon = static_cast<std::uint32_t>(polygonIndex);
            batch.PolygonCount = 1;
            batch.FirstSourceOrder = polygon.SourceOrder;
            batch.LastSourceOrder = polygon.SourceOrder;
            batch.IndexOffset = polygon.IndexOffset;
            batch.IndexCount = polygon.IndexCount;
            batch.EdgeIndexOffset = polygon.EdgeIndexOffset;
            batch.EdgeIndexCount = polygon.EdgeIndexCount;
            batch.PipelineKey = pipelineKey;
            batch.TextureKey = textureKey;
            plan.Batches.push_back(batch);
        }

        previousSourceOrder = polygon.SourceOrder;
        havePrevious = true;
    }

    plan.SourceOrderPreserved = true;
    plan.AdjacentOnly = true;
    plan.Valid = true;
    return true;
}

VulkanOpaquePipelineState BuildVulkanOpaquePipelineState(
    const VulkanRasterPipelineKey& key) noexcept
{
    VulkanOpaquePipelineState state;
    state.Topology = key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles)
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.DepthCompare = key.DepthEqual != 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    state.DepthWrite = key.DepthWrite != 0 ? VK_TRUE : VK_FALSE;
    state.StencilReplace = VK_TRUE;
    state.StencilReference = key.StencilReference & 0x3Fu;
    state.ColorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    state.AttributeWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    state.WBuffer = key.WBuffer != 0;
    state.AlphaTest = true;

    state.Valid =
        key.RenderMode == static_cast<std::uint32_t>(VulkanRasterRenderMode::Opaque) &&
        key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles) &&
        key.Textured == 0 &&
        key.TextureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::None) &&
        key.ColorFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED) &&
        key.AttributeFormat == static_cast<std::uint32_t>(VK_FORMAT_R8G8B8A8_UNORM) &&
        key.DepthStencilFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED);
    return state;
}


VulkanTranslucentPipelineState BuildVulkanTranslucentPipelineState(
    const VulkanRasterPipelineKey& key) noexcept
{
    VulkanTranslucentPipelineState state;
    state.Topology = key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles)
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.DepthCompare = key.DepthEqual != 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    state.DepthWrite = key.DepthWrite != 0 ? VK_TRUE : VK_FALSE;
    state.StencilReference = 0x40u | (key.StencilReference & 0x3Fu);
    state.ColorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    state.AttributeWriteMask = key.FogAttrWrite != 0
        ? VK_COLOR_COMPONENT_B_BIT
        : static_cast<VkColorComponentFlags>(0);
    state.WBuffer = key.WBuffer != 0;
    state.NeedsOpaquePass = key.NeedsOpaquePass != 0;

    state.Valid =
        key.RenderMode == static_cast<std::uint32_t>(VulkanRasterRenderMode::Translucent) &&
        key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles) &&
        key.Textured == 0 &&
        key.TextureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::None) &&
        key.ColorFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED) &&
        key.AttributeFormat == static_cast<std::uint32_t>(VK_FORMAT_R8G8B8A8_UNORM) &&
        key.DepthStencilFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED);
    return state;
}

VulkanShadowMaskPipelineState BuildVulkanShadowMaskPipelineState(
    const VulkanRasterPipelineKey& key) noexcept
{
    VulkanShadowMaskPipelineState state;
    state.Topology = key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles)
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.DepthCompare = key.DepthEqual != 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    state.WBuffer = key.WBuffer != 0;
    state.Valid =
        key.RenderMode == static_cast<std::uint32_t>(VulkanRasterRenderMode::ShadowMask) &&
        key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles) &&
        key.Textured == 0 &&
        key.TextureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::None) &&
        key.ColorFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED) &&
        key.AttributeFormat == static_cast<std::uint32_t>(VK_FORMAT_R8G8B8A8_UNORM) &&
        key.DepthStencilFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED);
    return state;
}

VulkanShadowRejectPipelineState BuildVulkanShadowRejectPipelineState(
    const VulkanRasterPipelineKey& key) noexcept
{
    VulkanShadowRejectPipelineState state;
    state.Topology = key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles)
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.DepthCompare = key.DepthEqual != 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    state.StencilReference = key.StencilReference & 0x3Fu;
    state.WBuffer = key.WBuffer != 0;
    state.Valid =
        key.RenderMode == static_cast<std::uint32_t>(VulkanRasterRenderMode::Shadow) &&
        key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles) &&
        key.Textured == 0 &&
        key.TextureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::None) &&
        key.ColorFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED) &&
        key.AttributeFormat == static_cast<std::uint32_t>(VK_FORMAT_R8G8B8A8_UNORM) &&
        key.DepthStencilFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED);
    return state;
}

VulkanShadowBlendPipelineState BuildVulkanShadowBlendPipelineState(
    const VulkanRasterPipelineKey& key) noexcept
{
    VulkanShadowBlendPipelineState state;
    state.Topology = key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles)
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.DepthCompare = key.DepthEqual != 0 ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    state.DepthWrite = key.DepthWrite != 0 ? VK_TRUE : VK_FALSE;
    state.StencilReference = 0x40u | (key.StencilReference & 0x3Fu);
    state.ColorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    state.AttributeWriteMask = key.FogAttrWrite != 0
        ? VK_COLOR_COMPONENT_B_BIT
        : static_cast<VkColorComponentFlags>(0);
    state.WBuffer = key.WBuffer != 0;
    state.Valid =
        key.RenderMode == static_cast<std::uint32_t>(VulkanRasterRenderMode::Shadow) &&
        key.Primitive == static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles) &&
        key.Textured == 0 &&
        key.TextureMode == static_cast<std::uint32_t>(VulkanRasterTextureMode::None) &&
        key.ColorFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED) &&
        key.AttributeFormat == static_cast<std::uint32_t>(VK_FORMAT_R8G8B8A8_UNORM) &&
        key.DepthStencilFormat != static_cast<std::uint32_t>(VK_FORMAT_UNDEFINED);
    return state;
}

VulkanToonHighlightConfig BuildVulkanToonHighlightConfig(
    const std::array<std::uint16_t, 32>& toonTable,
    std::uint32_t dispCnt,
    VulkanToonHighlightMode mode,
    bool textured) noexcept
{
    VulkanToonHighlightConfig config;
    config.DispCnt = dispCnt;
    config.Mode = static_cast<std::uint32_t>(mode);
    config.Textured = textured ? 1u : 0u;
    for (std::size_t i = 0; i < toonTable.size(); ++i)
    {
        const std::uint16_t color = toonTable[i];
        config.ToonColors[i] = {{
            static_cast<float>(color & 0x1Fu) / 31.0f,
            static_cast<float>((color >> 5) & 0x1Fu) / 31.0f,
            static_cast<float>((color >> 10) & 0x1Fu) / 31.0f,
            1.0f}};
    }
    return config;
}

std::array<float, 4> EvaluateVulkanToonHighlightReference(
    const VulkanToonHighlightConfig& config,
    const std::array<float, 4>& vertexColor,
    const std::array<float, 4>& textureColor) noexcept
{
    auto clamp01 = [](float value) noexcept {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    };
    const std::size_t toonIndex = static_cast<std::size_t>(
        clamp01(vertexColor[0]) * 31.0f);
    std::array<float, 4> v = vertexColor;
    const auto& toon = config.ToonColors[toonIndex];
    const auto mode = static_cast<VulkanToonHighlightMode>(config.Mode);
    if (mode == VulkanToonHighlightMode::Toon)
        v[0] = toon[0], v[1] = toon[1], v[2] = toon[2];
    else if (mode == VulkanToonHighlightMode::Highlight)
        v[1] = v[0], v[2] = v[0];

    std::array<float, 4> result = v;
    if (config.Textured)
    {
        for (std::size_t i = 0; i < 4; ++i)
            result[i] = v[i] * textureColor[i];
    }
    if (mode == VulkanToonHighlightMode::Highlight)
    {
        result[0] = clamp01(result[0] + toon[0]);
        result[1] = clamp01(result[1] + toon[1]);
        result[2] = clamp01(result[2] + toon[2]);
    }
    result[3] = config.Textured ? vertexColor[3] * textureColor[3] : vertexColor[3];
    return result;
}

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept
{
    VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT)
    {
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspects;
}

VulkanToonHighlightShaderAbi DescribeVulkanToonHighlightShaderAbi() noexcept
{
    return {};
}


VulkanTextureSamplingDescriptorContract
DescribeVulkanTextureSamplingDescriptorContract() noexcept
{
    return {};
}

std::array<float, 4> EvaluateVulkanTextureCombiner(
    const VulkanTextureCombinerInput& input) noexcept
{
    if (input.Mode == VulkanTextureCombinerMode::Raw)
        return input.TextureColor;

    std::array<float, 4> working = input.VertexColor;
    if (input.Mode == VulkanTextureCombinerMode::Toon)
    {
        working[0] = input.ToonColor[0];
        working[1] = input.ToonColor[1];
        working[2] = input.ToonColor[2];
    }
    else if (input.Mode == VulkanTextureCombinerMode::Highlight)
    {
        working[1] = working[0];
        working[2] = working[0];
    }

    std::array<float, 4> output{};
    if (input.Mode == VulkanTextureCombinerMode::Decal)
    {
        const float alpha = input.TextureColor[3];
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            output[channel] = input.TextureColor[channel] * alpha +
                working[channel] * (1.0f - alpha);
        }
        output[3] = working[3];
    }
    else
    {
        for (std::size_t channel = 0; channel < 4; ++channel)
            output[channel] = working[channel] * input.TextureColor[channel];
    }

    if (input.Mode == VulkanTextureCombinerMode::Highlight)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
            output[channel] = std::min(output[channel] + input.ToonColor[channel], 1.0f);
    }
    return output;
}

std::array<std::uint8_t, 4> QuantizeVulkanColor8(
    const std::array<float, 4>& color) noexcept
{
    std::array<std::uint8_t, 4> output{};
    for (std::size_t channel = 0; channel < output.size(); ++channel)
    {
        output[channel] = static_cast<std::uint8_t>(std::lround(
            std::clamp(color[channel], 0.0f, 1.0f) * 255.0f));
    }
    return output;
}

VulkanTexturedPolygonDescriptorContract
DescribeVulkanTexturedPolygonDescriptorContract() noexcept
{
    return {};
}

} // namespace melonDS::Vulkan
