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
constexpr VkFormat kColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UINT;
constexpr std::size_t kMaxResidentTextureIdentities = 128;
constexpr std::uint64_t kMaxResidentTextureBytes = 256ull * 1024ull * 1024ull;

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
    RenderXPos = 0;
    FrameSerial = 0;
    Generation = 0;
    Upload.Clear();
    Textures.clear();
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
        if (format == 0)
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
            continue;
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
                continue;
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
    options.TextureLayers = textureLayers.data();
    options.TextureLayerCount = textureLayers.size();
    std::string failure;
    if (!melonDS::Vulkan::BuildVulkanRasterUpload(
            gpu3D.RenderPolygonRAM.data(),
            gpu3D.RenderNumPolygons,
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

    frame.Scale = scale;
    frame.EngineAScreen = gpu.ScreenSwap ? 0u : 1u;
    frame.MasterBrightnessA = gpu.MasterBrightnessA;
    frame.RenderDispCnt = gpu3D.RenderDispCnt;
    frame.RenderClearAttr1 = gpu3D.RenderClearAttr1;
    frame.RenderClearAttr2 = gpu3D.RenderClearAttr2;
    frame.RenderXPos = gpu3D.GetRenderXPos();
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
        ImageResource Depth;
        ImageResource NativeReference;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        HostBuffer Vertex;
        HostBuffer Index;
        HostBuffer NativeReferenceStaging;
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
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
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
        DestroyImage(Functions, Device, slot.Color);
        DestroyImage(Functions, Device, slot.Depth);
        DestroyImage(Functions, Device, slot.NativeReference);
        DestroyHostBuffer(Functions, Device, slot.Vertex);
        DestroyHostBuffer(Functions, Device, slot.Index);
        DestroyHostBuffer(Functions, Device, slot.NativeReferenceStaging);
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
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (DescriptorPool)
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        for (VkSampler sampler : Samplers)
            if (sampler)
                Functions->vkDestroySampler(Device, sampler, nullptr);
        Pipeline = VK_NULL_HANDLE;
        PipelineLayout = VK_NULL_HANDLE;
        DescriptorPool = VK_NULL_HANDLE;
        DescriptorLayout = VK_NULL_HANDLE;
        RenderPass = VK_NULL_HANDLE;
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

        VkAttachmentDescription attachments[2]{};
        attachments[0].format = kColorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = DepthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
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
        passInfo.attachmentCount = 2;
        passInfo.pAttachments = attachments;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = 2;
        passInfo.pDependencies = dependencies;
        if (Functions->vkCreateRenderPass(
                Device, &passInfo, nullptr, &RenderPass) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorInfo.bindingCount = 1;
        descriptorInfo.pBindings = &binding;
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

        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 16384;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
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
        if (!CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterVertexSpirv),
                vertex) ||
            !CreateShader(
                melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPhase14NativeRasterFragmentSpirv),
                fragment))
        {
            if (vertex)
                Functions->vkDestroyShaderModule(Device, vertex, nullptr);
            if (fragment)
                Functions->vkDestroyShaderModule(Device, fragment, nullptr);
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
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &colorBlend;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
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
        Functions->vkDestroyShaderModule(Device, vertex, nullptr);
        Functions->vkDestroyShaderModule(Device, fragment, nullptr);
        if (pipelineResult != VK_SUCCESS)
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
                    slot.NativeReference))
            {
                return false;
            }
            VkImageView attachments[] = {
                slot.Color.View,
                slot.Depth.View,
            };
            VkFramebufferCreateInfo framebuffer{
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = RenderPass;
            framebuffer.attachmentCount = 2;
            framebuffer.pAttachments = attachments;
            framebuffer.width = extent.width;
            framebuffer.height = extent.height;
            framebuffer.layers = 1;
            if (Functions->vkCreateFramebuffer(
                    Device, &framebuffer, nullptr, &slot.Framebuffer) != VK_SUCCESS)
                return false;
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
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = version.DescriptorSets[sampler];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            Functions->vkUpdateDescriptorSets(
                Device, 1, &write, 0, nullptr);
        }

        return &version;
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
                slot.Index))
            return false;
        if (vertexBytes)
            std::memcpy(slot.Vertex.Map, frame.Upload.Vertices.data(), vertexBytes);
        if (indexBytes)
            std::memcpy(slot.Index.Map, frame.Upload.Indices.data(), indexBytes);
        if (!UploadNativeReference(command, slot, frame))
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
        VkClearValue clearValues[2]{};
        clearValues[0].color.float32[0] = clear.Color[0];
        clearValues[0].color.float32[1] = clear.Color[1];
        clearValues[0].color.float32[2] = clear.Color[2];
        clearValues[0].color.float32[3] = clear.Color[3];
        clearValues[1].depthStencil = {clear.Depth, clear.Stencil};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = RenderPass;
        begin.framebuffer = slot.Framebuffer;
        begin.renderArea.extent = slot.Color.Extent;
        begin.clearValueCount = 2;
        begin.pClearValues = clearValues;
        Functions->vkCmdBeginRenderPass(
            command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdBindPipeline(
            command, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
        const VkDeviceSize vertexOffset = 0;
        Functions->vkCmdBindVertexBuffers(
            command, 0, 1, &slot.Vertex.Buffer, &vertexOffset);
        Functions->vkCmdBindIndexBuffer(
            command, slot.Index.Buffer, 0, VK_INDEX_TYPE_UINT16);

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

        struct alignas(16) Push
        {
            float ScreenSize[2];
            std::uint32_t Textured;
            std::uint32_t TextureMode;
            std::uint32_t WBuffer;
            std::uint32_t Reserved[3];
        };
        static_assert(sizeof(Push) == 32);

        std::size_t polygonIndex = 0;
        while (polygonIndex < frame.Upload.Polygons.size())
        {
            const auto& polygon = frame.Upload.Polygons[polygonIndex];
            const bool unsupported =
                polygon.Primitive != static_cast<std::uint32_t>(
                    melonDS::Vulkan::VulkanRasterPrimitive::Triangles) ||
                (polygon.Flags & (
                    melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent |
                    melonDS::Vulkan::VulkanRasterPolygonFlag_ShadowMask |
                    melonDS::Vulkan::VulkanRasterPolygonFlag_Shadow)) != 0;
            const std::uint32_t textureMode = (polygon.Attr >> 4u) & 0x3u;
            const bool textured =
                (polygon.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Textured) != 0;
            const bool textureUnsupported = textured &&
                (polygon.TextureLayer >= textureResources.size() ||
                 textureResources[polygon.TextureLayer] == nullptr ||
                 textureMode > 1u);
            if (unsupported || textureUnsupported || polygon.IndexCount == 0)
            {
                ++polygonIndex;
                continue;
            }

            std::uint32_t drawCount = polygon.IndexCount;
            std::size_t next = polygonIndex + 1;
            while (next < frame.Upload.Polygons.size())
            {
                const auto& candidate = frame.Upload.Polygons[next];
                const bool candidateTextured =
                    (candidate.Flags & melonDS::Vulkan::VulkanRasterPolygonFlag_Textured) != 0;
                if (candidate.Primitive != polygon.Primitive ||
                    candidate.Flags != polygon.Flags ||
                    candidate.TextureLayer != polygon.TextureLayer ||
                    ((candidate.Attr >> 4u) & 0x3u) != textureMode ||
                    candidate.IndexOffset != polygon.IndexOffset + drawCount)
                {
                    break;
                }
                const bool candidateUnsupported =
                    (candidate.Flags & (
                        melonDS::Vulkan::VulkanRasterPolygonFlag_Translucent |
                        melonDS::Vulkan::VulkanRasterPolygonFlag_ShadowMask |
                        melonDS::Vulkan::VulkanRasterPolygonFlag_Shadow)) != 0 ||
                    (candidateTextured &&
                     (candidate.TextureLayer >= textureResources.size() ||
                      textureResources[candidate.TextureLayer] == nullptr ||
                      textureMode > 1u));
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
                polygon.IndexOffset,
                0,
                0);
            polygonIndex = next;
        }

        Functions->vkCmdEndRenderPass(command);
        slot.Color.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.Depth.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
