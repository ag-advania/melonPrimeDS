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
