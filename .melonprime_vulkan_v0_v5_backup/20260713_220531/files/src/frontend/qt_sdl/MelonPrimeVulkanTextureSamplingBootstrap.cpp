#include "MelonPrimeVulkanTextureSamplingBootstrap.h"

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

#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "Vulkan_shaders/generated/VulkanShaders.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Vulkan::EvaluateVulkanTextureCombiner;
using melonDS::Vulkan::QuantizeVulkanColor8;
using melonDS::Vulkan::VulkanTextureCombinerInput;
using melonDS::Vulkan::VulkanTextureCombinerMode;
using melonDS::Vulkan::VulkanToonHighlightConfig;
using melonDS::Vulkan::VulkanToonHighlightMode;

constexpr std::uint32_t kTextureWidth = 4;
constexpr std::uint32_t kTextureHeight = 4;
constexpr std::uint32_t kResultCount = 7;

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

struct SampleResult
{
    const char* Name = nullptr;
    std::array<std::uint8_t, 4> Expected{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> Actual{{0, 0, 0, 0}};
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool IntegerTextureCreated = false;
    bool TextureUploadCompleted = false;
    bool ClampSamplerCreated = false;
    bool RepeatSamplerCreated = false;
    bool MirrorSamplerCreated = false;
    bool DescriptorLayoutCreated = false;
    bool DescriptorSetAllocated = false;
    bool DescriptorUpdateIntegrated = false;
    bool ComputePipelineCreated = false;
    bool CommandBufferBindIntegrated = false;
    bool DispatchSubmitted = false;
    bool ReadbackCompleted = false;
    bool ClampPassed = false;
    bool RepeatPassed = false;
    bool MirrorPassed = false;
    bool ModulatePassed = false;
    bool DecalPassed = false;
    bool ToonTexturedPassed = false;
    bool HighlightTexturedPassed = false;
    bool SamplesMatched = false;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleResult> Samples;
};

std::array<std::uint8_t, kTextureWidth * kTextureHeight * 4> BuildTexture()
{
    std::array<std::uint8_t, kTextureWidth * kTextureHeight * 4> bytes{};
    const std::array<std::array<std::uint8_t, 4>, 4> columns{{
        {{63, 0, 0, 31}},
        {{0, 63, 0, 16}},
        {{0, 0, 63, 8}},
        {{63, 63, 0, 31}},
    }};
    for (std::uint32_t y = 0; y < kTextureHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kTextureWidth; ++x)
        {
            const std::size_t offset = (y * kTextureWidth + x) * 4u;
            std::copy(columns[x].begin(), columns[x].end(), bytes.begin() + offset);
        }
    }
    return bytes;
}

std::array<float, 4> DecodeTexel(const std::array<std::uint8_t, 4>& value)
{
    return {{
        static_cast<float>(value[0]) / 63.0f,
        static_cast<float>(value[1]) / 63.0f,
        static_cast<float>(value[2]) / 63.0f,
        static_cast<float>(value[3]) / 31.0f,
    }};
}

bool Matches(const std::array<std::uint8_t, 4>& a,
             const std::array<std::uint8_t, 4>& b)
{
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > 1)
            return false;
    }
    return true;
}

std::array<std::uint16_t, 32> BuildToonTable()
{
    std::array<std::uint16_t, 32> table{};
    table[10] = 31u;          // red
    table[15] = 31u << 10;    // blue
    return table;
}

