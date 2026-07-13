#include "MelonPrimeVulkanNativeRaster.h"

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1

#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "GPU.h"
#include "GPU3D.h"
#include "GPU3D_TexcacheVulkan.h"
#include "GPU_Vulkan.h"
#include "Platform.h"
#include "../../../Vulkan_shaders/generated/VulkanShaders.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Platform::Log;
using melonDS::Platform::LogLevel;

constexpr std::uint32_t kNativeWidth = 256;
constexpr std::uint32_t kNativeHeight = 192;
constexpr std::size_t kNativePixels =
    static_cast<std::size_t>(kNativeWidth) * kNativeHeight;
constexpr std::uint32_t kClearBitmapWidth = 256;
constexpr std::uint32_t kClearBitmapHeight = 256;
constexpr std::size_t kClearBitmapPixels =
    static_cast<std::size_t>(kClearBitmapWidth) * kClearBitmapHeight;
constexpr VkFormat kColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UINT;
constexpr std::size_t kMaxResidentTextureIdentities = 128;
constexpr std::uint64_t kMaxResidentTextureBytes = 256ull * 1024ull * 1024ull;

struct alignas(16) NativeRasterToonUniform
{
    std::array<std::array<std::uint32_t, 4>, 32> Colors{};
};

static_assert(sizeof(NativeRasterToonUniform) == 32u * 16u);

// Matches Sapphire's 128-byte graphics final-pass push ABI.
struct alignas(16) NativeRasterFinalPush
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t ClearColor = 0;
    std::uint32_t ClearDepth = 0;
    std::uint32_t TriangleCount = 0;
    std::uint32_t DispCnt = 0;
    std::uint32_t AlphaRef = 0;
    std::uint32_t FogColor = 0;
    std::uint32_t FogOffset = 0;
    std::uint32_t FogShift = 0;
    std::uint32_t ClearAttr = 0;
    std::array<std::uint32_t, 9> FogDensityPacked{};
    std::array<std::uint32_t, 8> EdgeColorPacked{};
    std::uint32_t VariantKey = 0;
    std::uint32_t PassIndex = 0;
    std::uint32_t TriangleBase = 0;
    std::uint32_t DepthBlendMode = 0;
};

static_assert(sizeof(NativeRasterFinalPush) == 128u);

VkImageAspectFlags DepthAspectMask(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
}

