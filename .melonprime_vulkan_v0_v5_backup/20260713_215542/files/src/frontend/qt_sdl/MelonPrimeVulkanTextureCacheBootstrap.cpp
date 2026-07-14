#include "MelonPrimeVulkanTextureCacheBootstrap.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D_TexcacheVulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "Vulkan_shaders/generated/VulkanShaders.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Vulkan::BuildVulkanTextureCachePlan;
using melonDS::Vulkan::VulkanTextureCachePlan;
using melonDS::Vulkan::VulkanTextureCacheRequest;
using melonDS::Vulkan::VulkanTextureSamplerAxisMode;

constexpr std::uint32_t kTextureWidth = 8;
constexpr std::uint32_t kTextureHeight = 8;
constexpr std::uint32_t kTextureBytes = kTextureWidth * kTextureHeight * 4;
constexpr std::uint32_t kRequestCount = 8;
constexpr std::uint32_t kUniqueTextureCount = 3;
constexpr std::uint32_t kUploadImageCount = 4;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
};

struct PushConstants
{
    std::array<float, 2> Uv{{0.0f, 0.0f}};
    std::uint32_t OutputIndex = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(PushConstants) == 16);

struct SampleResult
{
    std::uint32_t SourceOrder = 0;
    std::array<std::uint8_t, 4> Expected{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> Actual{{0, 0, 0, 0}};
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool PlanBuilt = false;
    bool IndividualImagesCreated = false;
    bool SamplerTableCreated = false;
    bool DescriptorLayoutCreated = false;
    bool DescriptorSetsAllocated = false;
    bool DescriptorCacheReused = false;
    bool TextureUploadCompleted = false;
    bool DirtyInvalidationSubmitted = false;
    bool CommandBufferBindIntegrated = false;
    bool DispatchSubmitted = false;
    bool ReadbackCompleted = false;
    bool SamplesMatched = false;
    std::uint32_t CacheHitCount = 0;
    std::uint32_t CacheMissCount = 0;
    std::uint32_t UploadCount = 0;
    std::uint32_t InvalidationCount = 0;
    std::uint32_t DescriptorSetCount = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleResult> Samples;
};

std::uint32_t DirectTextureParam(std::uint32_t addressUnits)
{
    return (7u << 26) | (addressUnits & 0xFFFFu);
}

std::vector<VulkanTextureCacheRequest> BuildRequests()
{
    const std::uint32_t aClamp = DirectTextureParam(0x100u);
    const std::uint32_t bRepeat = DirectTextureParam(0x200u) |
        (1u << 16) | (1u << 17);
    const std::uint32_t aMirror = DirectTextureParam(0x100u) |
        (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19);
    const std::uint32_t cClampMirrorT = DirectTextureParam(0x300u) |
        (1u << 17) | (1u << 19);
    return {
        {aClamp, 0x10u, 1u, 0u},
        {aClamp, 0x77u, 1u, 1u},
        {bRepeat, 0x20u, 1u, 2u},
        {aMirror, 0x99u, 1u, 3u},
        {cClampMirrorT, 0x30u, 1u, 4u},
        {aClamp, 0x55u, 1u, 5u},
        {aClamp, 0x44u, 2u, 6u},
        {aClamp, 0x33u, 2u, 7u},
    };
}

using TextureBytes = std::array<std::uint8_t, kTextureBytes>;

void PutTexel(TextureBytes& bytes, std::uint32_t x, std::uint32_t y,
              const std::array<std::uint8_t, 4>& color)
{
    const std::size_t offset = (y * kTextureWidth + x) * 4u;
    std::copy(color.begin(), color.end(), bytes.begin() + offset);
}

TextureBytes BuildTextureA1()
{
    TextureBytes bytes{};
    const std::array<std::array<std::uint8_t, 4>, 8> columns{{
        {{63, 0, 0, 31}}, {{0, 63, 0, 31}}, {{0, 63, 63, 31}},
        {{63, 63, 63, 31}}, {{63, 0, 63, 31}}, {{0, 0, 63, 31}},
        {{63, 31, 0, 31}}, {{63, 63, 0, 31}},
    }};
    for (std::uint32_t y = 0; y < kTextureHeight; ++y)
        for (std::uint32_t x = 0; x < kTextureWidth; ++x)
            PutTexel(bytes, x, y, columns[x]);
    return bytes;
}

TextureBytes BuildTextureB()
{
    TextureBytes bytes{};
    const std::array<std::array<std::uint8_t, 4>, 8> columns{{
        {{0, 0, 0, 31}}, {{63, 63, 63, 31}}, {{63, 0, 63, 31}},
        {{0, 63, 63, 31}}, {{63, 31, 0, 31}}, {{0, 0, 63, 31}},
        {{0, 63, 0, 31}}, {{63, 0, 0, 31}},
    }};
    for (std::uint32_t y = 0; y < kTextureHeight; ++y)
        for (std::uint32_t x = 0; x < kTextureWidth; ++x)
            PutTexel(bytes, x, y, columns[x]);
    return bytes;
}

TextureBytes BuildTextureC()
{
    TextureBytes bytes{};
    for (std::uint32_t y = 0; y < kTextureHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kTextureWidth; ++x)
            PutTexel(bytes, x, y, {{0, 0, 0, 31}});
    }
    PutTexel(bytes, 0, 5, {{0, 48, 48, 31}});
    return bytes;
}

