#include "GPU_Vulkan.h"

#include "GPU_ColorOp.h"
#include "Vulkan_RomScaleBridge.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "Platform.h"

namespace melonDS
{

// MELONPRIME_VULKAN_ANDROIDSTYLE_FAST_BRIDGE_V2
namespace
{

constexpr std::size_t kNativeWidth = 256u;
constexpr std::size_t kNativeHeight = 192u;
constexpr std::size_t kNativePixelCount = kNativeWidth * kNativeHeight;

// Android keeps native 2D data compact and performs scaled composition on the
// GPU. MelonPrime's ROM-visible native Vulkan path is not integrated yet, so a
// bounded CPU compatibility budget prevents 8x/16x frame construction from
// monopolizing EmuThread. This permits a 4x 256x192 engine-A surface.
constexpr std::size_t kCompatibilityScalePixelBudget = 1024u * 1024u;

u32 ExpandRgb6ToBgra(u32 color) noexcept
{
    const u32 r6 = color & 0x3Fu;
    const u32 g6 = (color >> 8) & 0x3Fu;
    const u32 b6 = (color >> 16) & 0x3Fu;
    const u32 r8 = (r6 << 2) | (r6 >> 4);
    const u32 g8 = (g6 << 2) | (g6 >> 4);
    const u32 b8 = (b6 << 2) | (b6 >> 4);
    return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
}

u32 ApplyEngineABrightness(u32 color, u16 registerValue) noexcept
{
    const u32 mode = registerValue >> 14;
    const u32 factor = std::min<u32>(registerValue & 0x1Fu, 16u);
    if (mode == 1u)
        color = ColorBrightnessUp(color, factor, 0);
    else if (mode == 2u)
        color = ColorBrightnessDown(color, factor, 0xFu);
    return color;
}

int ResolveCompatibilityScale(int requestedScale) noexcept
{
    int scale = std::clamp(requestedScale, 1, 16);
    while (scale > 1)
    {
        const std::size_t pixels =
            kNativePixelCount * static_cast<std::size_t>(scale) * scale;
        if (pixels <= kCompatibilityScalePixelBudget)
            break;
        --scale;
    }
    return scale;
}

void FastNearestUpscale(
    const u32* source,
    int scale,
    std::vector<u32>& destination)
{
    const int width = static_cast<int>(kNativeWidth) * scale;
    const int height = static_cast<int>(kNativeHeight) * scale;
    destination.resize(static_cast<std::size_t>(width) * height);

    for (int sourceY = 0; sourceY < static_cast<int>(kNativeHeight); ++sourceY)
    {
        u32* firstExpandedRow =
            destination.data() +
            static_cast<std::size_t>(sourceY * scale) * width;
        const u32* sourceRow =
            source + static_cast<std::size_t>(sourceY) * kNativeWidth;

        for (int sourceX = 0; sourceX < static_cast<int>(kNativeWidth); ++sourceX)
        {
            std::fill_n(
                firstExpandedRow + sourceX * scale,
                scale,
                sourceRow[sourceX]);
        }

        const std::size_t rowBytes =
            static_cast<std::size_t>(width) * sizeof(u32);
        for (int repeatY = 1; repeatY < scale; ++repeatY)
        {
            std::memcpy(
                firstExpandedRow + static_cast<std::size_t>(repeatY) * width,
                firstExpandedRow,
                rowBytes);
        }
    }
}

} // namespace

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept
{
    VulkanRendererShellContract contract{};
    contract.ModeName = computeSelected ? "Vulkan Compute Shader" : "Vulkan";
    contract.ComputeSelected = computeSelected;
    return contract;
}

VulkanRenderer::VulkanRenderer(melonDS::NDS& nds, bool useComputeRenderer) noexcept
    : SoftRenderer(nds),
      ComputeRendererSelected(useComputeRenderer),
      Native3DFrame(NativePixelCount, 0)
{
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Init()
{
    if (Initialized)
        return true;

    if (!SoftRenderer::Init())
        return false;

    try
    {
        for (auto& frame : CompatibilityFrames)
        {
            if (!frame)
                frame = std::make_shared<VulkanCompatibilityFrame>();
        }
    }
    catch (const std::bad_alloc&)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[MelonPrime] Vulkan compatibility frame ring allocation failed\n");
        return false;
    }

    Initialized = true;
    const VulkanRendererShellContract contract =
        DescribeVulkanRendererShell(ComputeRendererSelected);
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] %s renderer shell initialized: "
        "software_correctness_baseline=1 native_vulkan_raster_bootstrap=1 "
        "native_vulkan_clear_plane_bootstrap=1 native_vulkan_clear_bitmap_bootstrap=1 "
        "native_vulkan_vertex_upload_bootstrap=1 native_vulkan_polygon_batch_bootstrap=1 "
        "native_vulkan_opaque_pipeline_bootstrap=1 "
        "native_vulkan_translucent_pipeline_bootstrap=1 "
        "native_vulkan_shadow_pipeline_bootstrap=1 native_vulkan_toon_highlight_contract=1 native_vulkan_toon_highlight_shader_abi=1 native_vulkan_toon_highlight_descriptor_runtime=1 native_vulkan_toon_highlight_gpu_draw=1 native_vulkan_texture_sampling_bootstrap=1 native_vulkan_textured_polygon_bootstrap=1 native_vulkan_texture_cache_bootstrap=1 native_vulkan_texture_decode_bootstrap=1 native_vulkan_texture_upload_ring=1 native_vulkan_phase8_subsystem_complete=1 native_vulkan_software_2d_upload_final=1 native_vulkan_2d_composition=1 native_vulkan_final_composition=1 native_vulkan_gpu_resident_output=1 native_vulkan_phase9_subsystem_complete=1 native_vulkan_output_ring=1 native_vulkan_zero_copy_presenter=1 native_vulkan_multi_window_lease=1 native_vulkan_timeline_presenter_wait=1 native_vulkan_phase10_subsystem_complete=1 native_vulkan_rom_integration=0 native_vulkan_3d=0 generation=%llu\n",
        contract.ModeName,
        static_cast<unsigned long long>(OutputGeneration));
    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] Vulkan compatibility performance: "
        "androidstyle_frame_ring=1 slots=3 coverage_only=0 "
        "hires_color_reconstruction=1 single_scaled_screen=1 "
        "presenter_pixel_diff=0\n");
    return true;
}