std::vector<SampleResult> BuildExpected()
{
    const std::array<float, 4> red = DecodeTexel({{63, 0, 0, 31}});
    const std::array<float, 4> green = DecodeTexel({{0, 63, 0, 16}});
    const std::array<float, 4> blue = DecodeTexel({{0, 0, 63, 8}});
    const std::array<float, 4> yellow = DecodeTexel({{63, 63, 0, 31}});
    std::vector<SampleResult> samples;
    samples.reserve(kResultCount);
    samples.push_back({"clamp", QuantizeVulkanColor8(yellow)});
    samples.push_back({"repeat", QuantizeVulkanColor8(green)});
    samples.push_back({"mirror", QuantizeVulkanColor8(blue)});

    VulkanTextureCombinerInput input;
    input.Mode = VulkanTextureCombinerMode::Modulate;
    input.VertexColor = {{0.5f, 1.0f, 0.25f, 16.0f / 31.0f}};
    input.TextureColor = green;
    samples.push_back({"modulate", QuantizeVulkanColor8(
        EvaluateVulkanTextureCombiner(input))});

    input = {};
    input.Mode = VulkanTextureCombinerMode::Decal;
    input.VertexColor = {{1.0f, 0.0f, 0.0f, 20.0f / 31.0f}};
    input.TextureColor = blue;
    samples.push_back({"decal", QuantizeVulkanColor8(
        EvaluateVulkanTextureCombiner(input))});

    input = {};
    input.Mode = VulkanTextureCombinerMode::Toon;
    input.VertexColor = {{10.0f / 31.0f, 0.4f, 0.6f, 1.0f}};
    input.TextureColor = red;
    input.ToonColor = {{1.0f, 0.0f, 0.0f, 1.0f}};
    samples.push_back({"toon_textured", QuantizeVulkanColor8(
        EvaluateVulkanTextureCombiner(input))});

    input = {};
    input.Mode = VulkanTextureCombinerMode::Highlight;
    input.VertexColor = {{15.0f / 31.0f, 0.2f, 0.8f, 1.0f}};
    input.TextureColor = green;
    input.ToonColor = {{0.0f, 0.0f, 1.0f, 1.0f}};
    samples.push_back({"highlight_textured", QuantizeVulkanColor8(
        EvaluateVulkanTextureCombiner(input))});
    return samples;
}

