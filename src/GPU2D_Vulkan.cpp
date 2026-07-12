#include "GPU2D_Vulkan.h"

#include <algorithm>
#include <cstring>

namespace melonDS::Vulkan
{
namespace
{

std::uint32_t ClampByte(std::uint32_t value) noexcept
{
    return std::min<std::uint32_t>(value, 255u);
}

VulkanPhase9PackedPixel BlendPixel(
    VulkanPhase9PackedPixel top,
    VulkanPhase9PackedPixel bottom,
    std::uint32_t eva,
    std::uint32_t evb) noexcept
{
    const auto t = UnpackVulkanPhase9Pixel(top);
    const auto b = UnpackVulkanPhase9Pixel(bottom);
    if (t[3] == 0)
        return bottom;
    if (b[3] == 0)
        return top;
    eva = std::min<std::uint32_t>(eva, 16u);
    evb = std::min<std::uint32_t>(evb, 16u);
    return PackVulkanPhase9Pixel(
        ClampByte((t[0] * eva + b[0] * evb + 8u) >> 4u),
        ClampByte((t[1] * eva + b[1] * evb + 8u) >> 4u),
        ClampByte((t[2] * eva + b[2] * evb + 8u) >> 4u),
        std::max<std::uint32_t>(t[3], b[3]));
}

VulkanPhase9PackedPixel ApplyBrightness(
    VulkanPhase9PackedPixel pixel,
    std::uint32_t mode,
    std::uint32_t factor) noexcept
{
    const auto c = UnpackVulkanPhase9Pixel(pixel);
    factor = std::min<std::uint32_t>(factor, 16u);
    std::array<std::uint32_t, 3> rgb{{c[0], c[1], c[2]}};
    if (mode == static_cast<std::uint32_t>(VulkanPhase9BrightnessMode::Increase))
    {
        for (auto& channel : rgb)
            channel = channel + (((255u - channel) * factor + 8u) >> 4u);
    }
    else if (mode == static_cast<std::uint32_t>(VulkanPhase9BrightnessMode::Decrease))
    {
        for (auto& channel : rgb)
            channel = channel - ((channel * factor + 7u) >> 4u);
    }
    return PackVulkanPhase9Pixel(rgb[0], rgb[1], rgb[2], c[3]);
}

bool EqualState(
    const VulkanPhase9ScanlineState& a,
    const VulkanPhase9ScanlineState& b) noexcept
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

VulkanPhase9PackedPixel PackVulkanPhase9Pixel(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue,
    std::uint32_t alpha) noexcept
{
    return (red & 0xFFu) | ((green & 0xFFu) << 8u) |
        ((blue & 0xFFu) << 16u) | ((alpha & 0xFFu) << 24u);
}

std::array<std::uint8_t, 4> UnpackVulkanPhase9Pixel(
    VulkanPhase9PackedPixel pixel) noexcept
{
    return {{
        static_cast<std::uint8_t>(pixel & 0xFFu),
        static_cast<std::uint8_t>((pixel >> 8u) & 0xFFu),
        static_cast<std::uint8_t>((pixel >> 16u) & 0xFFu),
        static_cast<std::uint8_t>((pixel >> 24u) & 0xFFu),
    }};
}

std::vector<VulkanPhase9RenderRange> BuildVulkanPhase9RenderRanges(
    const std::vector<VulkanPhase9ScanlineState>& states,
    std::string* failureReason)
{
    if (states.size() != kPhase9NativeHeight)
    {
        if (failureReason)
            *failureReason = "Phase 9 requires exactly 192 scanline states";
        return {};
    }
    std::vector<VulkanPhase9RenderRange> ranges;
    std::uint32_t start = 0;
    for (std::uint32_t line = 1; line < kPhase9NativeHeight; ++line)
    {
        if (!EqualState(states[line - 1u], states[line]))
        {
            ranges.push_back({start, line});
            start = line;
        }
    }
    ranges.push_back({start, kPhase9NativeHeight});
    return ranges;
}

VulkanPhase9PackedPixel ComposeVulkanPhase9LayerPixel(
    VulkanPhase9PackedPixel bg,
    VulkanPhase9PackedPixel obj,
    VulkanPhase9PackedPixel threeD,
    const VulkanPhase9ScanlineState& state,
    std::uint32_t engine) noexcept
{
    const std::uint32_t bit = 1u << (engine & 1u);
    VulkanPhase9PackedPixel result = bg;
    const bool blend = (state.BlendEnableMask & bit) != 0u;
    if ((state.Use3DMask & bit) != 0u)
    {
        result = blend ? BlendPixel(threeD, result, state.Eva, state.Evb)
                       : (UnpackVulkanPhase9Pixel(threeD)[3] != 0u ? threeD : result);
    }
    if ((state.ObjEnableMask & bit) != 0u)
    {
        result = blend ? BlendPixel(obj, result, state.Eva, state.Evb)
                       : (UnpackVulkanPhase9Pixel(obj)[3] != 0u ? obj : result);
    }
    return result;
}

VulkanPhase9PackedPixel ComposeVulkanPhase9FinalPixel(
    VulkanPhase9PackedPixel layerA,
    VulkanPhase9PackedPixel layerB,
    VulkanPhase9PackedPixel vram,
    VulkanPhase9PackedPixel fifo,
    const VulkanPhase9ScanlineState& state,
    std::uint32_t outputLayer) noexcept
{
    const std::uint32_t engine = (outputLayer ^ (state.ScreenSwap & 1u)) & 1u;
    const std::uint32_t bit = 1u << engine;
    if ((state.ScreenEnabledMask & bit) == 0u)
        return PackVulkanPhase9Pixel(0u, 0u, 0u, 255u);
    const std::uint32_t mode = engine == 0u ? state.DisplayModeA : state.DisplayModeB;
    VulkanPhase9PackedPixel pixel = engine == 0u ? layerA : layerB;
    if (mode == static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Vram))
        pixel = vram;
    else if (mode == static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Fifo))
        pixel = fifo;
    const std::uint32_t brightMode = engine == 0u ? state.BrightModeA : state.BrightModeB;
    const std::uint32_t brightFactor = engine == 0u ? state.BrightFactorA : state.BrightFactorB;
    return ApplyBrightness(pixel, brightMode, brightFactor);
}

