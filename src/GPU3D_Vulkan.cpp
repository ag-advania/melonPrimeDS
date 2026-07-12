#include "GPU3D_Vulkan.h"

namespace melonDS::Vulkan
{

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

} // namespace melonDS::Vulkan