void VulkanRenderer::AdvanceOutputGeneration() noexcept
{
    ++OutputGeneration;
    if (OutputGeneration == 0)
        OutputGeneration = 1;
    FrameSerial = 0;
}

void VulkanRenderer::Reset()
{
    SoftRenderer::Reset();
    std::fill(Native3DFrame.begin(), Native3DFrame.end(), 0);
    ClearHighResolutionOutput();
    AdvanceOutputGeneration();
}

void VulkanRenderer::Stop()
{
    SoftRenderer::Stop();
    Initialized = false;
    std::fill(Native3DFrame.begin(), Native3DFrame.end(), 0);
    ClearHighResolutionOutput();
    AdvanceOutputGeneration();
}

void VulkanRenderer::PreSavestate()
{
    SoftRenderer::PreSavestate();
}

void VulkanRenderer::PostSavestate()
{
    SoftRenderer::PostSavestate();
    ClearHighResolutionOutput();
    AdvanceOutputGeneration();
}

void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    SoftRenderer::SetRenderSettings(settings);

    const int requestedScale = std::clamp(settings.ScaleFactor, 1, 16);
    const bool changed = requestedScale != ScaleFactor ||
        settings.BetterPolygons != BetterPolygons ||
        settings.HiresCoordinates != HiresCoordinates;
    ScaleFactor = requestedScale;
    BetterPolygons = settings.BetterPolygons;
    HiresCoordinates = settings.HiresCoordinates;
    if (changed)
    {
        ClearHighResolutionOutput();
        AdvanceOutputGeneration();
        Platform::Log(
            Platform::LogLevel::Info,
            "[MelonPrime] Vulkan runtime settings applied: requested_scale=%d "
            "compatibility_build_scale=%d better_polygons=%d "
            "hires_coordinates=%d\n",
            ScaleFactor,
            ResolveCompatibilityScale(ScaleFactor),
            BetterPolygons ? 1 : 0,
            HiresCoordinates ? 1 : 0);
    }
}

void VulkanRenderer::SwapBuffers()
{
    SoftRenderer::SwapBuffers();
    ++FrameSerial;
    if (FrameSerial == 0)
        ++FrameSerial;
    RebuildHighResolutionOutput();
}

void VulkanRenderer::OnRendered3DLine(u32 line, const u32* pixels) noexcept
{
    if (!pixels || line >= kNativeHeight ||
        Native3DFrame.size() != NativePixelCount)
    {
        return;
    }
    if (line == 0)
        std::fill(Native3DFrame.begin(), Native3DFrame.end(), 0);
    std::memcpy(
        Native3DFrame.data() + static_cast<std::size_t>(line) * kNativeWidth,
        pixels,
        kNativeWidth * sizeof(u32));
}

