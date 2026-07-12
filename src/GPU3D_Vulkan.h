#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU3D_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_PLANE_CONTRACT_V1
// MELONPRIME_VULKAN_CLEAR_BITMAP_CONTRACT_V1

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kRasterTargetContractVersion = 1;
inline constexpr std::uint32_t kClearPlaneContractVersion = 1;
inline constexpr std::uint32_t kClearBitmapContractVersion = 1;

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

enum ClearBitmapDirtyBits : std::uint32_t
{
    ClearBitmapDirty_None = 0,
    ClearBitmapDirty_Color = 1u << 0,
    ClearBitmapDirty_Depth = 1u << 1,
    ClearBitmapDirty_All = ClearBitmapDirty_Color | ClearBitmapDirty_Depth,
};

struct alignas(16) ClearBitmapPushConstants
{
    std::array<float, 2> Offset{{0.0f, 0.0f}};
    std::uint32_t OpaquePolyId = 0;
    std::uint32_t Padding = 0;
};

static_assert(sizeof(ClearBitmapPushConstants) == 16);
static_assert(offsetof(ClearBitmapPushConstants, OpaquePolyId) == 8);

struct ClearBitmapState
{
    std::uint32_t RenderDispCnt = 0;
    std::uint32_t RawAttr1 = 0;
    std::uint32_t RawAttr2 = 0;
    bool Enabled = false;
    std::uint8_t OffsetX = 0;
    std::uint8_t OffsetY = 0;
    std::uint8_t OpaquePolyId = 0;
    std::array<float, 2> Offset{{0.0f, 0.0f}};
    ClearBitmapPushConstants PushConstants{};
};

class ClearBitmapDirtyTracker
{
public:
    void Reset() noexcept;
    void MarkDirty(std::uint32_t mask) noexcept;
    [[nodiscard]] std::uint32_t PendingMask() const noexcept;
    std::uint32_t ConsumeIfEnabled(bool enabled) noexcept;

private:
    std::uint32_t DirtyMask = ClearBitmapDirty_All;
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

ClearBitmapState DecodeClearBitmapState(
    std::uint32_t renderDispCnt,
    std::uint32_t renderClearAttr1,
    std::uint32_t renderClearAttr2) noexcept;

std::array<std::uint8_t, 4> DecodeClearBitmapColorTexel(
    std::uint16_t color) noexcept;

std::uint32_t DecodeClearBitmapDepthTexel(std::uint16_t value) noexcept;

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept;

} // namespace melonDS::Vulkan