std::uint64_t TextureIdentity(
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

std::uint32_t TextureSamplerIndex(std::uint32_t texParam) noexcept
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

std::uint32_t FindMemoryType(
    QVulkanWindow* window,
    std::uint32_t bits,
    VkMemoryPropertyFlags required) noexcept
{
    if (!window || !window->vulkanInstance())
        return std::numeric_limits<std::uint32_t>::max();
    VkPhysicalDeviceMemoryProperties properties{};
    window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
        window->physicalDevice(), &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((bits & (1u << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) == required)
        {
            return index;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

struct HostBuffer
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    void* Map = nullptr;
    VkDeviceSize Capacity = 0;
};

void DestroyHostBuffer(
    QVulkanDeviceFunctions* functions,
    VkDevice device,
    HostBuffer& buffer) noexcept
{
    if (!functions || !device)
        return;
    if (buffer.Map && buffer.Memory)
        functions->vkUnmapMemory(device, buffer.Memory);
    if (buffer.Buffer)
        functions->vkDestroyBuffer(device, buffer.Buffer, nullptr);
    if (buffer.Memory)
        functions->vkFreeMemory(device, buffer.Memory, nullptr);
    buffer = {};
}

bool EnsureHostBuffer(
    QVulkanWindow* window,
    QVulkanDeviceFunctions* functions,
    VkDevice device,
    VkDeviceSize required,
    VkBufferUsageFlags usage,
    HostBuffer& buffer)
{
    required = std::max<VkDeviceSize>(required, 4);
    if (buffer.Buffer && buffer.Capacity >= required)
        return true;

    DestroyHostBuffer(functions, device, buffer);
    VkDeviceSize capacity = 64u * 1024u;
    while (capacity < required)
        capacity *= 2u;

    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = capacity;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = functions->vkCreateBuffer(
        device, &info, nullptr, &buffer.Buffer);
    if (result != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    functions->vkGetBufferMemoryRequirements(
        device, buffer.Buffer, &requirements);
    const std::uint32_t memoryType = FindMemoryType(
        window,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memoryType == std::numeric_limits<std::uint32_t>::max())
        return false;

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result = functions->vkAllocateMemory(
        device, &allocation, nullptr, &buffer.Memory);
    if (result == VK_SUCCESS)
        result = functions->vkBindBufferMemory(
            device, buffer.Buffer, buffer.Memory, 0);
    if (result == VK_SUCCESS)
    {
        result = functions->vkMapMemory(
            device, buffer.Memory, 0, capacity, 0, &buffer.Map);
    }
    if (result != VK_SUCCESS)
        return false;
    buffer.Capacity = capacity;
    return true;
}

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
};

void DestroyImage(
    QVulkanDeviceFunctions* functions,
    VkDevice device,
    ImageResource& image) noexcept
{
    if (!functions || !device)
        return;
    if (image.View)
        functions->vkDestroyImageView(device, image.View, nullptr);
    if (image.Image)
        functions->vkDestroyImage(device, image.Image, nullptr);
    if (image.Memory)
        functions->vkFreeMemory(device, image.Memory, nullptr);
    image = {};
}

bool CreateImage(
    QVulkanWindow* window,
    QVulkanDeviceFunctions* functions,
    VkDevice device,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    VkImageViewType viewType,
    ImageResource& output)
{
    DestroyImage(functions, device, output);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = functions->vkCreateImage(
        device, &info, nullptr, &output.Image);
    if (result != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements{};
    functions->vkGetImageMemoryRequirements(
        device, output.Image, &requirements);
    const std::uint32_t memoryType = FindMemoryType(
        window,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == std::numeric_limits<std::uint32_t>::max())
        return false;

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    result = functions->vkAllocateMemory(
        device, &allocation, nullptr, &output.Memory);
    if (result == VK_SUCCESS)
        result = functions->vkBindImageMemory(
            device, output.Image, output.Memory, 0);
    if (result != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = output.Image;
    view.viewType = viewType;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    result = functions->vkCreateImageView(
        device, &view, nullptr, &output.View);
    if (result != VK_SUCCESS)
        return false;

    output.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    output.Format = format;
    output.Extent = extent;
    return true;
}

void TransitionImage(
    QVulkanDeviceFunctions* functions,
    VkCommandBuffer command,
    ImageResource& image,
    VkImageLayout next,
    VkPipelineStageFlags sourceStage,
    VkPipelineStageFlags destinationStage,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    VkImageAspectFlags aspect)
{
    if (image.Layout == next)
        return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = image.Layout;
    barrier.newLayout = next;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.Image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    functions->vkCmdPipelineBarrier(
        command,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
    image.Layout = next;
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
    if (scale <= 1)
        return true;

    std::vector<melonDS::u32> native3D;
    if (!renderer.CopyNative3DForPresenter(native3D) ||
        native3D.size() != kNativePixels)
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

    // Binding zero must remain valid for untextured draws. A one-texel opaque
    // white RGB6A5 texture is multiplication-neutral and never sampled when
    // the shader's textured flag is zero.
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
        // Sapphire treats the DISP3DCNT texture-mapping bit as part of each
        // draw's texture state. Keep the polygon in the native raster when
        // mapping is disabled, but leave its texture layer invalid so the
        // shared upload marks it untextured instead of dropping the frame.
        if (format == 0 || !textureMapsEnabled)
            continue;

        const auto key = melonDS::Vulkan::BuildVulkanTextureCacheKey(
            polygon->TexParam, polygon->TexPalette);
        const std::uint64_t identity = TextureIdentity(key);
        activeTextureIdentities.insert(identity);
        const std::uint32_t samplerIndex =
            TextureSamplerIndex(polygon->TexParam);
        const std::uint64_t bindingIdentity = identity ^
            (static_cast<std::uint64_t>(samplerIndex + 1u) *
             0xD6E8FEB86659FD93ull);
        auto existing = frameTextureIndices.find(bindingIdentity);
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
            // Rendering this polygon without its texture would make the
            // high-resolution depth result disagree with the native frame.
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

    // CPU decoded pixels are retained only for identities used by the current
    // frame. NativeRasterFrame owns shared references until presentation is
    // finished, so removing stale cache entries cannot invalidate a snapshot.
    for (auto item = m_impl->Cache.begin(); item != m_impl->Cache.end();)
    {
        if (activeTextureIdentities.find(item->first) == activeTextureIdentities.end())
            item = m_impl->Cache.erase(item);
        else
            ++item;
    }

    melonDS::Vulkan::VulkanRasterBuildOptions options;
    options.ScaleFactor = scale;
    options.BetterPolygons = renderer.GetRecordedBetterPolygons();
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
        Log(LogLevel::Warn,
            "[MelonPrime] Vulkan native raster snapshot failed: %s\n",
            failure.empty() ? "unknown reason" : failure.c_str());
        frame.Clear();
        return false;
    }

    frame.NativeReferenceBgra.resize(kNativePixels);
    std::transform(
        native3D.begin(),
        native3D.end(),
        frame.NativeReferenceBgra.begin(),
        ExpandNativeRgb6a5);

    if ((gpu3D.RenderDispCnt & (1u << 14u)) != 0)
    {
        // Sapphire/OpenGL source the clear bitmap from texture slots 2 and 3.
        // Keep the decoded integer texels in the immutable frame snapshot so
        // the presentation thread never reads emulation VRAM directly.
        const auto* colorVram = reinterpret_cast<const std::uint16_t*>(
            gpu.VRAMFlat_Texture + 0x40000u);
        const auto* depthVram = reinterpret_cast<const std::uint16_t*>(
            gpu.VRAMFlat_Texture + 0x60000u);
        frame.ClearBitmapColorRgba6a5.resize(kClearBitmapPixels);
        frame.ClearBitmapDepthFog.resize(kClearBitmapPixels);
        for (std::size_t pixel = 0; pixel < kClearBitmapPixels; ++pixel)
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

struct NativeRasterGpu::Impl
{
    struct Slot
    {
        ImageResource Color;
        ImageResource Attributes;
        ImageResource DepthValue;
        ImageResource Depth;
        ImageResource NativeReference;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        VkFramebuffer FinalFramebuffer = VK_NULL_HANDLE;
        HostBuffer Vertex;
        HostBuffer Index;
        HostBuffer EdgeIndex;
        HostBuffer ToonTable;
        HostBuffer NativeReferenceStaging;
        ImageResource ClearBitmapColor;
        ImageResource ClearBitmapDepth;
        HostBuffer ClearBitmapColorStaging;
        HostBuffer ClearBitmapDepthStaging;
        VkDescriptorSet ClearBitmapDescriptor = VK_NULL_HANDLE;
        VkDescriptorSet FinalDescriptor = VK_NULL_HANDLE;
        std::uint64_t FrameSerial = 0;
        std::uint64_t Generation = 0;
    };

    struct TextureVersion
    {
        ImageResource Image;
        HostBuffer Staging;
        std::uint64_t ContentHash = 0;
        std::array<VkDescriptorSet, 9> DescriptorSets{};
    };

    struct TextureResource
    {
        std::array<TextureVersion, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> Versions{};
    };

    QVulkanWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkRenderPass FinalRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ClearBitmapDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout ClearBitmapPipelineLayout = VK_NULL_HANDLE;
    VkPipeline ClearBitmapPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout FinalDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout FinalPipelineLayout = VK_NULL_HANDLE;
    VkPipeline EdgeMarkPipeline = VK_NULL_HANDLE;
    VkPipeline FinalEdgePipeline = VK_NULL_HANDLE;
    VkPipeline FinalFogPipeline = VK_NULL_HANDLE;
    VkPipeline FinalFogAlphaPipeline = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkPipeline LinePipeline = VK_NULL_HANDLE;
    VkPipeline DepthEqualPipeline = VK_NULL_HANDLE;
    VkPipeline DepthEqualLinePipeline = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> TranslucentPipelines{};
    std::array<VkPipeline, 4> TranslucentLinePipelines{};
    std::array<VkPipeline, 4> DepthEqualTranslucentPipelines{};
    std::array<VkPipeline, 4> DepthEqualTranslucentLinePipelines{};
    std::array<VkPipeline, 4> BgZeroTranslucentPipelines{};
    std::array<VkPipeline, 4> BgZeroTranslucentLinePipelines{};
    std::array<VkPipeline, 8> BgZeroShadowBlendPipelines{};
    std::array<VkPipeline, 8> BgZeroShadowBlendLinePipelines{};
    VkPipeline StencilBitClearPipeline = VK_NULL_HANDLE;
    VkPipeline ShadowMaskPipeline = VK_NULL_HANDLE;
    VkPipeline ShadowMaskLinePipeline = VK_NULL_HANDLE;
    VkPipeline BgZeroShadowMaskPipeline = VK_NULL_HANDLE;
    VkPipeline BgZeroShadowMaskLinePipeline = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> ShadowClearPipelines{};
    std::array<VkPipeline, 2> ShadowClearLinePipelines{};
    std::array<VkPipeline, 4> ShadowBlendPipelines{};
    std::array<VkPipeline, 4> ShadowBlendLinePipelines{};
    std::array<VkPipeline, 4> DepthEqualShadowBlendPipelines{};
    std::array<VkPipeline, 4> DepthEqualShadowBlendLinePipelines{};
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkSampler, 9> Samplers{};
    std::array<Slot, QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT> Slots{};
    std::unordered_map<std::uint64_t, std::unique_ptr<TextureResource>> Textures;
    int FrameCount = 0;
    int Scale = 0;
    bool TargetsReady = false;
    bool Ready = false;

    void DestroySlot(Slot& slot)
    {
        if (slot.Framebuffer)
            Functions->vkDestroyFramebuffer(Device, slot.Framebuffer, nullptr);
        slot.Framebuffer = VK_NULL_HANDLE;
        if (slot.FinalFramebuffer)
            Functions->vkDestroyFramebuffer(Device, slot.FinalFramebuffer, nullptr);
        slot.FinalFramebuffer = VK_NULL_HANDLE;
        if (slot.ClearBitmapDescriptor && DescriptorPool)
            Functions->vkFreeDescriptorSets(
                Device, DescriptorPool, 1, &slot.ClearBitmapDescriptor);
        slot.ClearBitmapDescriptor = VK_NULL_HANDLE;
        if (slot.FinalDescriptor && DescriptorPool)
            Functions->vkFreeDescriptorSets(
                Device, DescriptorPool, 1, &slot.FinalDescriptor);
        slot.FinalDescriptor = VK_NULL_HANDLE;
        DestroyImage(Functions, Device, slot.Color);
        DestroyImage(Functions, Device, slot.Attributes);
        DestroyImage(Functions, Device, slot.DepthValue);
        DestroyImage(Functions, Device, slot.Depth);
        DestroyImage(Functions, Device, slot.NativeReference);
        DestroyImage(Functions, Device, slot.ClearBitmapColor);
        DestroyImage(Functions, Device, slot.ClearBitmapDepth);
        DestroyHostBuffer(Functions, Device, slot.Vertex);
        DestroyHostBuffer(Functions, Device, slot.Index);
        DestroyHostBuffer(Functions, Device, slot.EdgeIndex);
        DestroyHostBuffer(Functions, Device, slot.ToonTable);
        DestroyHostBuffer(Functions, Device, slot.NativeReferenceStaging);
        DestroyHostBuffer(Functions, Device, slot.ClearBitmapColorStaging);
        DestroyHostBuffer(Functions, Device, slot.ClearBitmapDepthStaging);
        slot.FrameSerial = 0;
        slot.Generation = 0;
    }

    void DestroyTargets()
    {
        if (!Functions || !Device)
            return;
        for (auto& slot : Slots)
            DestroySlot(slot);
        Scale = 0;
        FrameCount = 0;
        TargetsReady = false;
    }

    void DestroyTextureVersion(TextureVersion& version)
    {
        std::array<VkDescriptorSet, 9> allocated{};
        std::uint32_t count = 0;
        for (VkDescriptorSet descriptor : version.DescriptorSets)
        {
            if (descriptor)
                allocated[count++] = descriptor;
        }
        if (count && DescriptorPool)
            Functions->vkFreeDescriptorSets(Device, DescriptorPool, count, allocated.data());
        DestroyImage(Functions, Device, version.Image);
        DestroyHostBuffer(Functions, Device, version.Staging);
        version.ContentHash = 0;
        version.DescriptorSets.fill(VK_NULL_HANDLE);
    }

    void DestroyTexture(TextureResource& texture)
    {
        for (auto& version : texture.Versions)
            DestroyTextureVersion(version);
    }

    void ClearTextureCache(bool waitForIdle)
    {
        if (waitForIdle)
            Functions->vkDeviceWaitIdle(Device);
        for (auto& item : Textures)
            DestroyTexture(*item.second);
        Textures.clear();
    }

    void Release()
    {
        if (!Functions || !Device)
        {
            Window = nullptr;
            Functions = nullptr;
            Device = VK_NULL_HANDLE;
            Ready = false;
            return;
        }
        Functions->vkDeviceWaitIdle(Device);
        DestroyTargets();
        ClearTextureCache(false);
        if (Pipeline)
            Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
        if (LinePipeline)
            Functions->vkDestroyPipeline(Device, LinePipeline, nullptr);
        if (DepthEqualPipeline)
            Functions->vkDestroyPipeline(Device, DepthEqualPipeline, nullptr);
        if (DepthEqualLinePipeline)
            Functions->vkDestroyPipeline(Device, DepthEqualLinePipeline, nullptr);
        for (VkPipeline pipeline : TranslucentPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : TranslucentLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : DepthEqualTranslucentPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : DepthEqualTranslucentLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : BgZeroTranslucentPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : BgZeroTranslucentLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : BgZeroShadowBlendPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : BgZeroShadowBlendLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        if (StencilBitClearPipeline)
            Functions->vkDestroyPipeline(Device, StencilBitClearPipeline, nullptr);
        if (ShadowMaskPipeline)
            Functions->vkDestroyPipeline(Device, ShadowMaskPipeline, nullptr);
        if (ShadowMaskLinePipeline)
            Functions->vkDestroyPipeline(Device, ShadowMaskLinePipeline, nullptr);
        if (BgZeroShadowMaskPipeline)
            Functions->vkDestroyPipeline(Device, BgZeroShadowMaskPipeline, nullptr);
        if (BgZeroShadowMaskLinePipeline)
            Functions->vkDestroyPipeline(Device, BgZeroShadowMaskLinePipeline, nullptr);
        for (VkPipeline pipeline : ShadowClearPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : ShadowClearLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : ShadowBlendPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : ShadowBlendLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : DepthEqualShadowBlendPipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        for (VkPipeline pipeline : DepthEqualShadowBlendLinePipelines)
            if (pipeline)
                Functions->vkDestroyPipeline(Device, pipeline, nullptr);
        if (EdgeMarkPipeline)
            Functions->vkDestroyPipeline(Device, EdgeMarkPipeline, nullptr);
        if (FinalEdgePipeline)
            Functions->vkDestroyPipeline(Device, FinalEdgePipeline, nullptr);
        if (FinalFogPipeline)
            Functions->vkDestroyPipeline(Device, FinalFogPipeline, nullptr);
        if (FinalFogAlphaPipeline)
            Functions->vkDestroyPipeline(Device, FinalFogAlphaPipeline, nullptr);
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (ClearBitmapPipeline)
            Functions->vkDestroyPipeline(Device, ClearBitmapPipeline, nullptr);
        if (ClearBitmapPipelineLayout)
            Functions->vkDestroyPipelineLayout(
                Device, ClearBitmapPipelineLayout, nullptr);
        if (FinalPipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, FinalPipelineLayout, nullptr);
        if (DescriptorPool)
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        if (ClearBitmapDescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(
                Device, ClearBitmapDescriptorLayout, nullptr);
        if (FinalDescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(
                Device, FinalDescriptorLayout, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        if (FinalRenderPass)
            Functions->vkDestroyRenderPass(Device, FinalRenderPass, nullptr);
        for (VkSampler sampler : Samplers)
            if (sampler)
                Functions->vkDestroySampler(Device, sampler, nullptr);
        Pipeline = VK_NULL_HANDLE;
        LinePipeline = VK_NULL_HANDLE;
        DepthEqualPipeline = VK_NULL_HANDLE;
        DepthEqualLinePipeline = VK_NULL_HANDLE;
        TranslucentPipelines.fill(VK_NULL_HANDLE);
        TranslucentLinePipelines.fill(VK_NULL_HANDLE);
        DepthEqualTranslucentPipelines.fill(VK_NULL_HANDLE);
        DepthEqualTranslucentLinePipelines.fill(VK_NULL_HANDLE);
        BgZeroTranslucentPipelines.fill(VK_NULL_HANDLE);
        BgZeroTranslucentLinePipelines.fill(VK_NULL_HANDLE);
        BgZeroShadowBlendPipelines.fill(VK_NULL_HANDLE);
        BgZeroShadowBlendLinePipelines.fill(VK_NULL_HANDLE);
        StencilBitClearPipeline = VK_NULL_HANDLE;
        ShadowMaskPipeline = VK_NULL_HANDLE;
        ShadowMaskLinePipeline = VK_NULL_HANDLE;
        BgZeroShadowMaskPipeline = VK_NULL_HANDLE;
        BgZeroShadowMaskLinePipeline = VK_NULL_HANDLE;
        ShadowClearPipelines.fill(VK_NULL_HANDLE);
        ShadowClearLinePipelines.fill(VK_NULL_HANDLE);
        ShadowBlendPipelines.fill(VK_NULL_HANDLE);
        ShadowBlendLinePipelines.fill(VK_NULL_HANDLE);
        DepthEqualShadowBlendPipelines.fill(VK_NULL_HANDLE);
        DepthEqualShadowBlendLinePipelines.fill(VK_NULL_HANDLE);
        EdgeMarkPipeline = VK_NULL_HANDLE;
        FinalEdgePipeline = VK_NULL_HANDLE;
        FinalFogPipeline = VK_NULL_HANDLE;
        FinalFogAlphaPipeline = VK_NULL_HANDLE;
        PipelineLayout = VK_NULL_HANDLE;
        ClearBitmapPipeline = VK_NULL_HANDLE;
        ClearBitmapPipelineLayout = VK_NULL_HANDLE;
        FinalPipelineLayout = VK_NULL_HANDLE;
        DescriptorPool = VK_NULL_HANDLE;
        DescriptorLayout = VK_NULL_HANDLE;
        ClearBitmapDescriptorLayout = VK_NULL_HANDLE;
        FinalDescriptorLayout = VK_NULL_HANDLE;
        RenderPass = VK_NULL_HANDLE;
        FinalRenderPass = VK_NULL_HANDLE;
        Samplers.fill(VK_NULL_HANDLE);
        Ready = false;
        Window = nullptr;
        Functions = nullptr;
        Device = VK_NULL_HANDLE;
        DepthFormat = VK_FORMAT_UNDEFINED;
    }

    bool CreateShader(
        const std::uint32_t* words,
        std::size_t bytes,
        VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes;
        info.pCode = words;
        return Functions->vkCreateShaderModule(
            Device, &info, nullptr, &module) == VK_SUCCESS;
    }

    bool Initialize(QVulkanWindow* window, QVulkanDeviceFunctions* functions)
    {
        if (Ready && Window == window && Functions == functions &&
            Device == window->device())
        {
            return true;
        }
        Release();
        Window = window;
        Functions = functions;
        Device = window ? window->device() : VK_NULL_HANDLE;
        DepthFormat = window ? window->depthStencilFormat() : VK_FORMAT_UNDEFINED;
        if (!Window || !Functions || !Device || DepthFormat == VK_FORMAT_UNDEFINED)
            return false;

        VkAttachmentDescription attachments[4]{};
        attachments[0].format = kColorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[2].format = VK_FORMAT_R32_SFLOAT;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[3].format = DepthFormat;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const std::array<VkAttachmentReference, 3> colorRefs{{
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        }};
        VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(colorRefs.size());
        subpass.pColorAttachments = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        passInfo.attachmentCount = 4;
        passInfo.pAttachments = attachments;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = 2;
        passInfo.pDependencies = dependencies;
        if (Functions->vkCreateRenderPass(
                Device, &passInfo, nullptr, &RenderPass) != VK_SUCCESS)
            return false;

        VkAttachmentDescription finalColor{};
        finalColor.format = kColorFormat;
        finalColor.samples = VK_SAMPLE_COUNT_1_BIT;
        finalColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        finalColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        finalColor.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        finalColor.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference finalColorRef{
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription finalSubpass{};
        finalSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        finalSubpass.colorAttachmentCount = 1;
        finalSubpass.pColorAttachments = &finalColorRef;
        std::array<VkSubpassDependency, 2> finalDependencies{};
        finalDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        finalDependencies[0].dstSubpass = 0;
        finalDependencies[0].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        finalDependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        finalDependencies[0].srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        finalDependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        finalDependencies[1].srcSubpass = 0;
        finalDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        finalDependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        finalDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        finalDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        finalDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo finalPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        finalPassInfo.attachmentCount = 1;
        finalPassInfo.pAttachments = &finalColor;
        finalPassInfo.subpassCount = 1;
        finalPassInfo.pSubpasses = &finalSubpass;
        finalPassInfo.dependencyCount =
            static_cast<std::uint32_t>(finalDependencies.size());
        finalPassInfo.pDependencies = finalDependencies.data();
        if (Functions->vkCreateRenderPass(
                Device, &finalPassInfo, nullptr, &FinalRenderPass) != VK_SUCCESS)
            return false;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        descriptorInfo.pBindings = bindings.data();
        if (Functions->vkCreateDescriptorSetLayout(
                Device, &descriptorInfo, nullptr, &DescriptorLayout) != VK_SUCCESS)
            return false;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = 32;
        VkPipelineLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &DescriptorLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (Functions->vkCreatePipelineLayout(
                Device, &layoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
            return false;

        std::array<VkDescriptorSetLayoutBinding, 2> clearBitmapBindings{};
        for (std::uint32_t binding = 0; binding < clearBitmapBindings.size(); ++binding)
        {
            clearBitmapBindings[binding].binding = binding;
            clearBitmapBindings[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            clearBitmapBindings[binding].descriptorCount = 1;
            clearBitmapBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo clearBitmapDescriptorInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        clearBitmapDescriptorInfo.bindingCount =
            static_cast<std::uint32_t>(clearBitmapBindings.size());
        clearBitmapDescriptorInfo.pBindings = clearBitmapBindings.data();
        if (Functions->vkCreateDescriptorSetLayout(
                Device,
                &clearBitmapDescriptorInfo,
                nullptr,
                &ClearBitmapDescriptorLayout) != VK_SUCCESS)
        {
            return false;
        }

        VkPushConstantRange clearBitmapPushRange{};
        clearBitmapPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        clearBitmapPushRange.size =
            sizeof(melonDS::Vulkan::ClearBitmapPushConstants);
        VkPipelineLayoutCreateInfo clearBitmapLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        clearBitmapLayoutInfo.setLayoutCount = 1;
        clearBitmapLayoutInfo.pSetLayouts = &ClearBitmapDescriptorLayout;
        clearBitmapLayoutInfo.pushConstantRangeCount = 1;
        clearBitmapLayoutInfo.pPushConstantRanges = &clearBitmapPushRange;
        if (Functions->vkCreatePipelineLayout(
                Device,
                &clearBitmapLayoutInfo,
                nullptr,
                &ClearBitmapPipelineLayout) != VK_SUCCESS)
        {
            return false;
        }

        std::array<VkDescriptorSetLayoutBinding, 2> finalBindings{};
        for (std::uint32_t binding = 0; binding < finalBindings.size(); ++binding)
        {
            finalBindings[binding].binding = binding;
            finalBindings[binding].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            finalBindings[binding].descriptorCount = 1;
            finalBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo finalDescriptorInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        finalDescriptorInfo.bindingCount =
            static_cast<std::uint32_t>(finalBindings.size());
        finalDescriptorInfo.pBindings = finalBindings.data();
        if (Functions->vkCreateDescriptorSetLayout(
                Device,
                &finalDescriptorInfo,
                nullptr,
                &FinalDescriptorLayout) != VK_SUCCESS)
            return false;

        VkPushConstantRange finalPushRange{};
        finalPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        finalPushRange.size = sizeof(NativeRasterFinalPush);
        VkPipelineLayoutCreateInfo finalLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        finalLayoutInfo.setLayoutCount = 1;
        finalLayoutInfo.pSetLayouts = &FinalDescriptorLayout;
        finalLayoutInfo.pushConstantRangeCount = 1;
        finalLayoutInfo.pPushConstantRanges = &finalPushRange;
        if (Functions->vkCreatePipelineLayout(
                Device, &finalLayoutInfo, nullptr, &FinalPipelineLayout) != VK_SUCCESS)
            return false;

        const std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16400},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384},
        }};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 16400;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (Functions->vkCreateDescriptorPool(
                Device, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
            return false;

        for (std::uint32_t s = 0; s < 3; ++s)
        {
            for (std::uint32_t t = 0; t < 3; ++t)
            {
                const auto axis = [](std::uint32_t mode) {
                    switch (mode)
                    {
                    case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    }
                };
                VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                samplerInfo.magFilter = VK_FILTER_NEAREST;
                samplerInfo.minFilter = VK_FILTER_NEAREST;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = axis(s);
                samplerInfo.addressModeV = axis(t);
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.maxLod = 0.0f;
                const std::uint32_t index = s * 3u + t;
                if (Functions->vkCreateSampler(
                        Device, &samplerInfo, nullptr, &Samplers[index]) != VK_SUCCESS)
                    return false;
            }
        }

        VkShaderModule vertex = VK_NULL_HANDLE;
        VkShaderModule fragment = VK_NULL_HANDLE;
        VkShaderModule stencilClearVertex = VK_NULL_HANDLE;
        VkShaderModule stencilOnlyFragment = VK_NULL_HANDLE;
        VkShaderModule shadowMaskFragment = VK_NULL_HANDLE;
        VkShaderModule clearBitmapVertex = VK_NULL_HANDLE;
        VkShaderModule clearBitmapFragment = VK_NULL_HANDLE;
        VkShaderModule finalVertex = VK_NULL_HANDLE;
        VkShaderModule finalEdgeFragment = VK_NULL_HANDLE;
        VkShaderModule finalFogFragment = VK_NULL_HANDLE;
        if (!CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterVertexSpirv),
                vertex) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterFragmentSpirv),
                fragment) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14StencilClearVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14StencilClearVertexSpirv),
                stencilClearVertex) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14StencilOnlyFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14StencilOnlyFragmentSpirv),
                stencilOnlyFragment) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14ShadowMaskFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14ShadowMaskFragmentSpirv),
                shadowMaskFragment) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanClearBitmapVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanClearBitmapVertexSpirv),
                clearBitmapVertex) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanClearBitmapFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanClearBitmapFragmentSpirv),
                clearBitmapFragment) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14FinalVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14FinalVertexSpirv),
                finalVertex) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14FinalEdgeFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14FinalEdgeFragmentSpirv),
                finalEdgeFragment) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14FinalFogFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14FinalFogFragmentSpirv),
                finalFogFragment))
        {
            if (vertex)
                Functions->vkDestroyShaderModule(Device, vertex, nullptr);
            if (fragment)
                Functions->vkDestroyShaderModule(Device, fragment, nullptr);
            if (stencilClearVertex)
                Functions->vkDestroyShaderModule(Device, stencilClearVertex, nullptr);
            if (stencilOnlyFragment)
                Functions->vkDestroyShaderModule(Device, stencilOnlyFragment, nullptr);
            if (shadowMaskFragment)
                Functions->vkDestroyShaderModule(Device, shadowMaskFragment, nullptr);
            if (clearBitmapVertex)
                Functions->vkDestroyShaderModule(Device, clearBitmapVertex, nullptr);
            if (clearBitmapFragment)
                Functions->vkDestroyShaderModule(Device, clearBitmapFragment, nullptr);
            if (finalVertex)
                Functions->vkDestroyShaderModule(Device, finalVertex, nullptr);
            if (finalEdgeFragment)
                Functions->vkDestroyShaderModule(Device, finalEdgeFragment, nullptr);
            if (finalFogFragment)
                Functions->vkDestroyShaderModule(Device, finalFogFragment, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(melonDS::Vulkan::VulkanPackedVertex);
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attributes[4]{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_UINT, 0};
        attributes[1] = {1, 0, VK_FORMAT_R32_UINT, 16};
        attributes[2] = {2, 0, VK_FORMAT_R32_UINT, 20};
        attributes[3] = {3, 0, VK_FORMAT_R32_UINT, 24};
        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 4;
        vertexInput.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        // Ordinary DS polygons use strict less-than. Attribute bit 14 selects
        // the dedicated Sapphire-style less-or-equal variants below.
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkStencilOpState stencil{};
        stencil.failOp = VK_STENCIL_OP_KEEP;
        stencil.depthFailOp = VK_STENCIL_OP_KEEP;
        stencil.passOp = VK_STENCIL_OP_REPLACE;
        stencil.compareOp = VK_COMPARE_OP_ALWAYS;
        stencil.compareMask = 0xFFu;
        stencil.writeMask = 0xFFu;
        depth.stencilTestEnable = VK_TRUE;
        depth.front = stencil;
        depth.back = stencil;
        std::array<VkPipelineColorBlendAttachmentState, 3> colorBlends{};
        colorBlends[0].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlends[1].colorWriteMask = colorBlends[0].colorWriteMask;
        colorBlends[2].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = static_cast<std::uint32_t>(colorBlends.size());
        blend.pAttachments = colorBlends.data();
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 5;
        dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &assembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depth;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = PipelineLayout;
        pipelineInfo.renderPass = RenderPass;
        const VkResult pipelineResult = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &Pipeline);
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        const VkResult linePipelineResult = pipelineResult == VK_SUCCESS
            ? Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &LinePipeline)
            : pipelineResult;

        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkResult depthEqualPipelineResult = linePipelineResult == VK_SUCCESS
            ? Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &DepthEqualPipeline)
            : linePipelineResult;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        const VkResult depthEqualLinePipelineResult = depthEqualPipelineResult == VK_SUCCESS
            ? Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &DepthEqualLinePipeline)
            : depthEqualPipelineResult;

        const auto createTopologyPair = [&](VkPipeline& trianglePipeline,
                                            VkPipeline& linePipeline) -> VkResult {
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkResult result = Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &trianglePipeline);
            if (result != VK_SUCCESS)
                return result;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            return Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &linePipeline);
        };

        VkResult translucentPipelineResult = depthEqualLinePipelineResult;
        if (translucentPipelineResult == VK_SUCCESS)
        {
            depth.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
            depth.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
            colorBlends[1].colorWriteMask = VK_COLOR_COMPONENT_B_BIT;
            for (std::uint32_t depthEqual = 0; depthEqual < 2; ++depthEqual)
            {
                depth.depthCompareOp = depthEqual
                    ? VK_COMPARE_OP_LESS_OR_EQUAL
                    : VK_COMPARE_OP_LESS;
                auto& trianglePipelines = depthEqual
                    ? DepthEqualTranslucentPipelines
                    : TranslucentPipelines;
                auto& linePipelines = depthEqual
                    ? DepthEqualTranslucentLinePipelines
                    : TranslucentLinePipelines;
                for (std::uint32_t depthWrite = 0; depthWrite < 2; ++depthWrite)
                {
                    depth.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
                    colorBlends[2].colorWriteMask = depthWrite
                        ? VK_COLOR_COMPONENT_R_BIT
                        : 0;
                    for (std::uint32_t alphaBlend = 0; alphaBlend < 2; ++alphaBlend)
                    {
                        const std::uint32_t index = depthWrite * 2u + alphaBlend;
                        auto& colorBlend = colorBlends[0];
                        colorBlend.blendEnable = alphaBlend ? VK_TRUE : VK_FALSE;
                        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
                        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        colorBlend.alphaBlendOp = VK_BLEND_OP_MAX;

                        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                        translucentPipelineResult = Functions->vkCreateGraphicsPipelines(
                            Device,
                            VK_NULL_HANDLE,
                            1,
                            &pipelineInfo,
                            nullptr,
                            &trianglePipelines[index]);
                        if (translucentPipelineResult != VK_SUCCESS)
                            break;
                        assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                        translucentPipelineResult = Functions->vkCreateGraphicsPipelines(
                            Device,
                            VK_NULL_HANDLE,
                            1,
                            &pipelineInfo,
                            nullptr,
                            &linePipelines[index]);
                        if (translucentPipelineResult != VK_SUCCESS)
                            break;

                        stencil.passOp = VK_STENCIL_OP_INVERT;
                        stencil.compareOp = VK_COMPARE_OP_EQUAL;
                        depth.front = stencil;
                        depth.back = stencil;
                        const std::uint32_t bgShadowIndex =
                            depthEqual * 4u + index;
                        translucentPipelineResult = createTopologyPair(
                            BgZeroShadowBlendPipelines[bgShadowIndex],
                            BgZeroShadowBlendLinePipelines[bgShadowIndex]);
                        if (translucentPipelineResult == VK_SUCCESS && alphaBlend == 0u)
                        {
                            colorBlend.blendEnable = VK_FALSE;
                            const std::uint32_t bgIndex =
                                depthEqual * 2u + depthWrite;
                            translucentPipelineResult = createTopologyPair(
                                BgZeroTranslucentPipelines[bgIndex],
                                BgZeroTranslucentLinePipelines[bgIndex]);
                        }
                        stencil.passOp = VK_STENCIL_OP_REPLACE;
                        stencil.compareOp = VK_COMPARE_OP_NOT_EQUAL;
                        depth.front = stencil;
                        depth.back = stencil;
                        if (translucentPipelineResult != VK_SUCCESS)
                            break;
                    }
                    if (translucentPipelineResult != VK_SUCCESS)
                        break;
                }
                if (translucentPipelineResult != VK_SUCCESS)
                    break;
            }
        }

        VkResult shadowPipelineResult = translucentPipelineResult;
        if (shadowPipelineResult == VK_SUCCESS)
        {
            // Sapphire clears only shadow bit 7 before every mask, preserving
            // the lower polygon and translucent ID bits.
            VkPipelineVertexInputStateCreateInfo emptyVertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            stages[0].module = stencilClearVertex;
            stages[1].module = stencilOnlyFragment;
            pipelineInfo.pVertexInputState = &emptyVertexInput;
            depth.depthTestEnable = VK_FALSE;
            depth.depthWriteEnable = VK_FALSE;
            stencil.failOp = VK_STENCIL_OP_KEEP;
            stencil.depthFailOp = VK_STENCIL_OP_KEEP;
            stencil.passOp = VK_STENCIL_OP_REPLACE;
            stencil.compareOp = VK_COMPARE_OP_ALWAYS;
            depth.front = stencil;
            depth.back = stencil;
            colorBlends[0] = {};
            colorBlends[1] = {};
            colorBlends[2] = {};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            shadowPipelineResult = Functions->vkCreateGraphicsPipelines(
                Device,
                VK_NULL_HANDLE,
                1,
                &pipelineInfo,
                nullptr,
                &StencilBitClearPipeline);
        }

        if (shadowPipelineResult == VK_SUCCESS)
        {
            // Shadow mask: record bit 7 only where the depth test fails.
            stages[0].module = vertex;
            stages[1].module = shadowMaskFragment;
            pipelineInfo.pVertexInputState = &vertexInput;
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_FALSE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            stencil.failOp = VK_STENCIL_OP_KEEP;
            stencil.depthFailOp = VK_STENCIL_OP_REPLACE;
            stencil.passOp = VK_STENCIL_OP_KEEP;
            stencil.compareOp = VK_COMPARE_OP_ALWAYS;
            depth.front = stencil;
            depth.back = stencil;
            shadowPipelineResult = createTopologyPair(
                ShadowMaskPipeline, ShadowMaskLinePipeline);
        }

        if (shadowPipelineResult == VK_SUCCESS)
        {
            // With a transparent clear plane, mark depth-failed background
            // coverage by flipping stencil bit 0: 0xFF -> 0xFE.
            stencil.depthFailOp = VK_STENCIL_OP_INVERT;
            stencil.compareOp = VK_COMPARE_OP_EQUAL;
            depth.front = stencil;
            depth.back = stencil;
            shadowPipelineResult = createTopologyPair(
                BgZeroShadowMaskPipeline,
                BgZeroShadowMaskLinePipeline);
        }

        if (shadowPipelineResult == VK_SUCCESS)
        {
            // Visible shadow pre-pass: clear bit 7 where the destination lower
            // polygon ID equals the shadow polygon ID.
            stencil.failOp = VK_STENCIL_OP_KEEP;
            stencil.depthFailOp = VK_STENCIL_OP_KEEP;
            stencil.passOp = VK_STENCIL_OP_ZERO;
            stencil.compareOp = VK_COMPARE_OP_EQUAL;
            depth.front = stencil;
            depth.back = stencil;
            for (std::uint32_t depthEqual = 0; depthEqual < 2; ++depthEqual)
            {
                depth.depthCompareOp = depthEqual
                    ? VK_COMPARE_OP_LESS_OR_EQUAL
                    : VK_COMPARE_OP_LESS;
                shadowPipelineResult = createTopologyPair(
                    ShadowClearPipelines[depthEqual],
                    ShadowClearLinePipelines[depthEqual]);
                if (shadowPipelineResult != VK_SUCCESS)
                    break;
            }
        }

        if (shadowPipelineResult == VK_SUCCESS)
        {
            // Visible shadow blend: draw only where bit 7 remains, then write
            // the Sapphire translucent marker into the lower seven bits.
            stages[1].module = fragment;
            stencil.failOp = VK_STENCIL_OP_KEEP;
            stencil.depthFailOp = VK_STENCIL_OP_KEEP;
            stencil.passOp = VK_STENCIL_OP_REPLACE;
            stencil.compareOp = VK_COMPARE_OP_EQUAL;
            depth.front = stencil;
            depth.back = stencil;
            colorBlends[1].colorWriteMask = VK_COLOR_COMPONENT_B_BIT;
            for (std::uint32_t depthEqual = 0; depthEqual < 2; ++depthEqual)
            {
                depth.depthCompareOp = depthEqual
                    ? VK_COMPARE_OP_LESS_OR_EQUAL
                    : VK_COMPARE_OP_LESS;
                auto& trianglePipelines = depthEqual
                    ? DepthEqualShadowBlendPipelines
                    : ShadowBlendPipelines;
                auto& linePipelines = depthEqual
                    ? DepthEqualShadowBlendLinePipelines
                    : ShadowBlendLinePipelines;
                for (std::uint32_t depthWrite = 0; depthWrite < 2; ++depthWrite)
                {
                    depth.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
                    colorBlends[2].colorWriteMask = depthWrite
                        ? VK_COLOR_COMPONENT_R_BIT
                        : 0;
                    for (std::uint32_t alphaBlend = 0; alphaBlend < 2; ++alphaBlend)
                    {
                        const std::uint32_t index = depthWrite * 2u + alphaBlend;
                        auto& colorBlend = colorBlends[0];
                        colorBlend = {};
                        colorBlend.colorWriteMask =
                            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        colorBlend.blendEnable = alphaBlend ? VK_TRUE : VK_FALSE;
                        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
                        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        colorBlend.alphaBlendOp = VK_BLEND_OP_MAX;
                        shadowPipelineResult = createTopologyPair(
                            trianglePipelines[index], linePipelines[index]);
                        if (shadowPipelineResult != VK_SUCCESS)
                            break;
                    }
                    if (shadowPipelineResult != VK_SUCCESS)
                        break;
                }
                if (shadowPipelineResult != VK_SUCCESS)
                    break;
            }
        }

        VkResult clearBitmapPipelineResult = shadowPipelineResult;
        if (clearBitmapPipelineResult == VK_SUCCESS)
        {
            VkPipelineShaderStageCreateInfo clearStages[2]{};
            clearStages[0].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            clearStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            clearStages[0].module = clearBitmapVertex;
            clearStages[0].pName = "main";
            clearStages[1].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            clearStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            clearStages[1].module = clearBitmapFragment;
            clearStages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo clearVertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo clearAssembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            clearAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo clearViewport{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            clearViewport.viewportCount = 1;
            clearViewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo clearRaster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            clearRaster.polygonMode = VK_POLYGON_MODE_FILL;
            clearRaster.cullMode = VK_CULL_MODE_NONE;
            clearRaster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            clearRaster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo clearMultisample{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            clearMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkStencilOpState clearStencil{};
            clearStencil.failOp = VK_STENCIL_OP_REPLACE;
            clearStencil.passOp = VK_STENCIL_OP_REPLACE;
            clearStencil.depthFailOp = VK_STENCIL_OP_REPLACE;
            clearStencil.compareOp = VK_COMPARE_OP_ALWAYS;
            clearStencil.compareMask = 0xFFu;
            clearStencil.writeMask = 0xFFu;
            clearStencil.reference = 0xFFu;
            VkPipelineDepthStencilStateCreateInfo clearDepth{
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            clearDepth.depthTestEnable = VK_TRUE;
            clearDepth.depthWriteEnable = VK_TRUE;
            clearDepth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
            clearDepth.stencilTestEnable = VK_TRUE;
            clearDepth.front = clearStencil;
            clearDepth.back = clearStencil;
            std::array<VkPipelineColorBlendAttachmentState, 3> clearAttachments{};
            for (std::size_t index = 0; index < 2; ++index)
            {
                clearAttachments[index].colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            }
            clearAttachments[2].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
            VkPipelineColorBlendStateCreateInfo clearBlend{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            clearBlend.attachmentCount =
                static_cast<std::uint32_t>(clearAttachments.size());
            clearBlend.pAttachments = clearAttachments.data();
            const VkDynamicState clearDynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
            VkPipelineDynamicStateCreateInfo clearDynamic{
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            clearDynamic.dynamicStateCount = 2;
            clearDynamic.pDynamicStates = clearDynamicStates;
            VkGraphicsPipelineCreateInfo clearPipelineInfo{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            clearPipelineInfo.stageCount = 2;
            clearPipelineInfo.pStages = clearStages;
            clearPipelineInfo.pVertexInputState = &clearVertexInput;
            clearPipelineInfo.pInputAssemblyState = &clearAssembly;
            clearPipelineInfo.pViewportState = &clearViewport;
            clearPipelineInfo.pRasterizationState = &clearRaster;
            clearPipelineInfo.pMultisampleState = &clearMultisample;
            clearPipelineInfo.pDepthStencilState = &clearDepth;
            clearPipelineInfo.pColorBlendState = &clearBlend;
            clearPipelineInfo.pDynamicState = &clearDynamic;
            clearPipelineInfo.layout = ClearBitmapPipelineLayout;
            clearPipelineInfo.renderPass = RenderPass;
            clearBitmapPipelineResult = Functions->vkCreateGraphicsPipelines(
                Device,
                VK_NULL_HANDLE,
                1,
                &clearPipelineInfo,
                nullptr,
                &ClearBitmapPipeline);
        }

        VkResult finalPipelineResult = clearBitmapPipelineResult;
        if (finalPipelineResult == VK_SUCCESS)
        {
            VkPipelineShaderStageCreateInfo edgeMarkStages[2]{};
            edgeMarkStages[0].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            edgeMarkStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            edgeMarkStages[0].module = vertex;
            edgeMarkStages[0].pName = "main";
            edgeMarkStages[1].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            edgeMarkStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            edgeMarkStages[1].module = fragment;
            edgeMarkStages[1].pName = "main";
            VkPipelineInputAssemblyStateCreateInfo edgeMarkAssembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            edgeMarkAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            VkPipelineDepthStencilStateCreateInfo edgeMarkDepth{
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            std::array<VkPipelineColorBlendAttachmentState, 3> edgeMarkAttachments{};
            edgeMarkAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_G_BIT;
            VkPipelineColorBlendStateCreateInfo edgeMarkBlend{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            edgeMarkBlend.attachmentCount =
                static_cast<std::uint32_t>(edgeMarkAttachments.size());
            edgeMarkBlend.pAttachments = edgeMarkAttachments.data();
            VkGraphicsPipelineCreateInfo edgeMarkInfo{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            edgeMarkInfo.stageCount = 2;
            edgeMarkInfo.pStages = edgeMarkStages;
            edgeMarkInfo.pVertexInputState = &vertexInput;
            edgeMarkInfo.pInputAssemblyState = &edgeMarkAssembly;
            edgeMarkInfo.pViewportState = &viewport;
            edgeMarkInfo.pRasterizationState = &raster;
            edgeMarkInfo.pMultisampleState = &multisample;
            edgeMarkInfo.pDepthStencilState = &edgeMarkDepth;
            edgeMarkInfo.pColorBlendState = &edgeMarkBlend;
            edgeMarkInfo.pDynamicState = &dynamic;
            edgeMarkInfo.layout = PipelineLayout;
            edgeMarkInfo.renderPass = RenderPass;
            finalPipelineResult = Functions->vkCreateGraphicsPipelines(
                Device,
                VK_NULL_HANDLE,
                1,
                &edgeMarkInfo,
                nullptr,
                &EdgeMarkPipeline);
        }

        const auto createFinalPipeline = [&](VkShaderModule fragmentModule,
                                             VkColorComponentFlags writeMask,
                                             VkPipeline& output) -> VkResult
        {
            VkPipelineShaderStageCreateInfo finalStages[2]{};
            finalStages[0].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            finalStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            finalStages[0].module = finalVertex;
            finalStages[0].pName = "main";
            finalStages[1].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            finalStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            finalStages[1].module = fragmentModule;
            finalStages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo finalVertexInput{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo finalAssembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            finalAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo finalViewport{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            finalViewport.viewportCount = 1;
            finalViewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo finalRaster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            finalRaster.polygonMode = VK_POLYGON_MODE_FILL;
            finalRaster.cullMode = VK_CULL_MODE_NONE;
            finalRaster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            finalRaster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo finalMultisample{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            finalMultisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState finalAttachment{};
            finalAttachment.colorWriteMask = writeMask;
            finalAttachment.blendEnable = VK_TRUE;
            if (fragmentModule == finalEdgeFragment)
            {
                finalAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                finalAttachment.dstColorBlendFactor =
                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                finalAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                finalAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            }
            else
            {
                finalAttachment.srcColorBlendFactor =
                    VK_BLEND_FACTOR_CONSTANT_COLOR;
                finalAttachment.dstColorBlendFactor =
                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                finalAttachment.srcAlphaBlendFactor =
                    VK_BLEND_FACTOR_CONSTANT_COLOR;
                finalAttachment.dstAlphaBlendFactor =
                    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            }
            finalAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            finalAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo finalBlend{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            finalBlend.attachmentCount = 1;
            finalBlend.pAttachments = &finalAttachment;
            const VkDynamicState finalDynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
                VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            };
            VkPipelineDynamicStateCreateInfo finalDynamic{
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            finalDynamic.dynamicStateCount = 3;
            finalDynamic.pDynamicStates = finalDynamicStates;
            VkGraphicsPipelineCreateInfo finalInfo{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            finalInfo.stageCount = 2;
            finalInfo.pStages = finalStages;
            finalInfo.pVertexInputState = &finalVertexInput;
            finalInfo.pInputAssemblyState = &finalAssembly;
            finalInfo.pViewportState = &finalViewport;
            finalInfo.pRasterizationState = &finalRaster;
            finalInfo.pMultisampleState = &finalMultisample;
            finalInfo.pColorBlendState = &finalBlend;
            finalInfo.pDynamicState = &finalDynamic;
            finalInfo.layout = FinalPipelineLayout;
            finalInfo.renderPass = FinalRenderPass;
            return Functions->vkCreateGraphicsPipelines(
                Device, VK_NULL_HANDLE, 1, &finalInfo, nullptr, &output);
        };

        if (finalPipelineResult == VK_SUCCESS)
        {
            finalPipelineResult = createFinalPipeline(
                finalEdgeFragment,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT,
                FinalEdgePipeline);
        }
        if (finalPipelineResult == VK_SUCCESS)
        {
            finalPipelineResult = createFinalPipeline(
                finalFogFragment,
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                FinalFogPipeline);
        }
        if (finalPipelineResult == VK_SUCCESS)
        {
            finalPipelineResult = createFinalPipeline(
                finalFogFragment,
                VK_COLOR_COMPONENT_A_BIT,
                FinalFogAlphaPipeline);
        }
        Functions->vkDestroyShaderModule(Device, vertex, nullptr);
        Functions->vkDestroyShaderModule(Device, fragment, nullptr);
        Functions->vkDestroyShaderModule(Device, stencilClearVertex, nullptr);
        Functions->vkDestroyShaderModule(Device, stencilOnlyFragment, nullptr);
        Functions->vkDestroyShaderModule(Device, shadowMaskFragment, nullptr);
        Functions->vkDestroyShaderModule(Device, clearBitmapVertex, nullptr);
        Functions->vkDestroyShaderModule(Device, clearBitmapFragment, nullptr);
        Functions->vkDestroyShaderModule(Device, finalVertex, nullptr);
        Functions->vkDestroyShaderModule(Device, finalEdgeFragment, nullptr);
        Functions->vkDestroyShaderModule(Device, finalFogFragment, nullptr);
        if (pipelineResult != VK_SUCCESS || linePipelineResult != VK_SUCCESS ||
            depthEqualPipelineResult != VK_SUCCESS ||
            depthEqualLinePipelineResult != VK_SUCCESS ||
            translucentPipelineResult != VK_SUCCESS ||
            shadowPipelineResult != VK_SUCCESS ||
            clearBitmapPipelineResult != VK_SUCCESS ||
            finalPipelineResult != VK_SUCCESS)
            return false;

        Ready = true;
        return true;
    }

    bool EnsureTargets(int scale, int frameCount)
    {
        if (TargetsReady && Scale == scale && FrameCount == frameCount &&
            Slots[0].Color.Image != VK_NULL_HANDLE)
        {
            return true;
        }
        Functions->vkDeviceWaitIdle(Device);
        // Texture descriptor sets also reference the per-frame-slot toon
        // uniform buffer. Rebuild them whenever target slots are recreated.
        ClearTextureCache(false);
        DestroyTargets();
        Scale = scale;
        FrameCount = frameCount;
        TargetsReady = false;
        const VkExtent2D extent{
            kNativeWidth * static_cast<std::uint32_t>(scale),
            kNativeHeight * static_cast<std::uint32_t>(scale)};
        const VkExtent2D nativeExtent{kNativeWidth, kNativeHeight};
        for (int index = 0; index < frameCount; ++index)
        {
            auto& slot = Slots[index];
            if (!CreateImage(
                    Window,
                    Functions,
                    Device,
                    extent,
                    kColorFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.Color) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    extent,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.Attributes) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    extent,
                    VK_FORMAT_R32_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.DepthValue) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    extent,
                    DepthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    DepthAspectMask(DepthFormat),
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.Depth) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    nativeExtent,
                    kColorFormat,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.NativeReference) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    {kClearBitmapWidth, kClearBitmapHeight},
                    VK_FORMAT_R8G8B8A8_UINT,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.ClearBitmapColor) ||
                !CreateImage(
                    Window,
                    Functions,
                    Device,
                    {kClearBitmapWidth, kClearBitmapHeight},
                    VK_FORMAT_R32_UINT,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_VIEW_TYPE_2D,
                    slot.ClearBitmapDepth))
            {
                return false;
            }

            VkDescriptorSetAllocateInfo clearBitmapAllocation{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            clearBitmapAllocation.descriptorPool = DescriptorPool;
            clearBitmapAllocation.descriptorSetCount = 1;
            clearBitmapAllocation.pSetLayouts = &ClearBitmapDescriptorLayout;
            if (Functions->vkAllocateDescriptorSets(
                    Device,
                    &clearBitmapAllocation,
                    &slot.ClearBitmapDescriptor) != VK_SUCCESS)
            {
                return false;
            }
            std::array<VkDescriptorImageInfo, 2> clearBitmapImages{};
            clearBitmapImages[0].sampler = Samplers[4];
            clearBitmapImages[0].imageView = slot.ClearBitmapColor.View;
            clearBitmapImages[0].imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            clearBitmapImages[1].sampler = Samplers[4];
            clearBitmapImages[1].imageView = slot.ClearBitmapDepth.View;
            clearBitmapImages[1].imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            std::array<VkWriteDescriptorSet, 2> clearBitmapWrites{};
            for (std::uint32_t binding = 0; binding < clearBitmapWrites.size(); ++binding)
            {
                clearBitmapWrites[binding].sType =
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                clearBitmapWrites[binding].dstSet = slot.ClearBitmapDescriptor;
                clearBitmapWrites[binding].dstBinding = binding;
                clearBitmapWrites[binding].descriptorCount = 1;
                clearBitmapWrites[binding].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                clearBitmapWrites[binding].pImageInfo =
                    &clearBitmapImages[binding];
            }
            Functions->vkUpdateDescriptorSets(
                Device,
                static_cast<std::uint32_t>(clearBitmapWrites.size()),
                clearBitmapWrites.data(),
                0,
                nullptr);
            VkImageView attachments[] = {
                slot.Color.View,
                slot.Attributes.View,
                slot.DepthValue.View,
                slot.Depth.View,
            };
            VkFramebufferCreateInfo framebuffer{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = RenderPass;
            framebuffer.attachmentCount = 4;
            framebuffer.pAttachments = attachments;
            framebuffer.width = extent.width;
            framebuffer.height = extent.height;
            framebuffer.layers = 1;
            if (Functions->vkCreateFramebuffer(
                    Device, &framebuffer, nullptr, &slot.Framebuffer) != VK_SUCCESS)
                return false;

            VkFramebufferCreateInfo finalFramebuffer{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            finalFramebuffer.renderPass = FinalRenderPass;
            finalFramebuffer.attachmentCount = 1;
            finalFramebuffer.pAttachments = &slot.Color.View;
            finalFramebuffer.width = extent.width;
            finalFramebuffer.height = extent.height;
            finalFramebuffer.layers = 1;
            if (Functions->vkCreateFramebuffer(
                    Device,
                    &finalFramebuffer,
                    nullptr,
                    &slot.FinalFramebuffer) != VK_SUCCESS)
            {
                return false;
            }

            VkDescriptorSetAllocateInfo finalAllocation{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            finalAllocation.descriptorPool = DescriptorPool;
            finalAllocation.descriptorSetCount = 1;
            finalAllocation.pSetLayouts = &FinalDescriptorLayout;
            if (Functions->vkAllocateDescriptorSets(
                    Device, &finalAllocation, &slot.FinalDescriptor) != VK_SUCCESS)
                return false;
            std::array<VkDescriptorImageInfo, 2> finalImages{};
            finalImages[0].sampler = Samplers[0];
            finalImages[0].imageView = slot.Attributes.View;
            finalImages[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            finalImages[1].sampler = Samplers[0];
            finalImages[1].imageView = slot.DepthValue.View;
            finalImages[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            std::array<VkWriteDescriptorSet, 2> finalWrites{};
            for (std::uint32_t binding = 0; binding < finalWrites.size(); ++binding)
            {
                finalWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                finalWrites[binding].dstSet = slot.FinalDescriptor;
                finalWrites[binding].dstBinding = binding;
                finalWrites[binding].descriptorCount = 1;
                finalWrites[binding].descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                finalWrites[binding].pImageInfo = &finalImages[binding];
            }
            Functions->vkUpdateDescriptorSets(
                Device,
                static_cast<std::uint32_t>(finalWrites.size()),
                finalWrites.data(),
                0,
                nullptr);
        }
        TargetsReady = true;
        return true;
    }

    bool PrepareTextureCache(const NativeRasterFrame& frame)
    {
        std::unordered_set<std::uint64_t> frameIdentities;
        frameIdentities.reserve(frame.Textures.size());
        std::uint64_t frameWorkingSetBytes = 0;
        for (const auto& texture : frame.Textures)
        {
            const auto inserted = frameIdentities.insert(texture.Key).second;
            if (inserted)
            {
                frameWorkingSetBytes +=
                    static_cast<std::uint64_t>(texture.Width) * texture.Height * 4ull *
                    2ull * static_cast<std::uint64_t>(FrameCount);
            }
        }
        if (frameIdentities.size() > kMaxResidentTextureIdentities ||
            frameWorkingSetBytes > kMaxResidentTextureBytes)
        {
            return false;
        }

        std::size_t missing = 0;
        for (std::uint64_t identity : frameIdentities)
            missing += Textures.find(identity) == Textures.end() ? 1u : 0u;
        if (Textures.size() + missing > kMaxResidentTextureIdentities)
            ClearTextureCache(true);
        return true;
    }

    TextureVersion* EnsureTexture(
        VkCommandBuffer command,
        int frameSlot,
        const NativeRasterTexture& texture)
    {
        const std::uint64_t expectedPixels =
            static_cast<std::uint64_t>(texture.Width) * texture.Height;
        if (!texture.Rgb6a5 || texture.Width == 0 || texture.Height == 0 ||
            expectedPixels > std::numeric_limits<std::size_t>::max() ||
            texture.Rgb6a5->size() != static_cast<std::size_t>(expectedPixels))
        {
            return nullptr;
        }
        auto [found, inserted] = Textures.try_emplace(
            texture.Key, std::make_unique<TextureResource>());
        (void)inserted;
        TextureVersion& version = found->second->Versions[frameSlot];
        if (version.Image.Image && version.ContentHash == texture.ContentHash)
            return &version;

        // QVulkanWindow does not reuse currentFrame() until that slot's fence
        // is complete. Only the selected version can therefore be replaced
        // without waiting for the other concurrent frames.
        DestroyTextureVersion(version);
        const auto fail = [&]() -> TextureVersion*
        {
            DestroyTextureVersion(version);
            return nullptr;
        };
        version.ContentHash = texture.ContentHash;
        const VkExtent2D extent{texture.Width, texture.Height};
        if (!CreateImage(
                Window,
                Functions,
                Device,
                extent,
                kTextureFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                version.Image))
            return fail();

        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(texture.Width) * texture.Height * 4u;
        if (!EnsureHostBuffer(
                Window,
                Functions,
                Device,
                bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                version.Staging))
            return fail();
        std::memcpy(version.Staging.Map, texture.Rgb6a5->data(), bytes);

        TransitionImage(
            Functions,
            command,
            version.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {texture.Width, texture.Height, 1};
        Functions->vkCmdCopyBufferToImage(
            command,
            version.Staging.Buffer,
            version.Image.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);
        TransitionImage(
            Functions,
            command,
            version.Image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        for (std::uint32_t sampler = 0; sampler < 9; ++sampler)
        {
            VkDescriptorSetAllocateInfo allocation{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = DescriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &DescriptorLayout;
            if (Functions->vkAllocateDescriptorSets(
                    Device, &allocation, &version.DescriptorSets[sampler]) != VK_SUCCESS)
                return fail();
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = Samplers[sampler];
            imageInfo.imageView = version.Image.View;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorBufferInfo toonInfo{};
            toonInfo.buffer = Slots[frameSlot].ToonTable.Buffer;
            toonInfo.range = sizeof(NativeRasterToonUniform);
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = version.DescriptorSets[sampler];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &imageInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = version.DescriptorSets[sampler];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].pBufferInfo = &toonInfo;
            Functions->vkUpdateDescriptorSets(
                Device,
                static_cast<std::uint32_t>(writes.size()),
                writes.data(),
                0,
                nullptr);
        }

        return &version;
    }

    bool UploadToonTable(Slot& slot, const NativeRasterFrame& frame)
    {
        if (!EnsureHostBuffer(
                Window,
                Functions,
                Device,
                sizeof(NativeRasterToonUniform),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                slot.ToonTable))
        {
            return false;
        }

        NativeRasterToonUniform uniform{};
        for (std::size_t index = 0; index < frame.RenderToonTable.size(); ++index)
        {
            const std::uint16_t color = frame.RenderToonTable[index];
            std::uint32_t red = (color << 1u) & 0x3Eu;
            std::uint32_t green = (color >> 4u) & 0x3Eu;
            std::uint32_t blue = (color >> 9u) & 0x3Eu;
            if (red) ++red;
            if (green) ++green;
            if (blue) ++blue;
            uniform.Colors[index][0] = red;
            uniform.Colors[index][1] = green;
            uniform.Colors[index][2] = blue;
            uniform.Colors[index][3] = 31u;
        }
        std::memcpy(slot.ToonTable.Map, &uniform, sizeof(uniform));
        return true;
    }

    bool UploadNativeReference(
        VkCommandBuffer command,
        Slot& slot,
        const NativeRasterFrame& frame)
    {
        const VkDeviceSize bytes = kNativePixels * sizeof(std::uint32_t);
        if (frame.NativeReferenceBgra.size() != kNativePixels ||
            !EnsureHostBuffer(
                Window,
                Functions,
                Device,
                bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                slot.NativeReferenceStaging))
        {
            return false;
        }
        std::memcpy(
            slot.NativeReferenceStaging.Map,
            frame.NativeReferenceBgra.data(),
            bytes);
        TransitionImage(
            Functions,
            command,
            slot.NativeReference,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            slot.NativeReference.Layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            slot.NativeReference.Layout == VK_IMAGE_LAYOUT_UNDEFINED
                ? 0
                : VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kNativeWidth, kNativeHeight, 1};
        Functions->vkCmdCopyBufferToImage(
            command,
            slot.NativeReferenceStaging.Buffer,
            slot.NativeReference.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);
        TransitionImage(
            Functions,
            command,
            slot.NativeReference,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        return true;
    }

    bool UploadClearBitmap(
        VkCommandBuffer command,
        Slot& slot,
        const NativeRasterFrame& frame)
    {
        const auto state = melonDS::Vulkan::DecodeClearBitmapState(
            frame.RenderDispCnt,
            frame.RenderClearAttr1,
            frame.RenderClearAttr2);
        if (!state.Enabled)
            return true;
        if (frame.ClearBitmapColorRgba6a5.size() != kClearBitmapPixels ||
            frame.ClearBitmapDepthFog.size() != kClearBitmapPixels)
        {
            return false;
        }

        const VkDeviceSize bytes =
            static_cast<VkDeviceSize>(kClearBitmapPixels) * sizeof(std::uint32_t);
        if (!EnsureHostBuffer(
                Window,
                Functions,
                Device,
                bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                slot.ClearBitmapColorStaging) ||
            !EnsureHostBuffer(
                Window,
                Functions,
                Device,
                bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                slot.ClearBitmapDepthStaging))
        {
            return false;
        }
        std::memcpy(
            slot.ClearBitmapColorStaging.Map,
            frame.ClearBitmapColorRgba6a5.data(),
            bytes);
        std::memcpy(
            slot.ClearBitmapDepthStaging.Map,
            frame.ClearBitmapDepthFog.data(),
            bytes);

        ImageResource* images[] = {
            &slot.ClearBitmapColor,
            &slot.ClearBitmapDepth,
        };
        const VkBuffer buffers[] = {
            slot.ClearBitmapColorStaging.Buffer,
            slot.ClearBitmapDepthStaging.Buffer,
        };
        for (std::size_t index = 0; index < std::size(images); ++index)
        {
            auto& image = *images[index];
            const bool undefined = image.Layout == VK_IMAGE_LAYOUT_UNDEFINED;
            TransitionImage(
                Functions,
                command,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                undefined
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                undefined ? 0 : VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {
                kClearBitmapWidth,
                kClearBitmapHeight,
                1,
            };
            Functions->vkCmdCopyBufferToImage(
                command,
                buffers[index],
                image.Image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy);
            TransitionImage(
                Functions,
                command,
                image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
        }
        return true;
    }

    bool Render(
        VkCommandBuffer command,
        int frameSlot,
        const NativeRasterFrame& frame,
        NativeRasterViews& views)
    {
        views = {};
        if (!frame.Valid || frame.Scale <= 1 || frameSlot < 0 ||
            frameSlot >= FrameCount || !frame.Upload.Valid)
            return true;
        auto& slot = Slots[frameSlot];
        if (slot.FrameSerial == frame.FrameSerial &&
            slot.Generation == frame.Generation &&
            slot.Color.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            slot.Attributes.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            slot.DepthValue.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            slot.NativeReference.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            views.HighResolution = slot.Color.View;
            views.NativeReference = slot.NativeReference.View;
            views.Sampler = Samplers[0];
            views.Valid = true;
            return true;
        }

        const VkDeviceSize vertexBytes =
            frame.Upload.Vertices.size() * sizeof(melonDS::Vulkan::VulkanPackedVertex);
        const VkDeviceSize indexBytes =
            frame.Upload.Indices.size() * sizeof(std::uint16_t);
        const VkDeviceSize edgeIndexBytes =
            frame.Upload.EdgeIndices.size() * sizeof(std::uint16_t);
        if (!EnsureHostBuffer(
                Window,
                Functions,
                Device,
                vertexBytes,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                slot.Vertex) ||
            !EnsureHostBuffer(
                Window,
                Functions,
                Device,
                indexBytes,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                slot.Index) ||
            !EnsureHostBuffer(
                Window,
                Functions,
                Device,
                edgeIndexBytes,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                slot.EdgeIndex))
            return false;
        if (vertexBytes)
            std::memcpy(slot.Vertex.Map, frame.Upload.Vertices.data(), vertexBytes);
        if (indexBytes)
            std::memcpy(slot.Index.Map, frame.Upload.Indices.data(), indexBytes);
        if (edgeIndexBytes)
            std::memcpy(
                slot.EdgeIndex.Map,
                frame.Upload.EdgeIndices.data(),
                edgeIndexBytes);
        if (!UploadNativeReference(command, slot, frame))
            return false;
        if (!UploadClearBitmap(command, slot, frame))
            return false;

        if (!UploadToonTable(slot, frame))
            return false;

        if (!PrepareTextureCache(frame))
            return false;
        std::vector<TextureVersion*> textureResources(frame.Textures.size(), nullptr);
        for (std::size_t index = 0; index < frame.Textures.size(); ++index)
            textureResources[index] = EnsureTexture(
                command, frameSlot, frame.Textures[index]);
        if (textureResources.empty() || textureResources[0] == nullptr)
            return false;

        const auto clear = melonDS::Vulkan::DecodeClearPlaneState(
            frame.RenderClearAttr1,
            frame.RenderClearAttr2);
        VkClearValue clearValues[4]{};
        clearValues[0].color.float32[0] = clear.Color[0];
        clearValues[0].color.float32[1] = clear.Color[1];
        clearValues[0].color.float32[2] = clear.Color[2];
        clearValues[0].color.float32[3] = clear.Color[3];
        clearValues[1].color.float32[0] = clear.Attributes[0];
        clearValues[1].color.float32[1] = clear.Attributes[1];
        clearValues[1].color.float32[2] = clear.Attributes[2];
        clearValues[1].color.float32[3] = clear.Attributes[3];
        clearValues[2].color.float32[0] = clear.Depth;
        clearValues[3].depthStencil = {clear.Depth, clear.Stencil};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = RenderPass;
        begin.framebuffer = slot.Framebuffer;
        begin.renderArea.extent = slot.Color.Extent;
        begin.clearValueCount = 4;
        begin.pClearValues = clearValues;
        Functions->vkCmdBeginRenderPass(
            command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        const VkDeviceSize vertexOffset = 0;
        Functions->vkCmdBindVertexBuffers(
            command, 0, 1, &slot.Vertex.Buffer, &vertexOffset);

        VkViewport viewport{};
        viewport.x = 0.0f;
        // The vertex shader maps DS y=0 to NDC -1. A positive-height Vulkan
        // viewport therefore maps it to framebuffer row zero. Negating the
        // viewport here inverted the native 3D layer relative to the 2D frame.
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(slot.Color.Extent.width);
        viewport.height = static_cast<float>(slot.Color.Extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = slot.Color.Extent;
        Functions->vkCmdSetViewport(command, 0, 1, &viewport);
        Functions->vkCmdSetScissor(command, 0, 1, &scissor);

        const auto clearBitmap = melonDS::Vulkan::DecodeClearBitmapState(
            frame.RenderDispCnt,
            frame.RenderClearAttr1,
            frame.RenderClearAttr2);
        if (clearBitmap.Enabled)
        {
            if (slot.ClearBitmapDescriptor == VK_NULL_HANDLE)
            {
                Functions->vkCmdEndRenderPass(command);
                return false;
            }
            Functions->vkCmdBindPipeline(
                command,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                ClearBitmapPipeline);
            Functions->vkCmdBindDescriptorSets(
                command,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                ClearBitmapPipelineLayout,
                0,
                1,
                &slot.ClearBitmapDescriptor,
                0,
                nullptr);
            Functions->vkCmdPushConstants(
                command,
                ClearBitmapPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(clearBitmap.PushConstants),
                &clearBitmap.PushConstants);
            Functions->vkCmdDraw(command, 3, 1, 0, 0);
        }

        struct alignas(16) Push
        {
            float ScreenSize[2];
            std::uint32_t Textured;
            std::uint32_t TextureMode;
            std::uint32_t WBuffer;
            std::uint32_t Reserved[3];
        };
        static_assert(sizeof(Push) == 32);

        enum class DrawPhase
        {
            Opaque,
            BackgroundZero,
            Alpha,
        };
        const bool clearPlaneAlphaZero =
            ((frame.RenderClearAttr1 >> 16u) & 0x1Fu) == 0u;
        const std::uint32_t clearPlanePolygonId =
            (frame.RenderClearAttr1 >> 24u) & 0x3Fu;
        const std::array<DrawPhase, 3> drawPhases{{
            DrawPhase::Opaque,
            DrawPhase::BackgroundZero,
            DrawPhase::Alpha,
        }};
        for (DrawPhase drawPhase : drawPhases)
        {
            if (drawPhase == DrawPhase::BackgroundZero && !clearPlaneAlphaZero)
                continue;
            std::size_t polygonIndex = 0;
            while (polygonIndex < frame.Upload.Polygons.size())
            {
            const auto& polygon = frame.Upload.Polygons[polygonIndex];
            const bool linePrimitive =
                polygon.Primitive == static_cast<std::uint32_t>(
                    melonDS::Vulkan::VulkanRasterPrimitive::Lines);
            const bool wireframe = ((polygon.Attr >> 16u) & 0x1Fu) == 0u;
            const bool translucent =
                (polygon.Flags &
                 melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent) != 0;
            const bool shadowMask =
                (polygon.Flags &
                 melonDS::Vulkan::VulkanRasterPolygonFlag_ShadowMask) != 0;
            const bool shadow =
                (polygon.Flags &
                 melonDS::Vulkan::VulkanRasterPolygonFlag_Shadow) != 0;
            const std::uint32_t textureMode = (polygon.Attr >> 4u) & 0x3u;
            const bool textured =
                (polygon.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Textured) != 0;
            const bool textureUnsupported = textured &&
                (polygon.TextureLayer >= textureResources.size() ||
                 textureResources[polygon.TextureLayer] == nullptr ||
                 (textureMode > 2u && !shadowMask && !shadow));
            if (textureUnsupported || polygon.IndexCount == 0)
            {
                ++polygonIndex;
                continue;
            }
            const bool opaqueDraw = !translucent && !shadowMask && !shadow;
            const bool alphaDraw = translucent || shadowMask || shadow;
            if ((drawPhase == DrawPhase::Opaque && !opaqueDraw) ||
                (drawPhase != DrawPhase::Opaque && !alphaDraw))
            {
                ++polygonIndex;
                continue;
            }

            std::uint32_t drawCount = wireframe
                ? polygon.EdgeIndexCount
                : polygon.IndexCount;
            std::size_t next = polygonIndex + 1;
            while (!linePrimitive && !wireframe && !shadowMask && !shadow &&
                   next < frame.Upload.Polygons.size())
            {
                const auto& candidate = frame.Upload.Polygons[next];
                const bool candidateTextured =
                    (candidate.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Textured) != 0;
                if (candidate.Primitive != polygon.Primitive ||
                    candidate.Flags != polygon.Flags ||
                    candidate.TextureLayer != polygon.TextureLayer ||
                    candidate.Attr != polygon.Attr ||
                    candidate.IndexOffset != polygon.IndexOffset + drawCount)
                {
                    break;
                }
                const bool candidateUnsupported =
                    (candidate.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent) != 0 ||
                    (candidateTextured &&
                     (candidate.TextureLayer >= textureResources.size() ||
                      textureResources[candidate.TextureLayer] == nullptr ||
                      textureMode > 2u));
                if (candidateUnsupported)
                    break;
                drawCount += candidate.IndexCount;
                ++next;
            }

            VkDescriptorSet descriptor = VK_NULL_HANDLE;
            if (textured)
            {
                const std::uint32_t sampler = std::min<std::uint32_t>(
                    frame.Textures[polygon.TextureLayer].SamplerIndex, 8u);
                descriptor = textureResources[polygon.TextureLayer]->DescriptorSets[sampler];
            }
            else if (!frame.Textures.empty() && textureResources[0])
            {
                descriptor = textureResources[0]->DescriptorSets[0];
            }
            if (descriptor == VK_NULL_HANDLE)
            {
                ++polygonIndex;
                continue;
            }
            Functions->vkCmdBindDescriptorSets(
                command,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                PipelineLayout,
                0,
                1,
                &descriptor,
                0,
                nullptr);
            const std::uint32_t polygonId = (polygon.Attr >> 24u) & 0x3Fu;
            const bool useLinePipeline = linePrimitive || wireframe;
            const bool useTranslucentPass = translucent && !wireframe && !shadowMask;
            const bool useDepthEqual = (polygon.Attr & (1u << 14u)) != 0;
            const bool needsOpaqueTexturePass =
                useTranslucentPass && ((polygon.Attr >> 16u) & 0x1Fu) == 0x1Fu;
            const std::uint32_t translucentPipelineIndex =
                (((polygon.Attr >> 11u) & 0x1u) * 2u) +
                ((frame.RenderDispCnt >> 3u) & 0x1u);

            Push push{};
            push.ScreenSize[0] = static_cast<float>(slot.Color.Extent.width);
            push.ScreenSize[1] = static_cast<float>(slot.Color.Extent.height);
            push.Textured = textured ? 1u : 0u;
            push.TextureMode = textureMode;
            push.WBuffer =
                (polygon.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_WBuffer)
                ? 1u
                : 0u;
            push.Reserved[0] = frame.RenderXPos;
            push.Reserved[1] = frame.RenderDispCnt;
            push.Reserved[2] = (wireframe ? 1u : 0u) |
                ((frame.RenderAlphaRef & 0x1Fu) << 8u);

            Functions->vkCmdBindIndexBuffer(
                command,
                wireframe ? slot.EdgeIndex.Buffer : slot.Index.Buffer,
                0,
                VK_INDEX_TYPE_UINT16);

            if (drawPhase == DrawPhase::BackgroundZero && shadowMask)
            {
                const std::uint32_t maskAlpha = wireframe
                    ? 31u
                    : ((polygon.Attr >> 16u) & 0x1Fu);
                if (maskAlpha > frame.RenderAlphaRef)
                {
                    Functions->vkCmdSetStencilCompareMask(
                        command, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
                    Functions->vkCmdSetStencilWriteMask(
                        command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x01u);
                    Functions->vkCmdSetStencilReference(
                        command, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
                    Functions->vkCmdBindPipeline(
                        command,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        useLinePipeline
                            ? BgZeroShadowMaskLinePipeline
                            : BgZeroShadowMaskPipeline);
                    Functions->vkCmdPushConstants(
                        command,
                        PipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0,
                        sizeof(push),
                        &push);
                    Functions->vkCmdDrawIndexed(
                        command,
                        drawCount,
                        1,
                        wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                        0,
                        0);
                }
                polygonIndex = next;
                continue;
            }

            if (shadowMask)
            {
                // Consecutive masks share bit 7; a new mask begins a new
                // Sapphire shadow group and therefore clears only that bit.
                Functions->vkCmdSetStencilCompareMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x80u);
                Functions->vkCmdSetStencilWriteMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x80u);
                Functions->vkCmdSetStencilReference(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0u);
                Functions->vkCmdBindPipeline(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    StencilBitClearPipeline);
                Functions->vkCmdDraw(command, 3, 1, 0, 0);

                const std::uint32_t maskAlpha = wireframe
                    ? 31u
                    : ((polygon.Attr >> 16u) & 0x1Fu);
                if (maskAlpha > frame.RenderAlphaRef)
                {
                    Functions->vkCmdSetStencilReference(
                        command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x80u);
                    Functions->vkCmdBindPipeline(
                        command,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        useLinePipeline ? ShadowMaskLinePipeline : ShadowMaskPipeline);
                    Functions->vkCmdPushConstants(
                        command,
                        PipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0,
                        sizeof(push),
                        &push);
                    Functions->vkCmdDrawIndexed(
                        command,
                        drawCount,
                        1,
                        wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                        0,
                        0);
                }
                polygonIndex = next;
                continue;
            }

            // Alpha-textured polygons with polygon alpha 31 contain both
            // opaque and translucent texels. Sapphire renders their opaque
            // coverage first so it participates in depth like an ordinary
            // polygon, then performs the alpha pass below.
            if (needsOpaqueTexturePass)
            {
                Functions->vkCmdSetStencilCompareMask(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    0xFFu);
                Functions->vkCmdSetStencilWriteMask(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    0xFFu);
                Functions->vkCmdSetStencilReference(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    polygonId);
                Functions->vkCmdBindPipeline(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    useDepthEqual
                        ? (useLinePipeline
                            ? DepthEqualLinePipeline
                            : DepthEqualPipeline)
                        : (useLinePipeline ? LinePipeline : Pipeline));
                Functions->vkCmdPushConstants(
                    command,
                    PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(push),
                    &push);
                Functions->vkCmdDrawIndexed(
                    command,
                    drawCount,
                    1,
                    wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                    0,
                    0);
            }

            if (drawPhase == DrawPhase::BackgroundZero)
            {
                if (!useTranslucentPass ||
                    (shadow && polygonId != clearPlanePolygonId))
                {
                    polygonIndex = next;
                    continue;
                }
                const std::uint32_t stencilWriteMask =
                    (~(0x40u | polygonId)) & 0xFFu;
                Functions->vkCmdSetStencilCompareMask(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    shadow ? 0xFFu : 0xFEu);
                Functions->vkCmdSetStencilWriteMask(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    stencilWriteMask);
                Functions->vkCmdSetStencilReference(
                    command,
                    VK_STENCIL_FACE_FRONT_AND_BACK,
                    shadow ? 0xFEu : 0xFFu);
                const std::uint32_t depthWrite =
                    (polygon.Attr >> 11u) & 0x1u;
                VkPipeline backgroundPipeline = VK_NULL_HANDLE;
                if (shadow)
                {
                    const std::uint32_t backgroundShadowIndex =
                        (useDepthEqual ? 4u : 0u) + translucentPipelineIndex;
                    backgroundPipeline = useLinePipeline
                        ? BgZeroShadowBlendLinePipelines[backgroundShadowIndex]
                        : BgZeroShadowBlendPipelines[backgroundShadowIndex];
                }
                else
                {
                    const std::uint32_t backgroundIndex =
                        (useDepthEqual ? 2u : 0u) + depthWrite;
                    backgroundPipeline = useLinePipeline
                        ? BgZeroTranslucentLinePipelines[backgroundIndex]
                        : BgZeroTranslucentPipelines[backgroundIndex];
                }
                push.Reserved[2] |= 2u;
                Functions->vkCmdBindPipeline(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    backgroundPipeline);
                Functions->vkCmdPushConstants(
                    command,
                    PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(push),
                    &push);
                Functions->vkCmdDrawIndexed(
                    command,
                    drawCount,
                    1,
                    wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                    0,
                    0);
                polygonIndex = next;
                continue;
            }

            if (shadow)
            {
                // Remove the mask below the same lower polygon ID.
                Functions->vkCmdSetStencilCompareMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x3Fu);
                Functions->vkCmdSetStencilWriteMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x80u);
                Functions->vkCmdSetStencilReference(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, polygonId);
                Functions->vkCmdBindPipeline(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    useLinePipeline
                        ? ShadowClearLinePipelines[useDepthEqual ? 1u : 0u]
                        : ShadowClearPipelines[useDepthEqual ? 1u : 0u]);
                Functions->vkCmdPushConstants(
                    command,
                    PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(push),
                    &push);
                Functions->vkCmdDrawIndexed(
                    command,
                    drawCount,
                    1,
                    wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                    0,
                    0);

                // Blend only over surviving shadow-mask pixels.
                Functions->vkCmdSetStencilCompareMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x80u);
                Functions->vkCmdSetStencilWriteMask(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0x7Fu);
                Functions->vkCmdSetStencilReference(
                    command, VK_STENCIL_FACE_FRONT_AND_BACK, 0xC0u | polygonId);
                Functions->vkCmdBindPipeline(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    useLinePipeline
                        ? (useDepthEqual
                            ? DepthEqualShadowBlendLinePipelines[translucentPipelineIndex]
                            : ShadowBlendLinePipelines[translucentPipelineIndex])
                        : (useDepthEqual
                            ? DepthEqualShadowBlendPipelines[translucentPipelineIndex]
                            : ShadowBlendPipelines[translucentPipelineIndex]));
                push.Reserved[2] |= 2u;
                Functions->vkCmdPushConstants(
                    command,
                    PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(push),
                    &push);
                Functions->vkCmdDrawIndexed(
                    command,
                    drawCount,
                    1,
                    wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                    0,
                    0);
                polygonIndex = next;
                continue;
            }

            Functions->vkCmdSetStencilCompareMask(
                command,
                VK_STENCIL_FACE_FRONT_AND_BACK,
                useTranslucentPass ? 0x7Fu : 0xFFu);
            Functions->vkCmdSetStencilWriteMask(
                command,
                VK_STENCIL_FACE_FRONT_AND_BACK,
                useTranslucentPass ? 0x7Fu : 0xFFu);
            Functions->vkCmdSetStencilReference(
                command,
                VK_STENCIL_FACE_FRONT_AND_BACK,
                useTranslucentPass ? (0x40u | polygonId) : polygonId);
            Functions->vkCmdBindPipeline(
                command,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                useTranslucentPass
                    ? (useLinePipeline
                        ? (useDepthEqual
                            ? DepthEqualTranslucentLinePipelines[translucentPipelineIndex]
                            : TranslucentLinePipelines[translucentPipelineIndex])
                        : (useDepthEqual
                            ? DepthEqualTranslucentPipelines[translucentPipelineIndex]
                            : TranslucentPipelines[translucentPipelineIndex]))
                    : (useDepthEqual
                        ? (useLinePipeline ? DepthEqualLinePipeline : DepthEqualPipeline)
                        : (useLinePipeline ? LinePipeline : Pipeline)));
            if (useTranslucentPass)
                push.Reserved[2] |= 2u;
            Functions->vkCmdPushConstants(
                command,
                PipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(push),
                &push);
            Functions->vkCmdDrawIndexed(
                command,
                drawCount,
                1,
                wireframe ? polygon.EdgeIndexOffset : polygon.IndexOffset,
                0,
                0);
            polygonIndex = next;
            }
        }

        if ((frame.RenderDispCnt & (1u << 5u)) != 0)
        {
            Functions->vkCmdBindPipeline(
                command, VK_PIPELINE_BIND_POINT_GRAPHICS, EdgeMarkPipeline);
            Functions->vkCmdBindIndexBuffer(
                command, slot.EdgeIndex.Buffer, 0, VK_INDEX_TYPE_UINT16);
            for (const auto& polygon : frame.Upload.Polygons)
            {
                const bool shadowMask =
                    (polygon.Flags &
                     melonDS::Vulkan::VulkanRasterPolygonFlag_ShadowMask) != 0;
                if (shadowMask || polygon.EdgeIndexCount == 0)
                    continue;

                const bool textured =
                    (polygon.Flags &
                     melonDS::Vulkan::VulkanRasterPolygonFlag_Textured) != 0;
                const bool shadow =
                    (polygon.Flags &
                     melonDS::Vulkan::VulkanRasterPolygonFlag_Shadow) != 0;
                const std::uint32_t textureMode = (polygon.Attr >> 4u) & 0x3u;
                if (textured &&
                    (polygon.TextureLayer >= textureResources.size() ||
                     textureResources[polygon.TextureLayer] == nullptr ||
                     (textureMode > 2u && !shadow)))
                {
                    continue;
                }
                VkDescriptorSet descriptor = textureResources[0]->DescriptorSets[0];
                if (textured)
                {
                    const std::uint32_t sampler = std::min<std::uint32_t>(
                        frame.Textures[polygon.TextureLayer].SamplerIndex, 8u);
                    descriptor = textureResources[polygon.TextureLayer]
                        ->DescriptorSets[sampler];
                }
                if (descriptor == VK_NULL_HANDLE)
                    continue;
                Functions->vkCmdBindDescriptorSets(
                    command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    PipelineLayout,
                    0,
                    1,
                    &descriptor,
                    0,
                    nullptr);

                Push edgePush{};
                edgePush.ScreenSize[0] =
                    static_cast<float>(slot.Color.Extent.width);
                edgePush.ScreenSize[1] =
                    static_cast<float>(slot.Color.Extent.height);
                edgePush.Textured = textured ? 1u : 0u;
                edgePush.TextureMode = textureMode;
                edgePush.WBuffer =
                    (polygon.Flags &
                     melonDS::Vulkan::VulkanRasterPolygonFlag_WBuffer) != 0
                    ? 1u
                    : 0u;
                edgePush.Reserved[0] = frame.RenderXPos;
                edgePush.Reserved[1] = frame.RenderDispCnt;
                const bool wireframe = ((polygon.Attr >> 16u) & 0x1Fu) == 0u;
                edgePush.Reserved[2] = (wireframe ? 1u : 0u) | 4u |
                    ((frame.RenderAlphaRef & 0x1Fu) << 8u);
                Functions->vkCmdPushConstants(
                    command,
                    PipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(edgePush),
                    &edgePush);
                Functions->vkCmdDrawIndexed(
                    command,
                    polygon.EdgeIndexCount,
                    1,
                    polygon.EdgeIndexOffset,
                    0,
                    0);
            }
        }

        Functions->vkCmdEndRenderPass(command);
        slot.Color.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        slot.Attributes.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.DepthValue.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.Depth.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        if (slot.FinalFramebuffer == VK_NULL_HANDLE ||
            slot.FinalDescriptor == VK_NULL_HANDLE)
            return false;
        VkRenderPassBeginInfo finalBegin{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        finalBegin.renderPass = FinalRenderPass;
        finalBegin.framebuffer = slot.FinalFramebuffer;
        finalBegin.renderArea.extent = slot.Color.Extent;
        Functions->vkCmdBeginRenderPass(
            command, &finalBegin, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdSetViewport(command, 0, 1, &viewport);
        Functions->vkCmdSetScissor(command, 0, 1, &scissor);
        Functions->vkCmdBindDescriptorSets(
            command,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            FinalPipelineLayout,
            0,
            1,
            &slot.FinalDescriptor,
            0,
            nullptr);

        NativeRasterFinalPush finalPush{};
        finalPush.Width = slot.Color.Extent.width;
        finalPush.Height = slot.Color.Extent.height;
        finalPush.DispCnt = frame.RenderDispCnt;
        finalPush.AlphaRef = frame.RenderAlphaRef;
        finalPush.FogColor = frame.RenderFogColor;
        finalPush.FogOffset = frame.RenderFogOffset;
        finalPush.FogShift = frame.RenderFogShift;
        finalPush.ClearAttr = frame.RenderClearAttr1;
        for (std::size_t index = 0;
             index < frame.RenderFogDensityTable.size();
             ++index)
        {
            finalPush.FogDensityPacked[index / 4u] |=
                static_cast<std::uint32_t>(frame.RenderFogDensityTable[index])
                << ((index % 4u) * 8u);
        }
        for (std::size_t index = 0; index < frame.RenderEdgeTable.size(); ++index)
        {
            const std::uint32_t color = frame.RenderEdgeTable[index];
            std::uint32_t red = (color << 1u) & 0x3Eu;
            std::uint32_t green = (color >> 4u) & 0x3Eu;
            std::uint32_t blue = (color >> 9u) & 0x3Eu;
            if (red) ++red;
            if (green) ++green;
            if (blue) ++blue;
            finalPush.EdgeColorPacked[index] =
                red | (green << 8u) | (blue << 16u);
        }

        if ((frame.RenderDispCnt & (1u << 5u)) != 0)
        {
            Functions->vkCmdBindPipeline(
                command, VK_PIPELINE_BIND_POINT_GRAPHICS, FinalEdgePipeline);
            Functions->vkCmdPushConstants(
                command,
                FinalPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(finalPush),
                &finalPush);
            const float blendConstants[4]{};
            Functions->vkCmdSetBlendConstants(command, blendConstants);
            Functions->vkCmdDraw(command, 3, 1, 0, 0);
        }
        if ((frame.RenderDispCnt & (1u << 7u)) != 0)
        {
            const bool alphaOnly = (frame.RenderDispCnt & (1u << 6u)) != 0;
            Functions->vkCmdBindPipeline(
                command,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                alphaOnly ? FinalFogAlphaPipeline : FinalFogPipeline);
            Functions->vkCmdPushConstants(
                command,
                FinalPipelineLayout,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(finalPush),
                &finalPush);
            const float blendConstants[4] = {
                static_cast<float>(frame.RenderFogColor & 0x1Fu) / 31.0f,
                static_cast<float>((frame.RenderFogColor >> 5u) & 0x1Fu) / 31.0f,
                static_cast<float>((frame.RenderFogColor >> 10u) & 0x1Fu) / 31.0f,
                static_cast<float>((frame.RenderFogColor >> 16u) & 0x1Fu) / 31.0f,
            };
            Functions->vkCmdSetBlendConstants(command, blendConstants);
            Functions->vkCmdDraw(command, 3, 1, 0, 0);
        }
        Functions->vkCmdEndRenderPass(command);
        slot.Color.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.FrameSerial = frame.FrameSerial;
        slot.Generation = frame.Generation;
        views.HighResolution = slot.Color.View;
        views.NativeReference = slot.NativeReference.View;
        views.Sampler = Samplers[0];
        views.Valid = true;
        return true;
    }
};

NativeRasterGpu::NativeRasterGpu()
    : m_impl(std::make_unique<Impl>())
{
}

NativeRasterGpu::~NativeRasterGpu()
{
    Release();
}

bool NativeRasterGpu::Render(
    QVulkanWindow* window,
    QVulkanDeviceFunctions* functions,
    VkCommandBuffer command,
    int frameSlot,
    const NativeRasterFrame& frame,
    NativeRasterViews& views)
{
    views = {};
    if (!frame.Valid || frame.Scale <= 1)
        return true;
    if (!m_impl->Initialize(window, functions))
        return false;
    if (!m_impl->EnsureTargets(
            frame.Scale,
            window->concurrentFrameCount()))
        return false;
    return m_impl->Render(command, frameSlot, frame, views);
}

void NativeRasterGpu::Release()
{
    if (m_impl)
        m_impl->Release();
}

} // namespace MelonPrime::Vulkan
