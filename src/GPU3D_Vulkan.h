#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU3D_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_PLANE_CONTRACT_V1
// MELONPRIME_VULKAN_CLEAR_BITMAP_CONTRACT_V1
// MELONPRIME_VULKAN_VERTEX_UPLOAD_CONTRACT_V1
// MELONPRIME_VULKAN_POLYGON_BATCH_CONTRACT_V1
// MELONPRIME_VULKAN_OPAQUE_PIPELINE_CONTRACT_V1
// MELONPRIME_VULKAN_TRANSLUCENT_PIPELINE_CONTRACT_V1
// MELONPRIME_VULKAN_SHADOW_PIPELINE_CONTRACT_V1
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_CONTRACT_V1
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_SHADER_ABI_V1

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace melonDS
{
class GPU3D;
struct Polygon;
}

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kRasterTargetContractVersion = 1;
inline constexpr std::uint32_t kClearPlaneContractVersion = 1;
inline constexpr std::uint32_t kClearBitmapContractVersion = 1;
inline constexpr std::uint32_t kVertexUploadContractVersion = 1;
inline constexpr std::uint32_t kPolygonBatchContractVersion = 1;
inline constexpr std::uint32_t kOpaquePipelineContractVersion = 1;
inline constexpr std::uint32_t kTranslucentPipelineContractVersion = 1;
inline constexpr std::uint32_t kShadowPipelineContractVersion = 1;

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

// Sapphire's graphics path uploads the full DS depth and W values as floats.
// Keep the legacy seven-word vertex ABI intact for the phase harnesses while
// carrying the values that its 16-bit DepthZW field cannot represent.
struct VulkanRasterDepthW
{
    std::uint32_t Depth = 0;
    std::uint32_t W = 1;
};

static_assert(sizeof(VulkanRasterDepthW) == 2u * sizeof(std::uint32_t));

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
    bool BetterPolygons = false;
    float PassiveRepeatCoveragePixels = 0.0f;
    const std::uint32_t* TextureLayers = nullptr;
    std::size_t TextureLayerCount = 0;
};

struct VulkanRasterUpload
{
    std::vector<VulkanPackedVertex> Vertices;
    std::vector<VulkanRasterDepthW> FullPrecisionDepthW;
    std::vector<std::uint16_t> Indices;
    std::vector<std::uint16_t> EdgeIndices;
    std::vector<VulkanPackedPolygon> Polygons;
    std::uint32_t SourcePolygonCount = 0;
    std::uint32_t SkippedDegenerateCount = 0;
    bool Valid = false;

    void Clear() noexcept;
};

enum class VulkanRasterRenderMode : std::uint32_t
{
    Opaque = 0,
    Translucent = 1,
    ShadowMask = 2,
    Shadow = 3,
};

enum class VulkanRasterTextureMode : std::uint32_t
{
    Modulate = 0,
    Decal = 1,
    ToonHighlight = 2,
    Reserved = 3,
    None = 0xFFFFFFFFu,
};

enum class VulkanRasterToonMode : std::uint32_t
{
    None = 0,
    Toon = 1,
    Highlight = 2,
};

enum class VulkanRasterSamplerAxisMode : std::uint32_t
{
    Clamp = 0,
    Repeat = 1,
    Mirror = 2,
};

// Fixed-size pipeline identity. Only state that can change Vulkan pipeline or
// pass semantics belongs here; texture object identity remains in TextureKey.
struct alignas(16) VulkanRasterPipelineKey
{
    std::uint32_t Primitive = 0;
    std::uint32_t RenderMode = 0;
    std::uint32_t TextureMode = 0xFFFFFFFFu;
    std::uint32_t ToonMode = 0;
    std::uint32_t WBuffer = 0;
    std::uint32_t DepthEqual = 0;
    std::uint32_t DepthWrite = 0;
    std::uint32_t FogAttrWrite = 0;
    std::uint32_t ShadowStage = 0;
    std::uint32_t NeedsOpaquePass = 0;
    std::uint32_t AlphaZero = 0;
    std::uint32_t Textured = 0;
    std::uint32_t ColorFormat = 0;
    std::uint32_t AttributeFormat = 0;
    std::uint32_t DepthStencilFormat = 0;
    std::uint32_t StencilReference = 0;
};

static_assert(sizeof(VulkanRasterPipelineKey) == 64);
static_assert(alignof(VulkanRasterPipelineKey) == 16);

// Exact texture/sampler binding identity. Adjacent polygons may share one draw
// only when both PipelineKey and TextureKey are identical.
struct alignas(16) VulkanRasterTextureKey
{
    std::uint32_t Enabled = 0;
    std::uint32_t NormalizedTexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint32_t TextureLayer = 0xFFFFFFFFu;
    std::uint32_t TextureRepeat = 0;
    std::uint32_t SamplerS = 0;
    std::uint32_t SamplerT = 0;
    std::uint32_t TextureFormat = 0;
};

