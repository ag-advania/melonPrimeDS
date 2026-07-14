#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU3D_VulkanCompute.h is owned by the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE11_COMPUTE_CONTRACT_V1

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kPhase11ComputeContractVersion = 1;
inline constexpr std::uint32_t kPhase11ComputeStageCount = 33;
inline constexpr std::uint32_t kPhase11ScaleCount = 5;
inline constexpr std::array<std::uint32_t, kPhase11ScaleCount> kPhase11Scales{
    1u, 2u, 4u, 8u, 16u};
inline constexpr std::uint32_t kPhase11TileSize = 8;
inline constexpr std::uint32_t kPhase11DescriptorBindingCount = 5;

// The order is intentionally identical to the Phase 11 implementation plan.
enum class VulkanComputeStage : std::uint32_t
{
    InterpSpansZ = 0,
    InterpSpansW,
    BinCombined,
    DepthBlendZ,
    DepthBlendW,
    RasteriseNoTextureZ,
    RasteriseNoTextureW,
    RasteriseNoTextureToonZ,
    RasteriseNoTextureToonW,
    RasteriseNoTextureHighlightZ,
    RasteriseNoTextureHighlightW,
    RasteriseTextureDecalZ,
    RasteriseTextureDecalW,
    RasteriseTextureModulateZ,
    RasteriseTextureModulateW,
    RasteriseTextureToonZ,
    RasteriseTextureToonW,
    RasteriseTextureHighlightZ,
    RasteriseTextureHighlightW,
    RasteriseShadowMaskZ,
    RasteriseShadowMaskW,
    ClearCoarseBinMask,
    ClearIndirectWorkCount,
    CalculateWorkOffsets,
    SortWork,
    FinalPass,
    FinalPassEdge,
    FinalPassFog,
    FinalPassEdgeFog,
    FinalPassAA,
    FinalPassAAEdge,
    FinalPassAAFog,
    FinalPassAAEdgeFog,
};

struct alignas(16) VulkanComputeMetaUniform
{
    std::uint32_t ScreenWidth = 0;
    std::uint32_t ScreenHeight = 0;
    std::uint32_t LayerCount = 2;
    std::uint32_t ScaleFactor = 1;
    std::uint32_t HiresCoordinates = 0;
    std::uint32_t FinalPassFlags = 0;
    std::uint32_t PixelCount = 0;
    std::uint32_t Iteration = 0;
};
static_assert(sizeof(VulkanComputeMetaUniform) == 32);
static_assert(alignof(VulkanComputeMetaUniform) == 16);
static_assert(offsetof(VulkanComputeMetaUniform, ScreenWidth) == 0);
static_assert(offsetof(VulkanComputeMetaUniform, HiresCoordinates) == 16);
static_assert(offsetof(VulkanComputeMetaUniform, PixelCount) == 24);

struct alignas(16) VulkanComputeSpanSetupY
{
    std::int32_t Y0 = 0;
    std::int32_t Y1 = 0;
    std::int32_t XMajor = 0;
    std::int32_t Flags = 0;
};
static_assert(sizeof(VulkanComputeSpanSetupY) == 16);

struct alignas(16) VulkanComputeSpanSetupX
{
    std::int32_t X0 = 0;
    std::int32_t X1 = 0;
    std::int32_t Delta = 0;
    std::int32_t Flags = 0;
};
static_assert(sizeof(VulkanComputeSpanSetupX) == 16);

struct alignas(16) VulkanComputeWorkDesc
{
    std::uint32_t PolygonIndex = 0;
    std::uint32_t TileIndex = 0;
    std::uint32_t Variant = 0;
    std::uint32_t Sequence = 0;
};
static_assert(sizeof(VulkanComputeWorkDesc) == 16);

struct VulkanComputeDeviceLimits
{
    std::uint32_t MaxGroupCountX = 65535;
    std::uint32_t MaxGroupCountY = 65535;
    std::uint32_t MaxGroupCountZ = 65535;
    std::uint32_t MaxInvocations = 128;
    std::uint32_t MaxSharedMemoryBytes = 16384;
};