VulkanPhase9OutputContract BuildVulkanPhase9OutputContract(
    std::uint32_t scale) noexcept
{
    VulkanPhase9OutputContract contract;
    contract.Scale = scale;
    contract.Width = kPhase9NativeWidth * scale;
    contract.Height = kPhase9NativeHeight * scale;
    contract.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    contract.Valid = scale >= 1u && scale <= 16u &&
        contract.Width / scale == kPhase9NativeWidth &&
        contract.Height / scale == kPhase9NativeHeight;
    return contract;
}

bool VulkanPhase9SnapshotReady(
    const VulkanPhase9FrameSnapshot& snapshot,
    std::string* failureReason) noexcept
{
    if (!snapshot.ProducerComplete)
    {
        if (failureReason) *failureReason = "producer is not complete";
        return false;
    }
    if (snapshot.FrameSerial == 0u || snapshot.Generation == 0u)
    {
        if (failureReason) *failureReason = "zero frame serial or generation";
        return false;
    }
    if (snapshot.ThreeDSerial != snapshot.FrameSerial ||
        snapshot.TwoDFinalSerial != snapshot.FrameSerial ||
        snapshot.CaptureSerial != snapshot.FrameSerial)
    {
        if (failureReason) *failureReason = "mixed frame generations";
        return false;
    }
    if (snapshot.Scale < 1u || snapshot.Scale > 16u ||
        snapshot.YStart >= snapshot.YEnd || snapshot.YEnd > kPhase9NativeHeight ||
        snapshot.EngineALayer >= kPhase9ScreenCount)
    {
        if (failureReason) *failureReason = "invalid Phase 9 snapshot metadata";
        return false;
    }
    return true;
}

bool VulkanPhase9ExitAudit::Passed() const noexcept
{
    return Software2DUploadFinal && Native2DMirror && Native2DVisible && Bg && Obj &&
        Window && Blend && Mosaic && Brightness && ThreeDLayer && PerScanlineState &&
        ScreenAB && PartialRender && CaptureSource && FinalPass && ScreenSwap &&
        VramDisplay && FifoDisplay && ScreenDisable && Scale && GpuResidentOutput &&
        FrameSerialOwnership && NoNormalCpuReadback && DebugReadback;
}

} // namespace melonDS::Vulkan