static_assert(sizeof(VulkanRasterTextureKey) == 32);
static_assert(alignof(VulkanRasterTextureKey) == 16);

// One batch represents one contiguous range in RenderPolygonRAM order. A/B/A
// is always three ranges; no frame-wide regrouping is permitted.
struct alignas(16) VulkanRasterBatch
{
    std::uint32_t FirstPolygon = 0;
    std::uint32_t PolygonCount = 0;
    std::uint32_t FirstSourceOrder = 0;
    std::uint32_t LastSourceOrder = 0;
    std::uint32_t IndexOffset = 0;
    std::uint32_t IndexCount = 0;
    std::uint32_t EdgeIndexOffset = 0;
    std::uint32_t EdgeIndexCount = 0;
    VulkanRasterPipelineKey PipelineKey{};
    VulkanRasterTextureKey TextureKey{};
};

static_assert(sizeof(VulkanRasterBatch) == 128);
static_assert(alignof(VulkanRasterBatch) == 16);
static_assert(offsetof(VulkanRasterBatch, PipelineKey) == 32);
static_assert(offsetof(VulkanRasterBatch, TextureKey) == 96);

struct VulkanRasterBatchOptions
{
    std::uint32_t RenderDispCnt = 0;
    VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat AttributeFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED;
};

struct VulkanRasterBatchPlan
{
    std::vector<VulkanRasterBatch> Batches;
    std::uint32_t SourcePolygonCount = 0;
    bool SourceOrderPreserved = false;
    bool AdjacentOnly = false;
    bool Valid = false;

    void Clear() noexcept;
};

// Phase 7.6 translates one opaque triangle batch key into Vulkan 1.1 fixed
// function state. Textured, translucent, shadow and line paths remain later
// subphases; this contract prevents the bootstrap from overclaiming support.
struct VulkanOpaquePipelineState
{
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
    VkBool32 DepthWrite = VK_TRUE;
    VkBool32 StencilReplace = VK_TRUE;
    std::uint32_t StencilReference = 0;
    VkColorComponentFlags ColorWriteMask = 0;
    VkColorComponentFlags AttributeWriteMask = 0;
    bool WBuffer = false;
    bool AlphaTest = true;
    bool Valid = false;
};

struct alignas(16) VulkanOpaquePushConstants
{
    std::array<float, 2> ScreenSize{{256.0f, 192.0f}};
    std::uint32_t RenderDispCnt = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(VulkanOpaquePushConstants) == 16);
static_assert(offsetof(VulkanOpaquePushConstants, RenderDispCnt) == 8);

// Phase 7.7 translates one untextured translucent triangle batch key into
// Vulkan fixed-function blend/depth/stencil state. The stencil compare mirrors
// the DS rule that only a translucent fragment with the same polyID is blocked;
// a different translucent polyID may blend over the previous result.
struct VulkanTranslucentPipelineState
{
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
    VkBool32 DepthWrite = VK_FALSE;
    VkBool32 BlendEnable = VK_TRUE;
    VkBlendFactor SrcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor DstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp ColorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor SrcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor DstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendOp AlphaBlendOp = VK_BLEND_OP_MAX;
    VkCompareOp StencilCompare = VK_COMPARE_OP_NOT_EQUAL;
    VkStencilOp StencilPass = VK_STENCIL_OP_REPLACE;
    std::uint32_t StencilCompareMask = 0x7Fu;
    std::uint32_t StencilWriteMask = 0x7Fu;
    std::uint32_t StencilReference = 0x40u;
    VkColorComponentFlags ColorWriteMask = 0;
    VkColorComponentFlags AttributeWriteMask = 0;
    bool WBuffer = false;
    bool AlphaRangeTest = true;
    bool NeedsOpaquePass = false;
    bool Valid = false;
};

struct alignas(16) VulkanTranslucentPushConstants
{
    std::array<float, 2> ScreenSize{{256.0f, 192.0f}};
    std::uint32_t RenderDispCnt = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(VulkanTranslucentPushConstants) == 16);
static_assert(offsetof(VulkanTranslucentPushConstants, RenderDispCnt) == 8);

