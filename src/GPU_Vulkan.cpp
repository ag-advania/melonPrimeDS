#include "GPU_Vulkan.h"

#include "GPU_ColorOp.h"
#include "GPU3D_TexcacheVulkan.h"
#include "Vulkan_RomScaleBridge.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
        if (!NativeRasterBuilder)
        {
            NativeRasterBuilder = std::make_unique<
                MelonPrime::Vulkan::NativeRasterSnapshotBuilder>();
        }
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
            "[MelonPrime] Vulkan renderer frame state allocation failed\n");
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
        "[MelonPrime] Vulkan native raster: "
        "presenter_device=1 scaled_render_target=1 packed_geometry=1 "
        "texture_cache=1 cpu_scale_squared_reconstruction=0\n");
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
    ClearNativeRasterFrame();
    ClearHighResolutionOutput();
    AdvanceOutputGeneration();
}

void VulkanRenderer::Stop()
{
    SoftRenderer::Stop();
    Initialized = false;
    std::fill(Native3DFrame.begin(), Native3DFrame.end(), 0);
    ClearNativeRasterFrame();
    NativeRasterBuilder.reset();
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
    ClearNativeRasterFrame();
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
        ClearNativeRasterFrame();
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

    // MELONPRIME_VULKAN_RENDERER_OWNED_RASTER_FRAME_V1
    // Snapshot live GPU3D/VRAM state on EmuThread at the renderer frame
    // boundary. The presenter only retains the immutable published frame.
    PublishNativeRasterFrame();
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
    // MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
    // Phase 7's CPU scale-squared reconstruction is deliberately retired.
    // High-resolution polygons are now rasterized by the presenter Vulkan
    // device directly into a scaled GPU render target.
    ClearHighResolutionOutput();
}


std::shared_ptr<const VulkanCompatibilityFrame>
VulkanRenderer::AcquireCompatibilityFrame() const
{
    // Native presenter raster owns ScaleFactor > 1 output. Returning no CPU
    // compatibility frame also guarantees copyHighResolutionScreens() cannot
    // silently reactivate the rejected bilinear reconstruction.
    return {};
}



bool VulkanRenderer::CopyNative3DForPresenter(
    std::vector<u32>& output) const
{
    // MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
    if (Native3DFrame.size() != NativePixelCount)
    {
        output.clear();
        return false;
    }
    output.assign(Native3DFrame.begin(), Native3DFrame.end());
    return true;
}

void VulkanRenderer::PublishNativeRasterFrame()
{
    if (!NativeRasterBuilder)
    {
        ClearNativeRasterFrame();
        return;
    }

    try
    {
        auto frame = std::make_shared<MelonPrime::Vulkan::NativeRasterFrame>();
        if (!NativeRasterBuilder->Build(*this, GPU, *frame) || !frame->Valid)
        {
            ClearNativeRasterFrame();
            return;
        }

        std::lock_guard<std::mutex> lock(NativeRasterMutex);
        PublishedNativeRasterFrame = std::move(frame);
    }
    catch (const std::bad_alloc&)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[MelonPrime] Vulkan native raster frame allocation failed\n");
        ClearNativeRasterFrame();
    }
}

void VulkanRenderer::ClearNativeRasterFrame() noexcept
{
    std::lock_guard<std::mutex> lock(NativeRasterMutex);
    PublishedNativeRasterFrame.reset();
}