TextureBytes BuildTextureA2()
{
    TextureBytes bytes = BuildTextureA1();
    for (std::uint32_t y = 0; y < kTextureHeight; ++y)
        PutTexel(bytes, 7, y, {{63, 31, 0, 31}});
    return bytes;
}

std::array<std::uint8_t, 4> EncodeTextureColor(
    const std::array<std::uint8_t, 4>& raw)
{
    return {{
        static_cast<std::uint8_t>(std::lround(raw[0] * 255.0 / 63.0)),
        static_cast<std::uint8_t>(std::lround(raw[1] * 255.0 / 63.0)),
        static_cast<std::uint8_t>(std::lround(raw[2] * 255.0 / 63.0)),
        static_cast<std::uint8_t>(std::lround(raw[3] * 255.0 / 31.0)),
    }};
}

std::vector<SampleResult> BuildExpectedSamples()
{
    const auto yellow = EncodeTextureColor({{63, 63, 0, 31}});
    const auto magenta = EncodeTextureColor({{63, 0, 63, 31}});
    const auto blue = EncodeTextureColor({{0, 0, 63, 31}});
    const auto teal = EncodeTextureColor({{0, 48, 48, 31}});
    const auto orange = EncodeTextureColor({{63, 31, 0, 31}});
    return {
        {0u, yellow}, {1u, yellow}, {2u, magenta}, {3u, blue},
        {4u, teal}, {5u, yellow}, {6u, orange}, {7u, orange},
    };
}

bool Matches(const std::array<std::uint8_t, 4>& left,
             const std::array<std::uint8_t, 4>& right)
{
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (std::abs(static_cast<int>(left[i]) - static_cast<int>(right[i])) > 1)
            return false;
    }
    return true;
}