// Phase 7.8 shadow mask stage. Bit 7 is written only on depth failure; the
// lower seven stencil bits are preserved by the 0x80 write mask.
struct VulkanShadowMaskPipelineState
{
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
    VkBool32 DepthWrite = VK_FALSE;
    VkCompareOp StencilCompare = VK_COMPARE_OP_ALWAYS;
    VkStencilOp StencilFail = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilDepthFail = VK_STENCIL_OP_REPLACE;
    VkStencilOp StencilPass = VK_STENCIL_OP_KEEP;
    std::uint32_t StencilCompareMask = 0x80u;
    std::uint32_t StencilWriteMask = 0x80u;
    std::uint32_t StencilReference = 0x80u;
    VkColorComponentFlags ColorWriteMask = 0;
    VkColorComponentFlags AttributeWriteMask = 0;
    bool WBuffer = false;
    bool Valid = false;
};

// First visible-shadow stage. Fragments whose lower six stencil bits equal the
// shadow polygon ID clear bit 7, preventing a polygon from shadowing itself.
struct VulkanShadowRejectPipelineState
{
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
    VkBool32 DepthWrite = VK_FALSE;
    VkCompareOp StencilCompare = VK_COMPARE_OP_EQUAL;
    VkStencilOp StencilFail = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilDepthFail = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilPass = VK_STENCIL_OP_ZERO;
    std::uint32_t StencilCompareMask = 0x3Fu;
    std::uint32_t StencilWriteMask = 0x80u;
    std::uint32_t StencilReference = 0;
    VkColorComponentFlags ColorWriteMask = 0;
    VkColorComponentFlags AttributeWriteMask = 0;
    bool WBuffer = false;
    bool AlphaRangeTest = true;
    bool Valid = false;
};

// Second visible-shadow stage. Only pixels retaining stencil bit 7 are blended.
// The lower seven bits become 0x40 | polygon ID while bit 7 remains untouched.
struct VulkanShadowBlendPipelineState
{
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS;
    VkBool32 DepthWrite = VK_FALSE;
    VkBool32 BlendEnable = VK_TRUE;
    VkBlendFactor SrcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor DstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp ColorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor SrcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor DstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendOp AlphaBlendOp = VK_BLEND_OP_MAX;
    VkCompareOp StencilCompare = VK_COMPARE_OP_EQUAL;
    VkStencilOp StencilFail = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilDepthFail = VK_STENCIL_OP_KEEP;
    VkStencilOp StencilPass = VK_STENCIL_OP_REPLACE;
    std::uint32_t StencilCompareMask = 0x80u;
    std::uint32_t StencilWriteMask = 0x7Fu;
    std::uint32_t StencilReference = 0x40u;
    VkColorComponentFlags ColorWriteMask = 0;
    VkColorComponentFlags AttributeWriteMask = 0;
    bool WBuffer = false;
    bool AlphaRangeTest = true;
    bool Valid = false;
};

using VulkanShadowPushConstants = VulkanTranslucentPushConstants;

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

// Builds the upload from Sapphire's shared accelerated scene frontend.  This
// preserves its line selection, polygon boundary, high-resolution coordinate,
// and Better Polygons triangulation behavior while retaining MelonPrime's
// presenter-owned Vulkan resources.
bool BuildVulkanAcceleratedRasterUpload(
    const GPU3D& gpu3D,
    const VulkanRasterBuildOptions& options,
    VulkanRasterUpload& upload,
    std::string* failureReason = nullptr);

VulkanRasterPipelineKey BuildVulkanRasterPipelineKey(
    const VulkanPackedPolygon& polygon,
    const VulkanRasterBatchOptions& options) noexcept;

VulkanRasterTextureKey BuildVulkanRasterTextureKey(
    const VulkanPackedPolygon& polygon) noexcept;

bool BuildVulkanRasterBatchPlan(
    const VulkanRasterUpload& upload,
    const VulkanRasterBatchOptions& options,
    VulkanRasterBatchPlan& plan,
    std::string* failureReason = nullptr);

VulkanOpaquePipelineState BuildVulkanOpaquePipelineState(
    const VulkanRasterPipelineKey& key) noexcept;

VulkanTranslucentPipelineState BuildVulkanTranslucentPipelineState(
    const VulkanRasterPipelineKey& key) noexcept;

VulkanShadowMaskPipelineState BuildVulkanShadowMaskPipelineState(
    const VulkanRasterPipelineKey& key) noexcept;

VulkanShadowRejectPipelineState BuildVulkanShadowRejectPipelineState(
    const VulkanRasterPipelineKey& key) noexcept;

VulkanShadowBlendPipelineState BuildVulkanShadowBlendPipelineState(
    const VulkanRasterPipelineKey& key) noexcept;

inline constexpr std::uint32_t kToonHighlightContractVersion = 1;

enum class VulkanToonHighlightMode : std::uint32_t
{
    None = 0,
    Toon = 1,
    Highlight = 2,
};

