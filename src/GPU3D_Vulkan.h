#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU3D_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_PLANE_CONTRACT_V1
// MELONPRIME_VULKAN_CLEAR_BITMAP_CONTRACT_V1
// MELONPRIME_VULKAN_VERTEX_UPLOAD_CONTRACT_V1

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace melonDS
{
struct Polygon;
}

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kRasterTargetContractVersion = 1;
inline constexpr std::uint32_t kClearPlaneContractVersion = 1;
inline constexpr std::uint32_t kClearBitmapContractVersion = 1;
inline constexpr std::uint32_t kVertexUploadContractVersion = 1;

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

enum class VulkanRasterPrimitive : std::uint32_t
{
    Triangles = 0,
    Lines = 1,
};

enum VulkanRasterPolygonFlags : std::uint32_t
{
    VulkanRasterPolygonFlag_None = 0,
    VulkanRasterPolygonFlag_WBuffer = 1u << 0,
    VulkanRasterPolygonFlag_FacingView = 1u << 1,
    VulkanRasterPolygonFlag_Translucent = 1u << 2,
    VulkanRasterPolygonFlag_ShadowMask = 1u << 3,
    VulkanRasterPolygonFlag_Shadow = 1u << 4,
    VulkanRasterPolygonFlag_Line = 1u << 5,
    VulkanRasterPolygonFlag_Textured = 1u << 6,
};

// Matches the OpenGL Classic 7-word vertex upload contract exactly.
// The future Vulkan vertex shader consumes the same 28-byte payload.
struct VulkanPackedVertex
{
    std::uint32_t PositionXY = 0;
    std::uint32_t DepthZW = 0;
    std::uint32_t ColorRgba = 0;
    std::uint32_t TexcoordST = 0;
    std::uint32_t PolygonFlags = 0;
    std::uint32_t TextureLayer = 0xFFFFFFFFu;
    std::uint32_t TextureSize = 0;
};

static_assert(sizeof(VulkanPackedVertex) == 7u * sizeof(std::uint32_t));
static_assert(offsetof(VulkanPackedVertex, PositionXY) == 0);
static_assert(offsetof(VulkanPackedVertex, DepthZW) == 4);
static_assert(offsetof(VulkanPackedVertex, ColorRgba) == 8);
static_assert(offsetof(VulkanPackedVertex, TexcoordST) == 12);
static_assert(offsetof(VulkanPackedVertex, PolygonFlags) == 16);
static_assert(offsetof(VulkanPackedVertex, TextureLayer) == 20);
static_assert(offsetof(VulkanPackedVertex, TextureSize) == 24);

// One fixed-size record per non-degenerate source polygon. SourceOrder is never
// rewritten, allowing Phase 7.5 to batch adjacent compatible records only.
struct alignas(16) VulkanPackedPolygon
{
    std::uint32_t SourceOrder = 0;
    std::uint32_t Primitive = 0;
    std::uint32_t VertexOffset = 0;
    std::uint32_t VertexCount = 0;
    std::uint32_t IndexOffset = 0;
    std::uint32_t IndexCount = 0;
    std::uint32_t EdgeIndexOffset = 0;
    std::uint32_t EdgeIndexCount = 0;
    std::uint32_t Attr = 0;
    std::uint32_t TexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint32_t TextureLayer = 0xFFFFFFFFu;
    std::uint32_t TextureRepeat = 0;
    std::uint32_t Flags = 0;
    std::uint32_t Reserved0 = 0;
    std::uint32_t Reserved1 = 0;
};

static_assert(sizeof(VulkanPackedPolygon) == 64);
static_assert(alignof(VulkanPackedPolygon) == 16);
static_assert(offsetof(VulkanPackedPolygon, SourceOrder) == 0);
static_assert(offsetof(VulkanPackedPolygon, VertexOffset) == 8);
static_assert(offsetof(VulkanPackedPolygon, IndexOffset) == 16);
static_assert(offsetof(VulkanPackedPolygon, Attr) == 32);
static_assert(offsetof(VulkanPackedPolygon, TextureLayer) == 44);
static_assert(offsetof(VulkanPackedPolygon, Flags) == 52);

struct VulkanRasterBuildOptions
{
    int ScaleFactor = 1;
    const std::uint32_t* TextureLayers = nullptr;
    std::size_t TextureLayerCount = 0;
};

struct VulkanRasterUpload
{
    std::vector<VulkanPackedVertex> Vertices;
    std::vector<std::uint16_t> Indices;
    std::vector<std::uint16_t> EdgeIndices;
    std::vector<VulkanPackedPolygon> Polygons;
    std::uint32_t SourcePolygonCount = 0;
    std::uint32_t SkippedDegenerateCount = 0;
    bool Valid = false;

    void Clear() noexcept;
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

VulkanPackedVertex PackVulkanRasterVertex(
    const Polygon& polygon,
    std::uint32_t vertexIndex,
    int scaleFactor,
    std::uint32_t textureLayer) noexcept;

bool BuildVulkanRasterUpload(
    const Polygon* const* polygons,
    std::size_t polygonCount,
    const VulkanRasterBuildOptions& options,
    VulkanRasterUpload& upload,
    std::string* failureReason = nullptr);

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept;

} // namespace melonDS::Vulkan
