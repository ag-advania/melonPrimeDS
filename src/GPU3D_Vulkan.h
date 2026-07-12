#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU3D_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_PLANE_CONTRACT_V1

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kRasterTargetContractVersion = 1;
inline constexpr std::uint32_t kClearPlaneContractVersion = 1;

struct RasterTargetContract
{
    VkExtent2D Extent{};
    VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat AttributeFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags ColorUsage = 0;
    VkImageUsageFlags AttributeUsage = 0;
    VkImageUsageFlags DepthStencilUsage = 0;
    std::uint32_t ScaleFactor = 0;
    bool Valid = false;
};

struct ClearPlaneState
{
    std::uint32_t RawAttr1 = 0;
    std::uint32_t RawAttr2 = 0;
    std::array<std::uint8_t, 4> Color5{{0, 0, 0, 0}};
    std::uint8_t OpaquePolyId = 0;
    bool Fog = false;
    std::uint32_t Depth24 = 0;
    std::uint8_t Stencil = 0xFF;
    std::array<float, 4> Color{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<float, 4> Attributes{{0.0f, 0.0f, 0.0f, 1.0f}};
    float Depth = 0.0f;
};

RasterTargetContract BuildRasterTargetContract(
    int scaleFactor,
    VkFormat colorFormat,
    VkFormat depthStencilFormat) noexcept;

ClearPlaneState DecodeClearPlaneState(
    std::uint32_t renderClearAttr1,
    std::uint32_t renderClearAttr2) noexcept;

std::array<VkClearValue, 3> BuildClearPlaneAttachmentValues(
    const ClearPlaneState& state) noexcept;

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept;

} // namespace melonDS::Vulkan