struct alignas(16) VulkanToonHighlightConfig
{
    std::array<std::array<float, 4>, 32> ToonColors{};
    std::uint32_t DispCnt = 0;
    std::uint32_t Mode = 0;
    std::uint32_t Textured = 0;
    std::uint32_t Padding = 0;
};

static_assert(sizeof(VulkanToonHighlightConfig) == 528);
static_assert(alignof(VulkanToonHighlightConfig) == 16);

VulkanToonHighlightConfig BuildVulkanToonHighlightConfig(
    const std::array<std::uint16_t, 32>& toonTable,
    std::uint32_t dispCnt,
    VulkanToonHighlightMode mode,
    bool textured) noexcept;


inline constexpr std::uint32_t kVulkanToonHighlightDescriptorSet = 0;
inline constexpr std::uint32_t kVulkanToonHighlightDescriptorBinding = 0;
inline constexpr std::uint32_t kVulkanToonHighlightShaderAbiVersion = 1;

struct VulkanToonHighlightShaderAbi
{
    std::uint32_t DescriptorSet = kVulkanToonHighlightDescriptorSet;
    std::uint32_t DescriptorBinding = kVulkanToonHighlightDescriptorBinding;
    std::uint32_t ConfigSize = sizeof(VulkanToonHighlightConfig);
    std::uint32_t ToonTableEntries = 32;
    bool SupportsOpaque = true;
    bool SupportsTranslucent = true;
    bool SupportsWBuffer = true;
    bool SupportsTextured = true;
};

static_assert(sizeof(VulkanToonHighlightConfig) == 528);
static_assert(offsetof(VulkanToonHighlightConfig, DispCnt) == 512);
static_assert(offsetof(VulkanToonHighlightConfig, Mode) == 516);
static_assert(offsetof(VulkanToonHighlightConfig, Textured) == 520);

VulkanToonHighlightShaderAbi DescribeVulkanToonHighlightShaderAbi() noexcept;

std::array<float, 4> EvaluateVulkanToonHighlightReference(
    const VulkanToonHighlightConfig& config,
    const std::array<float, 4>& vertexColor,
    const std::array<float, 4>& textureColor) noexcept;

VkImageAspectFlags DepthStencilAspectMask(VkFormat format) noexcept;


// MELONPRIME_VULKAN_TEXTURE_SAMPLING_CONTRACT_V1
inline constexpr std::uint32_t kTextureSamplingContractVersion = 1;

enum class VulkanTextureCombinerMode : std::uint32_t
{
    Raw = 0,
    Modulate = 1,
    Decal = 2,
    Toon = 3,
    Highlight = 4,
};

struct VulkanTextureSamplingDescriptorContract
{
    std::uint32_t ContractVersion = kTextureSamplingContractVersion;
    std::uint32_t DescriptorSet = 0;
    std::uint32_t UniformBinding = 0;
    std::uint32_t ClampBinding = 1;
    std::uint32_t RepeatBinding = 2;
    std::uint32_t MirrorBinding = 3;
    std::uint32_t OutputBinding = 4;
    VkFormat TextureFormat = VK_FORMAT_R8G8B8A8_UINT;
};

struct VulkanTextureCombinerInput
{
    VulkanTextureCombinerMode Mode = VulkanTextureCombinerMode::Raw;
    std::array<float, 4> VertexColor{{1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<float, 4> TextureColor{{1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<float, 4> ToonColor{{0.0f, 0.0f, 0.0f, 1.0f}};
};

VulkanTextureSamplingDescriptorContract
DescribeVulkanTextureSamplingDescriptorContract() noexcept;
std::array<float, 4> EvaluateVulkanTextureCombiner(
    const VulkanTextureCombinerInput& input) noexcept;
std::array<std::uint8_t, 4> QuantizeVulkanColor8(
    const std::array<float, 4>& color) noexcept;

// MELONPRIME_VULKAN_TEXTURED_POLYGON_PIPELINE_CONTRACT_V1
inline constexpr std::uint32_t kTexturedPolygonPipelineContractVersion = 1;

struct VulkanTexturedPolygonDescriptorContract
{
    std::uint32_t ContractVersion = kTexturedPolygonPipelineContractVersion;
    std::uint32_t DescriptorSet = 0;
    std::uint32_t UniformBinding = 0;
    std::uint32_t TextureBinding = 1;
    VkFormat TextureFormat = VK_FORMAT_R8G8B8A8_UINT;
    bool Modulate = true;
    bool Decal = true;
    bool Toon = true;
    bool Highlight = true;
};

VulkanTexturedPolygonDescriptorContract
DescribeVulkanTexturedPolygonDescriptorContract() noexcept;

} // namespace melonDS::Vulkan