void VulkanRenderer::DrawScanline(u32 line)
{
    SoftRenderer::DrawScanline(line);
}

void VulkanRenderer::ClearHighResolutionOutput() noexcept
{
    std::lock_guard<std::mutex> lock(HighResolutionMutex);
    PublishedCompatibilityFrame.reset();
    LatestCompatibilityFrameSerial = 0;
    CompatibilityFrameProducerBusy.fill(false);
    NextCompatibilityFrameSlot = 0;
}

void VulkanRenderer::RebuildHighResolutionOutput()
{
    {
        std::lock_guard<std::mutex> lock(HighResolutionMutex);
        LatestCompatibilityFrameSerial = FrameSerial;
        if (ScaleFactor <= 1)
        {
            PublishedCompatibilityFrame.reset();
            return;
        }
    }

    void* topPointer = nullptr;
    void* bottomPointer = nullptr;
    if (!SoftRenderer::GetFramebuffers(&topPointer, &bottomPointer) ||
        !topPointer || !bottomPointer ||
        Native3DFrame.size() != NativePixelCount)
    {
        ClearHighResolutionOutput();
        return;
    }

    std::shared_ptr<VulkanCompatibilityFrame> frame;
    std::size_t frameSlot = CompatibilityFrameSlotCount;
    {
        std::lock_guard<std::mutex> lock(HighResolutionMutex);
        for (std::size_t attempt = 0;
             attempt < CompatibilityFrameSlotCount;
             ++attempt)
        {
            const std::size_t candidate =
                (NextCompatibilityFrameSlot + attempt) %
                CompatibilityFrameSlotCount;
            const auto& candidateFrame = CompatibilityFrames[candidate];
            if (!CompatibilityFrameProducerBusy[candidate] &&
                candidateFrame &&
                candidateFrame.use_count() == 1)
            {
                frameSlot = candidate;
                frame = candidateFrame;
                CompatibilityFrameProducerBusy[candidate] = true;
                NextCompatibilityFrameSlot =
                    (candidate + 1) % CompatibilityFrameSlotCount;
                break;
            }
        }

        if (!frame)
        {
            ++DroppedCompatibilityFrames;
            return;
        }
    }

    const auto releaseProducerSlot = [&](bool publish)
    {
        std::lock_guard<std::mutex> lock(HighResolutionMutex);
        CompatibilityFrameProducerBusy[frameSlot] = false;
        if (publish)
            PublishedCompatibilityFrame = frame;
    };

    try
    {
        const int requestedScale = std::clamp(ScaleFactor, 1, 16);
        const int scale = ResolveCompatibilityScale(requestedScale);
        const int engineAScreen = GPU.ScreenSwap ? 0 : 1;
        const auto* topSource = static_cast<const u32*>(topPointer);
        const auto* bottomSource = static_cast<const u32*>(bottomPointer);
        const u32* engineASource =
            engineAScreen == 0 ? topSource : bottomSource;

        if (engineAScreen == 0)
        {
            FastNearestUpscale(topSource, scale, frame->Top);
            frame->Bottom.assign(bottomSource, bottomSource + NativePixelCount);
            frame->TopWidth = static_cast<int>(kNativeWidth) * scale;
            frame->TopHeight = static_cast<int>(kNativeHeight) * scale;
            frame->BottomWidth = static_cast<int>(kNativeWidth);
            frame->BottomHeight = static_cast<int>(kNativeHeight);
        }
        else
        {
            frame->Top.assign(topSource, topSource + NativePixelCount);
            FastNearestUpscale(bottomSource, scale, frame->Bottom);
            frame->TopWidth = static_cast<int>(kNativeWidth);
            frame->TopHeight = static_cast<int>(kNativeHeight);
            frame->BottomWidth = static_cast<int>(kNativeWidth) * scale;
            frame->BottomHeight = static_cast<int>(kNativeHeight) * scale;
        }

        const u16 brightness = GPU.MasterBrightnessA;
        for (std::size_t index = 0; index < NativePixelCount; ++index)
        {
            const u32 native3D = Native3DFrame[index];
            const bool opaque = ((native3D >> 24) & 0x1Fu) != 0;
            const u32 expectedFinal = opaque
                ? ExpandRgb6ToBgra(ApplyEngineABrightness(native3D, brightness))
                : 0;
            const u32 finalNative = engineASource[index];

            Native3DBgra[index] = expectedFinal;
            Native3DVisible[index] =
                opaque &&
                ((expectedFinal & 0x00FFFFFFu) ==
                 (finalNative & 0x00FFFFFFu));
        }

        Vulkan::VulkanRomScaleSettings settings;
        settings.ScaleFactor = scale;
        settings.BetterPolygons = BetterPolygons;
        settings.HiresCoordinates = HiresCoordinates;

        // MELONPRIME_VULKAN_INTERNAL_RESOLUTION_VISIBLE_P7_V1
        // Coverage-only mode produced a larger image but repeated the same
        // native 3D color across every scale x scale block. Request the bridge's
        // scale-dependent subpixel color plane so 2x-4x visibly changes 3D output.
        settings.CoverageOnly = false;

        Vulkan::VulkanRomScaleResult bridge;
        if (!Vulkan::BuildVulkanRomScaleBridge(
                GPU.GPU3D,
                Native3DFrame.data(),
                settings,
                bridge) ||
            !bridge.Valid ||
            bridge.Width != static_cast<int>(kNativeWidth) * scale ||
            bridge.Height != static_cast<int>(kNativeHeight) * scale ||
            bridge.HighResolution3D.size() !=
                static_cast<std::size_t>(bridge.Width) *
                    static_cast<std::size_t>(bridge.Height) ||
            bridge.Coverage.size() !=
                static_cast<std::size_t>(bridge.Width) *
                    static_cast<std::size_t>(bridge.Height))
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "[MelonPrime] Vulkan high-resolution compatibility bridge failed: %s\n",
                bridge.FailureReason.c_str());
            releaseProducerSlot(false);
            return;
        }

        std::vector<u32>& engineAOutput =
            engineAScreen == 0 ? frame->Top : frame->Bottom;
        const int outputWidth = bridge.Width;

        for (int nativeY = 0;
             nativeY < static_cast<int>(kNativeHeight);
             ++nativeY)
        {
            for (int nativeX = 0;
                 nativeX < static_cast<int>(kNativeWidth);
                 ++nativeX)
            {
                const std::size_t nativeIndex =
                    static_cast<std::size_t>(nativeY) * kNativeWidth + nativeX;
                if (Native3DVisible[nativeIndex] == 0)
                    continue;

                const int firstX = nativeX * scale;
                const int firstY = nativeY * scale;
                for (int subY = 0; subY < scale; ++subY)
                {
                    const std::size_t row =
                        static_cast<std::size_t>(firstY + subY) * outputWidth;
                    const std::size_t first =
                        row + static_cast<std::size_t>(firstX);
                    for (int subX = 0; subX < scale; ++subX)
                    {
                        const std::size_t highIndex =
                            first + static_cast<std::size_t>(subX);
                        if (bridge.Coverage[highIndex] == 0)
                            continue;

                        const u32 highResolution3D =
                            bridge.HighResolution3D[highIndex];
                        if (((highResolution3D >> 24) & 0x1Fu) == 0)
                            continue;

                        engineAOutput[highIndex] = ExpandRgb6ToBgra(
                            ApplyEngineABrightness(highResolution3D, brightness));
                    }
                }
            }
        }

        frame->RequestedScale = requestedScale;
        frame->BuiltScale = scale;
        frame->EngineAScreen = engineAScreen;
        frame->FrameSerial = FrameSerial;
    }
    catch (const std::bad_alloc&)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[MelonPrime] Vulkan compatibility allocation failed; "
            "using native output for this frame\n");
        releaseProducerSlot(false);
        return;
    }

    releaseProducerSlot(true);
}

std::shared_ptr<const VulkanCompatibilityFrame>
VulkanRenderer::AcquireCompatibilityFrame() const
{
    std::lock_guard<std::mutex> lock(HighResolutionMutex);
    if (!PublishedCompatibilityFrame ||
        PublishedCompatibilityFrame->FrameSerial !=
            LatestCompatibilityFrameSerial)
    {
        return {};
    }
    return PublishedCompatibilityFrame;
}

RendererOutput VulkanRenderer::GetOutput()
{
    RendererOutput output = SoftRenderer::GetOutput();
    output.FrameSerial = FrameSerial;
    output.Generation = OutputGeneration;
    return output;
}

RendererOutputLease VulkanRenderer::AcquireOutputLease()
{
    return RendererOutputLease(GetOutput(), nullptr, nullptr);
}

} // namespace melonDS