struct VulkanComputeDispatchChunk
{
    std::uint64_t BaseWorkGroup = 0;
    std::uint32_t GroupCountX = 0;
    std::uint32_t GroupCountY = 0;
    std::uint32_t GroupCountZ = 1;
};

struct VulkanComputeBarrierEdge
{
    VulkanComputeStage Producer{};
    VulkanComputeStage Consumer{};
    bool StorageReadAfterWrite = true;
    bool IndirectReadAfterWrite = false;
    bool ImageReadAfterWrite = false;
};

struct VulkanComputePipelineKey
{
    VulkanComputeStage Stage{};
    std::uint32_t TileSize = kPhase11TileSize;
    std::uint32_t FinalPassFlags = 0;
    bool HiresCoordinates = false;

    friend bool operator==(const VulkanComputePipelineKey& left,
                           const VulkanComputePipelineKey& right) noexcept
    {
        return left.Stage == right.Stage && left.TileSize == right.TileSize &&
            left.FinalPassFlags == right.FinalPassFlags &&
            left.HiresCoordinates == right.HiresCoordinates;
    }
};

struct VulkanComputeExitAudit
{
    bool AllThirtyThreeStages = false;
    bool SharedShaderModule = false;
    bool HostShaderAbiExact = false;
    bool DescriptorLayoutExact = false;
    bool SpecializationPipelineCache = false;
    bool IndirectDispatchIntegrated = false;
    bool LargeWorkSplitIntegrated = false;
    bool ExplicitBarrierGraph = false;
    bool IntegerTextureSampling = false;
    bool CaptureTextureIntegrated = false;
    bool ClearBitmapIntegrated = false;
    bool ShadowIntegrated = false;
    bool FogIntegrated = false;
    bool EdgeIntegrated = false;
    bool AntialiasingIntegrated = false;
    bool HiresCoordinatesIntegrated = false;
    bool ScaleOneThroughSixteen = false;
    bool VisibleOutputOwnedByCompute = false;
    bool OutputRingIntegrated = false;
    bool ValidationClean = false;

    [[nodiscard]] bool Passed() const noexcept;
};

[[nodiscard]] std::string_view DescribeVulkanComputeStage(
    VulkanComputeStage stage) noexcept;
[[nodiscard]] bool IsVulkanComputeFinalPassStage(VulkanComputeStage stage) noexcept;
[[nodiscard]] bool IsVulkanComputeIndirectStage(VulkanComputeStage stage) noexcept;
[[nodiscard]] std::array<VulkanComputeStage, kPhase11ComputeStageCount>
BuildVulkanComputeStageOrder() noexcept;
[[nodiscard]] std::vector<VulkanComputeBarrierEdge>
BuildVulkanComputeBarrierGraph();
[[nodiscard]] std::vector<VulkanComputeDispatchChunk>
BuildVulkanComputeDispatchPlan(
    std::uint64_t realWorkGroupCount,
    const VulkanComputeDeviceLimits& limits);
[[nodiscard]] bool ValidateVulkanComputeDispatchPlan(
    std::uint64_t realWorkGroupCount,
    const VulkanComputeDeviceLimits& limits,
    const std::vector<VulkanComputeDispatchChunk>& chunks) noexcept;
[[nodiscard]] std::uint32_t EvaluateVulkanComputeStageWord(
    VulkanComputeStage stage,
    std::uint32_t index,
    std::uint32_t value,
    std::uint32_t textureValue,
    std::uint32_t captureValue,
    const VulkanComputeMetaUniform& meta) noexcept;
[[nodiscard]] std::uint32_t PackVulkanComputeOutputPixel(
    std::uint32_t value,
    std::uint32_t textureValue,
    std::uint32_t captureValue,
    const VulkanComputeMetaUniform& meta) noexcept;

} // namespace melonDS::Vulkan