class TextureSamplingProbe
{
public:
    TextureSamplingProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~TextureSamplingProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        Expected = BuildExpected();
        if (!CreateResources(result) || !RecordAndSubmit(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.Passed = result.IntegerTextureCreated &&
            result.TextureUploadCompleted && result.ClampSamplerCreated &&
            result.RepeatSamplerCreated && result.MirrorSamplerCreated &&
            result.DescriptorLayoutCreated && result.DescriptorSetAllocated &&
            result.DescriptorUpdateIntegrated && result.ComputePipelineCreated &&
            result.CommandBufferBindIntegrated && result.DispatchSubmitted &&
            result.ReadbackCompleted && result.SamplesMatched;
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
            if ((bits & (1u << i)) && (flags & required) == required &&
                (flags & preferred) == preferred)
                return i;
        }
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        {
            const auto flags = properties.memoryTypes[i].propertyFlags;
            if ((bits & (1u << i)) && (flags & required) == required)
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

    bool CreateTexture(ProbeResult& result)
    {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = VK_FORMAT_R8G8B8A8_UINT;
        image.extent = {kTextureWidth, kTextureHeight, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &image, nullptr, &Texture.Image);
        if (vr != VK_SUCCESS) return Fail("vkCreateImage(texture)", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, Texture.Image, &requirements);
        const auto type = FindMemoryType(requirements.memoryTypeBits, 0,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("texture memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &Texture.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(texture)", vr);
        vr = Functions->vkBindImageMemory(Device, Texture.Image, Texture.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindImageMemory(texture)", vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = Texture.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.format = image.format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &Texture.View);
        if (vr != VK_SUCCESS) return Fail("vkCreateImageView(texture)", vr);
        result.IntegerTextureCreated = true;
        return true;
    }

    bool CreateSampler(VkSamplerAddressMode mode, VkSampler& sampler)
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = mode;
        info.addressModeV = mode;
        info.addressModeW = mode;
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
        info.maxAnisotropy = 1.0f;
        const VkResult vr = Functions->vkCreateSampler(Device, &info, nullptr, &sampler);
        return vr == VK_SUCCESS ? true : Fail("vkCreateSampler", vr);
    }

    bool CreateResources(ProbeResult& result)
    {
        const auto textureBytes = BuildTexture();
        const auto toonTable = BuildToonTable();
        Config = melonDS::Vulkan::BuildVulkanToonHighlightConfig(
            toonTable, 0u, VulkanToonHighlightMode::None, true);
        if (!CreateBuffer(textureBytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0, TextureUpload) || !Upload(TextureUpload, textureBytes.data(), textureBytes.size()))
            return false;
        if (!CreateBuffer(sizeof(Config), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0, ConfigBuffer) || !Upload(ConfigBuffer, &Config, sizeof(Config)))
            return false;
        if (!CreateBuffer(sizeof(std::uint32_t) * 4u * 8u,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0, ResultBuffer))
            return false;
        const std::array<std::uint32_t, 8 * 4> zero{};
        if (!Upload(ResultBuffer, zero.data(), sizeof(zero)) || !CreateTexture(result))
            return false;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, ClampSampler)) return false;
        result.ClampSamplerCreated = true;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, RepeatSampler)) return false;
        result.RepeatSamplerCreated = true;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, MirrorSampler)) return false;
        result.MirrorSamplerCreated = true;

        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        for (std::uint32_t i = 1; i <= 3; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        VkResult vr = Functions->vkCreateDescriptorSetLayout(
            Device, &layout, nullptr, &DescriptorSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout", vr);
        result.DescriptorLayoutCreated = true;

        std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        }};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        pool.pPoolSizes = poolSizes.data();
        vr = Functions->vkCreateDescriptorPool(Device, &pool, nullptr, &DescriptorPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorPool", vr);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &DescriptorSetLayout;
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, &DescriptorSet);
        if (vr != VK_SUCCESS) return Fail("vkAllocateDescriptorSets", vr);
        result.DescriptorSetAllocated = true;

        VkPipelineLayoutCreateInfo pipelineLayout{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &DescriptorSetLayout;
        vr = Functions->vkCreatePipelineLayout(Device, &pipelineLayout, nullptr, &PipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout", vr);

        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = melonDS::Vulkan::Shaders::kVulkanTextureSamplingComputeSpirvSize;
        shader.pCode = melonDS::Vulkan::Shaders::kVulkanTextureSamplingComputeSpirv;
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
        result.ComputePipelineCreated = true;

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

    void UpdateDescriptors(ProbeResult& result)
    {
        VkDescriptorBufferInfo config{ConfigBuffer.Buffer, 0, sizeof(Config)};
        VkDescriptorBufferInfo output{ResultBuffer.Buffer, 0, ResultBuffer.Size};
        const std::array<VkDescriptorImageInfo, 3> images{{
            {ClampSampler, Texture.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {RepeatSampler, Texture.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {MirrorSampler, Texture.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};
        std::array<VkWriteDescriptorSet, 5> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DescriptorSet,
            0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &config, nullptr};
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            writes[i + 1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                DescriptorSet, i + 1, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i], nullptr, nullptr};
        }
        writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, DescriptorSet,
            4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &output, nullptr};
        Functions->vkUpdateDescriptorSets(
            Device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        result.DescriptorUpdateIntegrated = true;
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        UpdateDescriptors(result);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer", vr);

        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = Texture.Image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kTextureWidth, kTextureHeight, 1};
        Functions->vkCmdCopyBufferToImage(CommandBuffer, TextureUpload.Buffer,
            Texture.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toSample = toTransfer;
        toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &toSample);
        Functions->vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
        Functions->vkCmdBindDescriptorSets(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1,
            &DescriptorSet, 0, nullptr);
        result.CommandBufferBindIntegrated = true;
        Functions->vkCmdDispatch(CommandBuffer, 1, 1, 1);
        VkBufferMemoryBarrier output{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        output.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        output.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        output.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        output.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        output.buffer = ResultBuffer.Buffer;
        output.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
            0, nullptr, 1, &output, 0, nullptr);
        vr = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkEndCommandBuffer", vr);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        vr = Functions->vkQueueSubmit(Context->computeQueue(), 1, &submit, Fence);
        if (vr != VK_SUCCESS) return Fail("vkQueueSubmit", vr);
        vr = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences", vr);
        result.TextureUploadCompleted = true;
        result.DispatchSubmitted = true;
        return true;
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(
            Device, ResultBuffer.Memory, 0, ResultBuffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(readback)", vr);
        const auto* words = static_cast<const std::uint32_t*>(mapped);
        result.Samples = Expected;
        for (std::size_t i = 0; i < result.Samples.size(); ++i)
        {
            for (std::size_t c = 0; c < 4; ++c)
                result.Samples[i].Actual[c] = static_cast<std::uint8_t>(words[i * 4 + c]);
            result.Samples[i].Matched = Matches(
                result.Samples[i].Actual, result.Samples[i].Expected);
        }
        Functions->vkUnmapMemory(Device, ResultBuffer.Memory);
        result.ReadbackCompleted = true;
        result.ClampPassed = result.Samples[0].Matched;
        result.RepeatPassed = result.Samples[1].Matched;
        result.MirrorPassed = result.Samples[2].Matched;
        result.ModulatePassed = result.Samples[3].Matched;
        result.DecalPassed = result.Samples[4].Matched;
        result.ToonTexturedPassed = result.Samples[5].Matched;
        result.HighlightTexturedPassed = result.Samples[6].Matched;
        result.SamplesMatched = std::all_of(result.Samples.begin(), result.Samples.end(),
            [](const SampleResult& sample) { return sample.Matched; });
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
        if (DescriptorSetLayout) Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        if (ClampSampler) Functions->vkDestroySampler(Device, ClampSampler, nullptr);
        if (RepeatSampler) Functions->vkDestroySampler(Device, RepeatSampler, nullptr);
        if (MirrorSampler) Functions->vkDestroySampler(Device, MirrorSampler, nullptr);
        if (Texture.View) Functions->vkDestroyImageView(Device, Texture.View, nullptr);
        if (Texture.Image) Functions->vkDestroyImage(Device, Texture.Image, nullptr);
        if (Texture.Memory) Functions->vkFreeMemory(Device, Texture.Memory, nullptr);
        DestroyBuffer(ResultBuffer);
        DestroyBuffer(ConfigBuffer);
        DestroyBuffer(TextureUpload);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    BufferResource TextureUpload;
    BufferResource ConfigBuffer;
    BufferResource ResultBuffer;
    ImageResource Texture;
    VulkanToonHighlightConfig Config{};
    VkSampler ClampSampler = VK_NULL_HANDLE;
    VkSampler RepeatSampler = VK_NULL_HANDLE;
    VkSampler MirrorSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::vector<SampleResult> Expected;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject SampleJson(const SampleResult& sample)
{
    QJsonArray expected;
    QJsonArray actual;
    for (const auto value : sample.Expected) expected.append(static_cast<int>(value));
    for (const auto value : sample.Actual) actual.append(static_cast<int>(value));
    return {{"name", sample.Name}, {"expected", expected}, {"actual", actual},
        {"matched", sample.Matched}};
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples) samples.append(SampleJson(sample));
    return {
        {"passed", result.Passed},
        {"integer_texture_created", result.IntegerTextureCreated},
        {"texture_upload_completed", result.TextureUploadCompleted},
        {"clamp_sampler_created", result.ClampSamplerCreated},
        {"repeat_sampler_created", result.RepeatSamplerCreated},
        {"mirror_sampler_created", result.MirrorSamplerCreated},
        {"descriptor_layout_created", result.DescriptorLayoutCreated},
        {"descriptor_set_allocated", result.DescriptorSetAllocated},
        {"descriptor_update_integrated", result.DescriptorUpdateIntegrated},
        {"compute_pipeline_created", result.ComputePipelineCreated},
        {"command_buffer_bind_integrated", result.CommandBufferBindIntegrated},
        {"dispatch_submitted", result.DispatchSubmitted},
        {"readback_completed", result.ReadbackCompleted},
        {"samples_matched", result.SamplesMatched},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunTextureSamplingBootstrapHarness(const QString& outputPath, int iterations)
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
                TextureSamplingProbe probe(context, &window);
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
        passed ? "[MelonPrime] Vulkan texture sampling bootstrap passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan texture sampling bootstrap failed: iterations=%d\n",
        completed);
    const auto contract = melonDS::Vulkan::DescribeVulkanTextureSamplingDescriptorContract();
    const QJsonObject output{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(contract.ContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"texture_format", "VK_FORMAT_R8G8B8A8_UINT"},
        {"descriptor_set", static_cast<int>(contract.DescriptorSet)},
        {"uniform_binding", static_cast<int>(contract.UniformBinding)},
        {"clamp_binding", static_cast<int>(contract.ClampBinding)},
        {"repeat_binding", static_cast<int>(contract.RepeatBinding)},
        {"mirror_binding", static_cast<int>(contract.MirrorBinding)},
        {"output_binding", static_cast<int>(contract.OutputBinding)},
        {"integer_texture_created", lastResult.IntegerTextureCreated},
        {"texture_upload_completed", lastResult.TextureUploadCompleted},
        {"nearest_filter_validated", lastResult.SamplesMatched},
        {"clamp_passed", lastResult.ClampPassed},
        {"repeat_passed", lastResult.RepeatPassed},
        {"mirror_passed", lastResult.MirrorPassed},
        {"modulate_passed", lastResult.ModulatePassed},
        {"decal_passed", lastResult.DecalPassed},
        {"toon_textured_passed", lastResult.ToonTexturedPassed},
        {"highlight_textured_passed", lastResult.HighlightTexturedPassed},
        {"samples_matched", lastResult.SamplesMatched},
        {"gpu_texture_sampling_integrated", true},
        {"polygon_shader_integrated", false},
        {"texture_cache_integrated", false},
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