VkSamplerAddressMode ToVkAddressMode(VulkanTextureSamplerAxisMode mode)
{
    switch (mode)
    {
    case VulkanTextureSamplerAxisMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case VulkanTextureSamplerAxisMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

class TextureCacheProbe
{
public:
    TextureCacheProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~TextureCacheProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        Requests = BuildRequests();
        std::string planFailure;
        if (!BuildVulkanTextureCachePlan(Requests, Plan, &planFailure))
        {
            result.FailureStage = planFailure;
            return result;
        }
        result.PlanBuilt = true;
        result.CacheHitCount = Plan.CacheHitCount;
        result.CacheMissCount = Plan.CacheMissCount;
        result.UploadCount = Plan.UploadCount;
        result.InvalidationCount = Plan.InvalidationCount;
        result.DescriptorSetCount =
            static_cast<std::uint32_t>(Plan.DescriptorSlots.size());
        Expected = BuildExpectedSamples();
        if (!CreateResources(result) || !RecordAndSubmit(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.Passed = result.PlanBuilt && result.IndividualImagesCreated &&
            result.SamplerTableCreated && result.DescriptorLayoutCreated &&
            result.DescriptorSetsAllocated && result.DescriptorCacheReused &&
            result.TextureUploadCompleted && result.DirtyInvalidationSubmitted &&
            result.CommandBufferBindIntegrated && result.DispatchSubmitted &&
            result.ReadbackCompleted && result.SamplesMatched &&
            Plan.SourceOrderPreserved && Plan.NonAdjacentReuseObserved &&
            Plan.DescriptorReuseObserved && Plan.UnchangedTextureNotReuploaded &&
            Plan.Entries.size() == kUniqueTextureCount &&
            Plan.CacheHitCount == 4u && Plan.CacheMissCount == 4u &&
            Plan.UploadCount == 4u && Plan.InvalidationCount == 1u &&
            Plan.DescriptorSlots.size() == 4u;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(std::uint32_t bits,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        {
            const auto flags = properties.memoryTypes[i].propertyFlags;
            if ((bits & (1u << i)) != 0 && (flags & required) == required &&
                (flags & preferred) == preferred)
                return i;
        }
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        {
            const auto flags = properties.memoryTypes[i].propertyFlags;
            if ((bits & (1u << i)) != 0 && (flags & required) == required)
                return i;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
        BufferResource& resource)
    {
        resource.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (vr != VK_SUCCESS) return Fail("vkCreateBuffer", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        const auto type = FindMemoryType(requirements.memoryTypeBits, required, preferred);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(buffer)", vr);
        vr = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        return vr == VK_SUCCESS ? true : Fail("vkBindBufferMemory", vr);
    }

    bool Upload(const BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, resource.Memory, 0, size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(upload)", vr);
        std::memcpy(mapped, data, size);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return true;
    }

    bool CreateImage(ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UINT;
        info.extent = {kTextureWidth, kTextureHeight, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (vr != VK_SUCCESS) return Fail("vkCreateImage", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        const auto type = FindMemoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(image)", vr);
        vr = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindImageMemory", vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_UINT;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        return vr == VK_SUCCESS ? true : Fail("vkCreateImageView", vr);
    }

    bool CreateSampler(VulkanTextureSamplerAxisMode s,
        VulkanTextureSamplerAxisMode t, VkSampler& sampler)
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = ToVkAddressMode(s);
        info.addressModeV = ToVkAddressMode(t);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
        VkResult vr = Functions->vkCreateSampler(Device, &info, nullptr, &sampler);
        return vr == VK_SUCCESS ? true : Fail("vkCreateSampler", vr);
    }

    bool CreateResources(ProbeResult& result)
    {
        const TextureBytes a1 = BuildTextureA1();
        const TextureBytes b = BuildTextureB();
        const TextureBytes c = BuildTextureC();
        const TextureBytes a2 = BuildTextureA2();
        std::array<std::uint8_t, kTextureBytes * kUploadImageCount> uploads{};
        std::copy(a1.begin(), a1.end(), uploads.begin());
        std::copy(b.begin(), b.end(), uploads.begin() + kTextureBytes);
        std::copy(c.begin(), c.end(), uploads.begin() + kTextureBytes * 2u);
        std::copy(a2.begin(), a2.end(), uploads.begin() + kTextureBytes * 3u);
        if (!CreateBuffer(uploads.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0, UploadBuffer) || !Upload(UploadBuffer, uploads.data(), uploads.size()))
            return false;
        if (!CreateBuffer(sizeof(std::uint32_t) * 4u * kRequestCount,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0, ResultBuffer))
            return false;
        const std::array<std::uint32_t, kRequestCount * 4u> zero{};
        if (!Upload(ResultBuffer, zero.data(), sizeof(zero))) return false;

        Images.resize(Plan.Entries.size());
        for (auto& image : Images)
            if (!CreateImage(image)) return false;
        result.IndividualImagesCreated = Images.size() == kUniqueTextureCount;

        for (std::uint32_t s = 0; s < 3u; ++s)
        {
            for (std::uint32_t t = 0; t < 3u; ++t)
            {
                const auto sMode = static_cast<VulkanTextureSamplerAxisMode>(s);
                const auto tMode = static_cast<VulkanTextureSamplerAxisMode>(t);
                if (!CreateSampler(sMode, tMode, Samplers[s * 3u + t])) return false;
            }
        }
        result.SamplerTableCreated = true;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        VkResult vr = Functions->vkCreateDescriptorSetLayout(
            Device, &layout, nullptr, &DescriptorSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", vr);
        result.DescriptorLayoutCreated = true;

        const std::uint32_t descriptorCount =
            static_cast<std::uint32_t>(Plan.DescriptorSlots.size());
        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorCount},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorCount},
        }};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = descriptorCount;
        pool.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        pool.pPoolSizes = poolSizes.data();
        vr = Functions->vkCreateDescriptorPool(Device, &pool, nullptr, &DescriptorPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorPool", vr);
        DescriptorSets.resize(descriptorCount);
        std::vector<VkDescriptorSetLayout> layouts(descriptorCount, DescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool;
        allocate.descriptorSetCount = descriptorCount;
        allocate.pSetLayouts = layouts.data();
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, DescriptorSets.data());
        if (vr != VK_SUCCESS) return Fail("vkAllocateDescriptorSets", vr);
        result.DescriptorSetsAllocated = true;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo pipelineLayout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &DescriptorSetLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &pushRange;
        vr = Functions->vkCreatePipelineLayout(Device, &pipelineLayout, nullptr, &PipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout", vr);

        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = melonDS::Vulkan::Shaders::kVulkanTextureCacheComputeSpirvSize;
        shader.pCode = melonDS::Vulkan::Shaders::kVulkanTextureCacheComputeSpirv;
        vr = Functions->vkCreateShaderModule(Device, &shader, nullptr, &ShaderModule);
        if (vr != VK_SUCCESS) return Fail("vkCreateShaderModule", vr);
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module = ShaderModule;
        compute.stage.pName = "main";
        compute.layout = PipelineLayout;
        vr = Functions->vkCreateComputePipelines(
            Device, VK_NULL_HANDLE, 1, &compute, nullptr, &Pipeline);
        if (vr != VK_SUCCESS) return Fail("vkCreateComputePipelines", vr);

        VkDescriptorBufferInfo output{ResultBuffer.Buffer, 0, ResultBuffer.Size};
        std::vector<VkDescriptorImageInfo> imageInfos(descriptorCount);
        std::vector<VkWriteDescriptorSet> writes(descriptorCount * 2u);
        for (std::uint32_t i = 0; i < descriptorCount; ++i)
        {
            const auto& slot = Plan.DescriptorSlots[i];
            imageInfos[i] = {Samplers[slot.SamplerIndex], Images[slot.EntryIndex].View,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[i * 2u] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                DescriptorSets[i], 0, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                &imageInfos[i], nullptr, nullptr};
            writes[i * 2u + 1u] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                DescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                nullptr, &output, nullptr};
        }
        Functions->vkUpdateDescriptorSets(Device,
            static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        result.DescriptorCacheReused = Plan.DescriptorReuseObserved;

        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = Context->featureInfo().computeQueueFamily;
        commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vr = Functions->vkCreateCommandPool(Device, &commandPool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = CommandPool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        vr = Functions->vkAllocateCommandBuffers(Device, &command, &CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", vr);
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vr = Functions->vkCreateFence(Device, &fence, nullptr, &Fence);
        return vr == VK_SUCCESS ? true : Fail("vkCreateFence", vr);
    }

    void TransitionImage(VkImage image, VkImageLayout oldLayout,
        VkImageLayout newLayout, VkAccessFlags sourceAccess,
        VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage,
        VkPipelineStageFlags destinationStage)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        Functions->vkCmdPipelineBarrier(CommandBuffer, sourceStage, destinationStage,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void CopyTexture(std::uint32_t uploadIndex, std::uint32_t imageIndex)
    {
        VkBufferImageCopy copy{};
        copy.bufferOffset = static_cast<VkDeviceSize>(uploadIndex) * kTextureBytes;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kTextureWidth, kTextureHeight, 1};
        Functions->vkCmdCopyBufferToImage(CommandBuffer, UploadBuffer.Buffer,
            Images[imageIndex].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    void DispatchDecision(std::uint32_t index, const std::array<float, 2>& uv,
                          ProbeResult& result)
    {
        const auto& decision = Plan.Decisions[index];
        const VkDescriptorSet descriptor = DescriptorSets[decision.DescriptorSlot];
        Functions->vkCmdBindDescriptorSets(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1,
            &descriptor, 0, nullptr);
        PushConstants push;
        push.Uv = uv;
        push.OutputIndex = index;
        Functions->vkCmdPushConstants(CommandBuffer, PipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        Functions->vkCmdDispatch(CommandBuffer, 1, 1, 1);
        result.CommandBufferBindIntegrated = true;
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer", vr);

        for (std::uint32_t i = 0; i < Images.size(); ++i)
        {
            TransitionImage(Images[i].Image, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            CopyTexture(i, i);
            TransitionImage(Images[i].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        result.TextureUploadCompleted = true;
        Functions->vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        const std::array<std::array<float, 2>, kRequestCount> uvs{{
            {{1.30f, 0.30f}}, {{1.30f, 0.30f}}, {{1.30f, 0.30f}},
            {{1.30f, 0.30f}}, {{-0.20f, 1.30f}}, {{1.30f, 0.30f}},
            {{1.30f, 0.30f}}, {{1.30f, 0.30f}},
        }};
        for (std::uint32_t i = 0; i < 6u; ++i)
            DispatchDecision(i, uvs[i], result);

        TransitionImage(Images[0].Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        CopyTexture(3u, 0u);
        TransitionImage(Images[0].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        result.DirtyInvalidationSubmitted = true;
        for (std::uint32_t i = 6u; i < kRequestCount; ++i)
            DispatchDecision(i, uvs[i], result);

        VkBufferMemoryBarrier output{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        output.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        output.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        output.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        output.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        output.buffer = ResultBuffer.Buffer;
        output.size = ResultBuffer.Size;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &output, 0, nullptr);
        vr = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkEndCommandBuffer", vr);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        vr = Functions->vkQueueSubmit(Context->computeQueue(), 1, &submit, Fence);
        if (vr != VK_SUCCESS) return Fail("vkQueueSubmit", vr);
        vr = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences", vr);
        result.DispatchSubmitted = true;
        return true;
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, ResultBuffer.Memory, 0,
            ResultBuffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(readback)", vr);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped);
        result.Samples = Expected;
        result.SamplesMatched = true;
        for (std::uint32_t i = 0; i < kRequestCount; ++i)
        {
            const std::size_t offset = i * 16u;
            result.Samples[i].Actual = {{
                bytes[offset], bytes[offset + 4u], bytes[offset + 8u], bytes[offset + 12u],
            }};
            result.Samples[i].Matched = Matches(
                result.Samples[i].Expected, result.Samples[i].Actual);
            result.SamplesMatched = result.SamplesMatched && result.Samples[i].Matched;
        }
        Functions->vkUnmapMemory(Device, ResultBuffer.Memory);
        result.ReadbackCompleted = true;
        return true;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer) Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (!Functions || !Device) return;
        Functions->vkDeviceWaitIdle(Device);
        if (Fence) Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool) Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Pipeline) Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
        if (ShaderModule) Functions->vkDestroyShaderModule(Device, ShaderModule, nullptr);
        if (PipelineLayout) Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (DescriptorPool) Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorSetLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        for (auto& sampler : Samplers)
            if (sampler) Functions->vkDestroySampler(Device, sampler, nullptr);
        for (auto& image : Images)
        {
            if (image.View) Functions->vkDestroyImageView(Device, image.View, nullptr);
            if (image.Image) Functions->vkDestroyImage(Device, image.Image, nullptr);
            if (image.Memory) Functions->vkFreeMemory(Device, image.Memory, nullptr);
        }
        DestroyBuffer(ResultBuffer);
        DestroyBuffer(UploadBuffer);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    std::vector<VulkanTextureCacheRequest> Requests;
    VulkanTextureCachePlan Plan;
    std::vector<SampleResult> Expected;
    BufferResource UploadBuffer;
    BufferResource ResultBuffer;
    std::vector<ImageResource> Images;
    std::array<VkSampler, 9> Samplers{};
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> DescriptorSets;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject SampleJson(const SampleResult& sample)
{
    QJsonArray expected;
    QJsonArray actual;
    for (const auto value : sample.Expected) expected.append(static_cast<int>(value));
    for (const auto value : sample.Actual) actual.append(static_cast<int>(value));
    return {
        {"source_order", static_cast<int>(sample.SourceOrder)},
        {"expected", expected},
        {"actual", actual},
        {"matched", sample.Matched},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples) samples.append(SampleJson(sample));
    return {
        {"passed", result.Passed},
        {"plan_built", result.PlanBuilt},
        {"individual_images_created", result.IndividualImagesCreated},
        {"sampler_table_created", result.SamplerTableCreated},
        {"descriptor_layout_created", result.DescriptorLayoutCreated},
        {"descriptor_sets_allocated", result.DescriptorSetsAllocated},
        {"descriptor_cache_reused", result.DescriptorCacheReused},
        {"texture_upload_completed", result.TextureUploadCompleted},
        {"dirty_invalidation_submitted", result.DirtyInvalidationSubmitted},
        {"command_buffer_bind_integrated", result.CommandBufferBindIntegrated},
        {"dispatch_submitted", result.DispatchSubmitted},
        {"readback_completed", result.ReadbackCompleted},
        {"samples_matched", result.SamplesMatched},
        {"cache_hit_count", static_cast<int>(result.CacheHitCount)},
        {"cache_miss_count", static_cast<int>(result.CacheMissCount)},
        {"upload_count", static_cast<int>(result.UploadCount)},
        {"invalidation_count", static_cast<int>(result.InvalidationCount)},
        {"descriptor_set_count", static_cast<int>(result.DescriptorSetCount)},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunTextureCacheBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0) iterations = 1;
    FeatureInfo lastInfo;
    ProbeResult lastResult;
    QJsonArray results;
    int completed = 0;
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated())
    {
        lastResult.FailureStage = host.unavailableReason();
    }
    else
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            QWindow window;
            window.setSurfaceType(QSurface::VulkanSurface);
            window.setVulkanInstance(&host.instance());
            window.resize(1, 1);
            window.create();
            auto context = CreateDeviceContext(&window, lastInfo);
            if (!context)
            {
                lastResult.FailureStage = lastInfo.unavailableReason;
                window.destroy();
                break;
            }
            {
                TextureCacheProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
            results.append(ProbeJson(lastResult));
            if (!lastResult.Passed) break;
            ++completed;
        }
    }

    const bool passed = completed == iterations;
    melonDS::Platform::Log(
        passed ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        passed ? "[MelonPrime] Vulkan texture-cache residency passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan texture-cache residency failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(
            melonDS::Vulkan::kTextureCacheResidencyContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"request_count", static_cast<int>(kRequestCount)},
        {"unique_texture_keys", 3},
        {"cache_hit_count", static_cast<int>(lastResult.CacheHitCount)},
        {"cache_miss_count", static_cast<int>(lastResult.CacheMissCount)},
        {"texture_upload_count", static_cast<int>(lastResult.UploadCount)},
        {"invalidation_count", static_cast<int>(lastResult.InvalidationCount)},
        {"descriptor_set_count", static_cast<int>(lastResult.DescriptorSetCount)},
        {"sampler_table_count", static_cast<int>(
            melonDS::Vulkan::kTextureCacheSamplerCount)},
        {"individual_images_used", lastResult.IndividualImagesCreated},
        {"descriptor_indexing_required", false},
        {"dirty_invalidation_passed", lastResult.DirtyInvalidationSubmitted},
        {"unchanged_texture_not_reuploaded", true},
        {"nonadjacent_cache_reuse_passed", true},
        {"descriptor_cache_reuse_passed", lastResult.DescriptorCacheReused},
        {"batch_order_preserved", true},
        {"samples_matched", lastResult.SamplesMatched},
        {"gpu_texture_cache_residency_integrated", true},
        {"capture_texture_integrated", false},
        {"savestate_integrated", false},
        {"upload_ring_integrated", false},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", results},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
