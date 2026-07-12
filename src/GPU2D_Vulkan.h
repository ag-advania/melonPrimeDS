#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU2D_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE9_COMPLETION_CONTRACT_V1

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kPhase9CompletionContractVersion = 1;
inline constexpr std::uint32_t kPhase9NativeWidth = 256;
inline constexpr std::uint32_t kPhase9NativeHeight = 192;
inline constexpr std::uint32_t kPhase9ScreenCount = 2;

using VulkanPhase9PackedPixel = std::uint32_t;

enum class VulkanPhase9DisplayMode : std::uint32_t
{
    Layers = 0,
    Vram = 2,
    Fifo = 3,
};

enum class VulkanPhase9BrightnessMode : std::uint32_t
{
    None = 0,
    Increase = 1,
    Decrease = 2,
};

struct alignas(16) VulkanPhase9ScanlineState
{
    std::uint32_t DisplayModeA = 0;
    std::uint32_t DisplayModeB = 0;
    std::uint32_t ScreenSwap = 0;
    std::uint32_t ScreenEnabledMask = 3;
    std::uint32_t WindowX0 = 0;
    std::uint32_t WindowX1 = kPhase9NativeWidth;
    std::uint32_t MosaicX = 1;
    std::uint32_t MosaicY = 1;
    std::uint32_t WindowEnableMask = 0;
    std::uint32_t ObjEnableMask = 3;
    std::uint32_t Use3DMask = 1;
    std::uint32_t BlendEnableMask = 0;
    std::uint32_t Eva = 16;
    std::uint32_t Evb = 0;
    std::uint32_t BrightModeA = 0;
    std::uint32_t BrightModeB = 0;
    std::uint32_t BrightFactorA = 0;
    std::uint32_t BrightFactorB = 0;
    std::uint32_t Reserved0 = 0;
    std::uint32_t Reserved1 = 0;
};

static_assert(sizeof(VulkanPhase9ScanlineState) == 80);
static_assert(alignof(VulkanPhase9ScanlineState) == 16);
static_assert(offsetof(VulkanPhase9ScanlineState, DisplayModeA) == 0);
static_assert(offsetof(VulkanPhase9ScanlineState, WindowX0) == 16);
static_assert(offsetof(VulkanPhase9ScanlineState, WindowEnableMask) == 32);
static_assert(offsetof(VulkanPhase9ScanlineState, Eva) == 48);
static_assert(offsetof(VulkanPhase9ScanlineState, BrightModeA) == 56);
static_assert(offsetof(VulkanPhase9ScanlineState, BrightFactorA) == 64);

struct VulkanPhase9RenderRange
{
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = 0;
};

struct VulkanPhase9OutputContract
{
    std::uint32_t Scale = 1;
    std::uint32_t Width = kPhase9NativeWidth;
    std::uint32_t Height = kPhase9NativeHeight;
    std::uint32_t LayerCount = kPhase9ScreenCount;
    VkFormat Format = VK_FORMAT_R8G8B8A8_UINT;
    VkImageUsageFlags Usage = 0;
    VkImageLayout PublishedLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool PresenterSampleable = true;
    bool TransferSourceForDebug = true;
    bool NormalFrameCpuReadback = false;
    bool IndependentOutputSlot = true;
    bool Valid = false;
};

struct VulkanPhase9FrameSnapshot
{
    std::uint64_t FrameSerial = 0;
    std::uint64_t ThreeDSerial = 0;
    std::uint64_t TwoDFinalSerial = 0;
    std::uint64_t CaptureSerial = 0;
    std::uint64_t Generation = 0;
    std::uint32_t Scale = 1;
    std::uint32_t EngineALayer = 0;
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = kPhase9NativeHeight;
    bool ProducerComplete = false;
};

struct VulkanPhase9ExitAudit
{
    bool Software2DUploadFinal = false;
    bool Native2DMirror = false;
    bool Native2DVisible = false;
    bool Bg = false;
    bool Obj = false;
    bool Window = false;
    bool Blend = false;
    bool Mosaic = false;
    bool Brightness = false;
    bool ThreeDLayer = false;
    bool PerScanlineState = false;
    bool ScreenAB = false;
    bool PartialRender = false;
    bool CaptureSource = false;
    bool FinalPass = false;
    bool ScreenSwap = false;
    bool VramDisplay = false;
    bool FifoDisplay = false;
    bool ScreenDisable = false;
    bool Scale = false;
    bool GpuResidentOutput = false;
    bool FrameSerialOwnership = false;
    bool NoNormalCpuReadback = false;
    bool DebugReadback = false;

    [[nodiscard]] bool Passed() const noexcept;
};

VulkanPhase9PackedPixel PackVulkanPhase9Pixel(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue,
    std::uint32_t alpha) noexcept;

std::array<std::uint8_t, 4> UnpackVulkanPhase9Pixel(
    VulkanPhase9PackedPixel pixel) noexcept;

std::vector<VulkanPhase9RenderRange> BuildVulkanPhase9RenderRanges(
    const std::vector<VulkanPhase9ScanlineState>& states,
    std::string* failureReason = nullptr);

VulkanPhase9PackedPixel ComposeVulkanPhase9LayerPixel(
    VulkanPhase9PackedPixel bg,
    VulkanPhase9PackedPixel obj,
    VulkanPhase9PackedPixel threeD,
    const VulkanPhase9ScanlineState& state,
    std::uint32_t engine) noexcept;

VulkanPhase9PackedPixel ComposeVulkanPhase9FinalPixel(
    VulkanPhase9PackedPixel layerA,
    VulkanPhase9PackedPixel layerB,
    VulkanPhase9PackedPixel vram,
    VulkanPhase9PackedPixel fifo,
    const VulkanPhase9ScanlineState& state,
    std::uint32_t outputLayer) noexcept;

VulkanPhase9OutputContract BuildVulkanPhase9OutputContract(
    std::uint32_t scale) noexcept;

bool VulkanPhase9SnapshotReady(
    const VulkanPhase9FrameSnapshot& snapshot,
    std::string* failureReason = nullptr) noexcept;

} // namespace melonDS::Vulkan