std::shared_ptr<const MelonPrime::Vulkan::NativeRasterFrame>
VulkanRenderer::AcquireNativeRasterFrame() const
{
    std::lock_guard<std::mutex> lock(NativeRasterMutex);
    return PublishedNativeRasterFrame;
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

namespace MelonPrime::Vulkan
{
namespace
{

constexpr std::size_t kRasterNativePixels = 256u * 192u;
constexpr std::size_t kRasterClearBitmapPixels = 256u * 256u;

std::uint64_t NativeTextureIdentity(
    const melonDS::Vulkan::VulkanTextureCacheKey& key) noexcept
{
    std::uint64_t hash = 1469598103934665603ull;
    const std::uint32_t words[] = {
        key.TexParam,
        key.TexPalette,
        key.Width,
        key.Height,
        key.Format,
        key.TextureAddress,
        key.PaletteAddress,
        key.Color0Transparent,
    };
    for (std::uint32_t word : words)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            hash ^= static_cast<std::uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::uint32_t NativeTextureSamplerIndex(std::uint32_t texParam) noexcept
{
    const auto s = melonDS::Vulkan::DecodeVulkanTextureSamplerAxis(
        texParam, false);
    const auto t = melonDS::Vulkan::DecodeVulkanTextureSamplerAxis(
        texParam, true);
    return melonDS::Vulkan::VulkanTextureSamplerTableIndex(s, t);
}

std::uint32_t ExpandNativeRgb6a5(std::uint32_t pixel) noexcept
{
    const std::uint32_t r6 = pixel & 0x3Fu;
    const std::uint32_t g6 = (pixel >> 8u) & 0x3Fu;
    const std::uint32_t b6 = (pixel >> 16u) & 0x3Fu;
    const std::uint32_t a5 = (pixel >> 24u) & 0x1Fu;
    const std::uint32_t r8 = (r6 << 2u) | (r6 >> 4u);
    const std::uint32_t g8 = (g6 << 2u) | (g6 >> 4u);
    const std::uint32_t b8 = (b6 << 2u) | (b6 >> 4u);
    const std::uint32_t a8 = (a5 * 255u + 15u) / 31u;
    return (a8 << 24u) | (r8 << 16u) | (g8 << 8u) | b8;
}

} // namespace

void NativeRasterFrame::Clear() noexcept
{
    Valid = false;
    Scale = 1;
    EngineAScreen = 0;
    MasterBrightnessA = 0;
    RenderDispCnt = 0;
    RenderClearAttr1 = 0;
    RenderClearAttr2 = 0;
    RenderAlphaRef = 0;
    RenderXPos = 0;
    RenderFogColor = 0;
    RenderFogOffset = 0;
    RenderFogShift = 0;
    RenderFogDensityTable.fill(0);
    RenderEdgeTable.fill(0);
    RenderToonTable.fill(0);
    FrameSerial = 0;
    Generation = 0;
    Upload.Clear();
    Textures.clear();
    ClearBitmapColorRgba6a5.clear();
    ClearBitmapDepthFog.clear();
    NativeReferenceBgra.clear();
}

struct NativeRasterSnapshotBuilder::Impl
{
    struct CachedTexture
    {
        std::uint64_t ContentHash = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::shared_ptr<const std::vector<std::uint32_t>> Pixels;
    };

    std::unordered_map<std::uint64_t, CachedTexture> Cache;
};

NativeRasterSnapshotBuilder::NativeRasterSnapshotBuilder()
    : m_impl(std::make_unique<Impl>())
{
}

NativeRasterSnapshotBuilder::~NativeRasterSnapshotBuilder() = default;

bool NativeRasterSnapshotBuilder::Build(
    const melonDS::VulkanRenderer& renderer,
    melonDS::GPU& gpu,
    NativeRasterFrame& frame)
{
    frame.Clear();
    const int scale = std::clamp(renderer.GetRecordedScaleFactor(), 1, 16);

    std::vector<melonDS::u32> native3D;
    if (!renderer.CopyNative3DForPresenter(native3D) ||
        native3D.size() != kRasterNativePixels)
    {
        return false;
    }

    auto textureDirty = gpu.VRAMDirty_Texture.DeriveState(
        gpu.VRAMMap_Texture, gpu);
    auto paletteDirty = gpu.VRAMDirty_TexPal.DeriveState(
        gpu.VRAMMap_TexPal, gpu);
    gpu.MakeVRAMFlat_TextureCoherent(textureDirty);
    gpu.MakeVRAMFlat_TexPalCoherent(paletteDirty);

    melonDS::Vulkan::VulkanTextureMemoryView memory;
    memory.Texture = gpu.VRAMFlat_Texture;
    memory.TextureSize = sizeof(gpu.VRAMFlat_Texture);
    memory.Palette = gpu.VRAMFlat_TexPal;
    memory.PaletteSize = sizeof(gpu.VRAMFlat_TexPal);

    const auto& gpu3D = gpu.GPU3D;
    const bool textureMapsEnabled =
        (gpu3D.RenderDispCnt & (1u << 0u)) != 0u;

    std::vector<std::uint32_t> textureLayers(
        gpu3D.RenderNumPolygons, 0xFFFFFFFFu);
    std::unordered_map<std::uint64_t, std::uint32_t> frameTextureIndices;
    std::unordered_set<std::uint64_t> activeTextureIdentities;

    static const auto whiteTexture = [] {
        auto pixels = std::make_shared<std::vector<std::uint32_t>>();
        pixels->push_back(0x1F3F3F3Fu);
        return std::shared_ptr<const std::vector<std::uint32_t>>(pixels);
    }();
    frame.Textures.push_back({
        0x4D504E4154495645ull,
        0x1F3F3F3Full,
        1,
        1,
        0,
        whiteTexture,
    });

    for (std::uint32_t sourceOrder = 0;
         sourceOrder < gpu3D.RenderNumPolygons;
         ++sourceOrder)
    {
        const melonDS::Polygon* polygon = gpu3D.RenderPolygonRAM[sourceOrder];
        if (!polygon || polygon->Degenerate)
            continue;
        const std::uint32_t format = (polygon->TexParam >> 26u) & 0x7u;
        if (format == 0 || !textureMapsEnabled)
            continue;

        const auto key = melonDS::Vulkan::BuildVulkanTextureCacheKey(
            polygon->TexParam, polygon->TexPalette);
        const std::uint64_t identity = NativeTextureIdentity(key);
        activeTextureIdentities.insert(identity);
        const std::uint32_t samplerIndex =
            NativeTextureSamplerIndex(polygon->TexParam);
        const std::uint64_t bindingIdentity = identity ^
            (static_cast<std::uint64_t>(samplerIndex + 1u) *
             0xD6E8FEB86659FD93ull);
        const auto existing = frameTextureIndices.find(bindingIdentity);
        if (existing != frameTextureIndices.end())
        {
            textureLayers[sourceOrder] = existing->second;
            continue;
        }

        melonDS::Vulkan::VulkanTextureDecodeFootprint footprint;
        std::string failure;
        if (!melonDS::Vulkan::BuildVulkanTextureDecodeFootprint(
                key, footprint, &failure))
        {
            // Rendering without a required texture would corrupt both color
            // and depth, so leave this snapshot unavailable for fallback.
            frame.Clear();
            return true;
        }
        const auto hashes = melonDS::Vulkan::HashVulkanTextureDecodeInput(
            footprint, memory);

        auto& cached = m_impl->Cache[identity];
        if (!cached.Pixels || cached.ContentHash != hashes.Combined)
        {
            auto pixels = std::make_shared<std::vector<std::uint32_t>>();
            if (!melonDS::Vulkan::DecodeVulkanTextureRgb6a5(
                    footprint, memory, *pixels, &failure))
            {
                m_impl->Cache.erase(identity);
                frame.Clear();
                return true;
            }
            cached.ContentHash = hashes.Combined;
            cached.Width = key.Width;
            cached.Height = key.Height;
            cached.Pixels = std::move(pixels);
        }

        const std::uint32_t textureIndex =
            static_cast<std::uint32_t>(frame.Textures.size());
        frameTextureIndices.emplace(bindingIdentity, textureIndex);
        textureLayers[sourceOrder] = textureIndex;
        frame.Textures.push_back({
            identity,
            cached.ContentHash,
            cached.Width,
            cached.Height,
            samplerIndex,
            cached.Pixels,
        });
    }

    for (auto item = m_impl->Cache.begin(); item != m_impl->Cache.end();)
    {
        if (activeTextureIdentities.find(item->first) ==
            activeTextureIdentities.end())
        {
            item = m_impl->Cache.erase(item);
        }
        else
        {
            ++item;
        }
    }

    melonDS::Vulkan::VulkanRasterBuildOptions options;
    options.ScaleFactor = scale;
    options.BetterPolygons = renderer.GetRecordedBetterPolygons();
    // Sapphire keeps a small passive expansion on repeat-textured polygons.
    // Without it, subpixel high-resolution coordinates expose cracks between
    // floor and wall triangles even when the optional user coverage fix is off.
    options.PassiveRepeatCoveragePixels = 0.2f;
    options.TextureLayers = textureLayers.data();
    options.TextureLayerCount = textureLayers.size();
    std::string failure;
    if (!melonDS::Vulkan::BuildVulkanAcceleratedRasterUpload(
            gpu3D,
            options,
            frame.Upload,
            &failure) ||
        !frame.Upload.Valid)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Warn,
            "[MelonPrime] Vulkan native raster snapshot failed: %s\n",
            failure.empty() ? "unknown reason" : failure.c_str());
        frame.Clear();
        return false;
    }

    frame.NativeReferenceBgra.resize(kRasterNativePixels);
    std::transform(
        native3D.begin(),
        native3D.end(),
        frame.NativeReferenceBgra.begin(),
        ExpandNativeRgb6a5);

    if ((gpu3D.RenderDispCnt & (1u << 14u)) != 0)
    {
        const auto* colorVram = reinterpret_cast<const std::uint16_t*>(
            gpu.VRAMFlat_Texture + 0x40000u);
        const auto* depthVram = reinterpret_cast<const std::uint16_t*>(
            gpu.VRAMFlat_Texture + 0x60000u);
        frame.ClearBitmapColorRgba6a5.resize(kRasterClearBitmapPixels);
        frame.ClearBitmapDepthFog.resize(kRasterClearBitmapPixels);
        for (std::size_t pixel = 0;
             pixel < kRasterClearBitmapPixels;
             ++pixel)
        {
            const auto color = melonDS::Vulkan::DecodeClearBitmapColorTexel(
                colorVram[pixel]);
            frame.ClearBitmapColorRgba6a5[pixel] =
                static_cast<std::uint32_t>(color[0]) |
                (static_cast<std::uint32_t>(color[1]) << 8u) |
                (static_cast<std::uint32_t>(color[2]) << 16u) |
                (static_cast<std::uint32_t>(color[3]) << 24u);
            frame.ClearBitmapDepthFog[pixel] =
                melonDS::Vulkan::DecodeClearBitmapDepthTexel(depthVram[pixel]);
        }
    }

    frame.Scale = scale;
    frame.EngineAScreen = gpu.ScreenSwap ? 0u : 1u;
    frame.MasterBrightnessA = gpu.MasterBrightnessA;
    frame.RenderDispCnt = gpu3D.RenderDispCnt;
    frame.RenderClearAttr1 = gpu3D.RenderClearAttr1;
    frame.RenderClearAttr2 = gpu3D.RenderClearAttr2;
    frame.RenderAlphaRef = gpu3D.RenderAlphaRef;
    frame.RenderXPos = gpu3D.GetRenderXPos();
    frame.RenderFogColor = gpu3D.RenderFogColor;
    frame.RenderFogOffset = gpu3D.RenderFogOffset;
    frame.RenderFogShift = gpu3D.RenderFogShift;
    std::copy_n(
        gpu3D.RenderFogDensityTable,
        frame.RenderFogDensityTable.size(),
        frame.RenderFogDensityTable.begin());
    std::copy_n(
        gpu3D.RenderEdgeTable,
        frame.RenderEdgeTable.size(),
        frame.RenderEdgeTable.begin());
    std::copy_n(
        gpu3D.RenderToonTable,
        frame.RenderToonTable.size(),
        frame.RenderToonTable.begin());
    frame.FrameSerial = renderer.GetFrameSerialForDiagnostics();
    frame.Generation = renderer.GetOutputGenerationForDiagnostics();
    frame.Valid = frame.FrameSerial != 0 && frame.Generation != 0;
    return true;
}

} // namespace MelonPrime::Vulkan
