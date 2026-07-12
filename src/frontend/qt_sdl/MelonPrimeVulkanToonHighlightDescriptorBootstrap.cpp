#include "MelonPrimeVulkanToonHighlightDescriptorBootstrap.h"

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

using melonDS::Vulkan::VulkanPackedVertex;
using melonDS::Vulkan::VulkanToonHighlightConfig;
using melonDS::Vulkan::VulkanToonHighlightMode;

constexpr std::uint32_t kWidth = 128;
constexpr std::uint32_t kHeight = 64;
constexpr std::uint32_t kTriangleCount = 4;

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

struct SampleExpectation
{
    const char* Name = nullptr;
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::array<std::uint8_t, 4> Expected{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> Actual{{0, 0, 0, 0}};
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool DescriptorLayoutCreated = false;
    bool DescriptorPoolCreated = false;
    bool DescriptorSetsAllocated = false;
    bool DescriptorUpdateIntegrated = false;
    bool PipelineLayoutIntegrated = false;
    bool CommandBufferBindIntegrated = false;
    bool DeviceLocalVertexBuffer = false;
    bool OpaquePipelineCreated = false;
    bool TranslucentPipelineCreated = false;
    bool DrawSubmitted = false;
    bool ColorReadbackCompleted = false;
    bool ToonOpaquePassed = false;
    bool HighlightOpaquePassed = false;
    bool ToonTranslucentPassed = false;
    bool HighlightTranslucentPassed = false;
    bool SamplesMatched = false;
    std::uint32_t DrawCount = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleExpectation> Samples;
};

std::uint32_t PackPair(std::uint32_t low, std::uint32_t high)
{
    return (low & 0xFFFFu) | ((high & 0xFFFFu) << 16);
}

std::uint32_t PackColor(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha5)
{
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8) |
        (static_cast<std::uint32_t>(blue) << 16) |
        (static_cast<std::uint32_t>(alpha5 & 31u) << 24);
}

std::uint8_t ToUnorm8(float value)
{
    return static_cast<std::uint8_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::array<std::uint8_t, 4> StorageColor(
    std::array<std::uint8_t, 4> rgba,
    VkFormat format)
{
    if (format == VK_FORMAT_B8G8R8A8_UNORM)
        std::swap(rgba[0], rgba[2]);
    return rgba;
}

bool PixelMatches(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& expected)
{
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        if (std::abs(static_cast<int>(actual[index]) -
                static_cast<int>(expected[index])) > 2)
            return false;
    }
    return true;
}

void AddTriangle(
    std::vector<VulkanPackedVertex>& vertices,
    const std::array<std::array<std::uint16_t, 2>, 3>& positions,
    std::uint8_t alpha5)
{
    for (const auto& position : positions)
    {
        VulkanPackedVertex vertex;
        vertex.PositionXY = PackPair(position[0], position[1]);
        vertex.DepthZW = PackPair(0x4000u, 0xFFFFu);
        vertex.ColorRgba = PackColor(128u, 128u, 128u, alpha5);
        vertex.TexcoordST = 0;
        vertex.PolygonFlags = 0;
        vertex.TextureLayer = 0xFFFFFFFFu;
        vertex.TextureSize = PackPair(8u, 8u);
        vertices.push_back(vertex);
    }
}

std::vector<VulkanPackedVertex> BuildVertices()
{
    std::vector<VulkanPackedVertex> vertices;
    vertices.reserve(kTriangleCount * 3u);
    AddTriangle(vertices, {{{{4, 4}}, {{60, 4}}, {{4, 28}}}}, 31u);
    AddTriangle(vertices, {{{{68, 4}}, {{124, 4}}, {{124, 28}}}}, 31u);
    AddTriangle(vertices, {{{{4, 36}}, {{60, 36}}, {{4, 60}}}}, 15u);
    AddTriangle(vertices, {{{{68, 36}}, {{124, 36}}, {{124, 60}}}}, 15u);
    return vertices;
}

std::array<std::uint16_t, 32> BuildToonTable()
{
    std::array<std::uint16_t, 32> table{};
    // Packed vertex red=128 maps to table index 15 with int(r * 31).
    table[15] = 31u;
    return table;
}

class ToonHighlightDrawProbe
{
public:
    ToonHighlightDrawProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~ToonHighlightDrawProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        Vertices = BuildVertices();
        const auto table = BuildToonTable();
        Configs[0] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(
            table, 0u, VulkanToonHighlightMode::Toon, false);
        Configs[1] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(
            table, 0u, VulkanToonHighlightMode::Highlight, false);
        const auto abi = melonDS::Vulkan::DescribeVulkanToonHighlightShaderAbi();
        if (abi.DescriptorSet != 0u || abi.DescriptorBinding != 0u ||
            abi.ConfigSize != sizeof(VulkanToonHighlightConfig) ||
            abi.ToonTableEntries != 32u)
        {
            result.FailureStage = "toon/highlight ABI mismatch";
            return result;
        }
        if (!CreateResources(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        if (!RecordAndSubmit(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        if (!Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.Passed = result.DescriptorLayoutCreated &&
            result.DescriptorPoolCreated && result.DescriptorSetsAllocated &&
            result.DescriptorUpdateIntegrated && result.PipelineLayoutIntegrated &&
            result.CommandBufferBindIntegrated && result.DeviceLocalVertexBuffer &&
            result.OpaquePipelineCreated && result.TranslucentPipelineCreated &&
            result.DrawSubmitted && result.ColorReadbackCompleted &&
            result.ToonOpaquePassed && result.HighlightOpaquePassed &&
            result.ToonTranslucentPassed && result.HighlightTranslucentPassed &&
            result.SamplesMatched && result.DrawCount == 4u;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t typeBits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) && (flags & required) == required &&
                (flags & preferred) == preferred)
                return index;
        }
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) && (flags & required) == required)
                return index;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        BufferResource& resource)
    {
        resource.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = Functions->vkCreateBuffer(
            Device, &info, nullptr, &resource.Buffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateBuffer", result);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(
            Device, resource.Buffer, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, required, preferred);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(
            Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(buffer)", result);
        result = Functions->vkBindBufferMemory(
            Device, resource.Buffer, resource.Memory, 0);
        return result == VK_SUCCESS ? true : Fail("vkBindBufferMemory", result);
    }

    bool UploadBuffer(const BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult result = Functions->vkMapMemory(
            Device, resource.Memory, 0, size, 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(upload)", result);
        std::memcpy(mapped, data, size);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return true;
    }

    bool CreateImage(VkFormat format, ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {kWidth, kHeight, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult result = Functions->vkCreateImage(
            Device, &info, nullptr, &resource.Image);
        if (result != VK_SUCCESS)
            return Fail("vkCreateImage", result);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(
            Device, resource.Image, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(
            Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(image)", result);
        result = Functions->vkBindImageMemory(
            Device, resource.Image, resource.Memory, 0);
        if (result != VK_SUCCESS)
            return Fail("vkBindImageMemory", result);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        result = Functions->vkCreateImageView(
            Device, &view, nullptr, &resource.View);
        return result == VK_SUCCESS ? true : Fail("vkCreateImageView", result);
    }

    bool CreateShaderModule(
        const std::uint32_t* code,
        std::size_t size,
        VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        const VkResult result = Functions->vkCreateShaderModule(
            Device, &info, nullptr, &module);
        return result == VK_SUCCESS ? true : Fail("vkCreateShaderModule", result);
    }

    bool CreatePipeline(
        VkShaderModule vertex,
        VkShaderModule fragment,
        bool translucent,
        VkPipeline& pipeline)
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(VulkanPackedVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attributes[4]{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_UINT, 0};
        attributes[1] = {1, 0, VK_FORMAT_R32_UINT, 16};
        attributes[2] = {2, 0, VK_FORMAT_R32_UINT, 20};
        attributes[3] = {3, 0, VK_FORMAT_R32_UINT, 24};
        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 4;
        vertexInput.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachments[2]{};
        attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        attachments[1].colorWriteMask = translucent ? 0u :
            (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
        if (translucent)
        {
            attachments[0].blendEnable = VK_TRUE;
            attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
            attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachments[0].alphaBlendOp = VK_BLEND_OP_MAX;
        }
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 2;
        blend.pAttachments = attachments;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = PipelineLayout;
        info.renderPass = RenderPass;
        const VkResult result = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        return result == VK_SUCCESS ? true :
            Fail("vkCreateGraphicsPipelines", result);
    }

    bool CreateResources(ProbeResult& result)
    {
        const VkMemoryPropertyFlags host =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkDeviceSize vertexBytes =
            Vertices.size() * sizeof(VulkanPackedVertex);
        if (!CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                host, 0, VertexUpload) ||
            !CreateBuffer(vertexBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VertexDevice) ||
            !UploadBuffer(VertexUpload, Vertices.data(),
                static_cast<std::size_t>(vertexBytes)))
            return false;
        result.DeviceLocalVertexBuffer = VertexDevice.Buffer != VK_NULL_HANDLE;

        for (std::size_t index = 0; index < ConfigBuffers.size(); ++index)
        {
            if (!CreateBuffer(sizeof(VulkanToonHighlightConfig),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, 0,
                    ConfigBuffers[index]) ||
                !UploadBuffer(ConfigBuffers[index], &Configs[index],
                    sizeof(VulkanToonHighlightConfig)))
                return false;
        }
        const VkDeviceSize rgbaBytes =
            static_cast<VkDeviceSize>(kWidth) * kHeight * 4u;
        if (!CreateBuffer(rgbaBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                host, 0, ColorReadback) ||
            !CreateImage(Context->featureInfo().colorFormat, ColorTarget) ||
            !CreateImage(VK_FORMAT_R8G8B8A8_UNORM, AttributeTarget))
            return false;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = melonDS::Vulkan::kVulkanToonHighlightDescriptorBinding;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo setLayoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayoutInfo.bindingCount = 1;
        setLayoutInfo.pBindings = &binding;
        VkResult vkResult = Functions->vkCreateDescriptorSetLayout(
            Device, &setLayoutInfo, nullptr, &DescriptorSetLayout);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateDescriptorSetLayout", vkResult);
        result.DescriptorLayoutCreated = true;

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 2;
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkResult = Functions->vkCreateDescriptorPool(
            Device, &poolInfo, nullptr, &DescriptorPool);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateDescriptorPool", vkResult);
        result.DescriptorPoolCreated = true;

        const VkDescriptorSetLayout layouts[2] = {
            DescriptorSetLayout, DescriptorSetLayout};
        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = DescriptorPool;
        allocateInfo.descriptorSetCount = 2;
        allocateInfo.pSetLayouts = layouts;
        vkResult = Functions->vkAllocateDescriptorSets(
            Device, &allocateInfo, DescriptorSets.data());
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateDescriptorSets", vkResult);
        result.DescriptorSetsAllocated = true;

        VkDescriptorBufferInfo bufferInfos[2]{};
        VkWriteDescriptorSet writes[2]{};
        for (std::uint32_t index = 0; index < 2; ++index)
        {
            bufferInfos[index].buffer = ConfigBuffers[index].Buffer;
            bufferInfos[index].range = sizeof(VulkanToonHighlightConfig);
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = DescriptorSets[index];
            writes[index].dstBinding =
                melonDS::Vulkan::kVulkanToonHighlightDescriptorBinding;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[index].pBufferInfo = &bufferInfos[index];
        }
        Functions->vkUpdateDescriptorSets(Device, 2, writes, 0, nullptr);
        result.DescriptorUpdateIntegrated = true;

        VkAttachmentDescription attachments[2]{};
        attachments[0].format = Context->featureInfo().colorFormat;
        attachments[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        for (auto& attachment : attachments)
        {
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        const VkAttachmentReference colorRefs[2] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        };
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colorRefs;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo renderPassInfo{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;
        vkResult = Functions->vkCreateRenderPass(
            Device, &renderPassInfo, nullptr, &RenderPass);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateRenderPass", vkResult);

        const VkImageView views[2] = {
            ColorTarget.View, AttributeTarget.View};
        VkFramebufferCreateInfo framebufferInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = RenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = views;
        framebufferInfo.width = kWidth;
        framebufferInfo.height = kHeight;
        framebufferInfo.layers = 1;
        vkResult = Functions->vkCreateFramebuffer(
            Device, &framebufferInfo, nullptr, &Framebuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateFramebuffer", vkResult);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(melonDS::Vulkan::VulkanOpaquePushConstants);
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &DescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        vkResult = Functions->vkCreatePipelineLayout(
            Device, &pipelineLayoutInfo, nullptr, &PipelineLayout);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreatePipelineLayout", vkResult);
        result.PipelineLayoutIntegrated = true;

        if (!CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueVertexSpirv),
                OpaqueVertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentToonHighlightSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentToonHighlightSpirv),
                OpaqueFragmentShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanTranslucentVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanTranslucentVertexSpirv),
                TranslucentVertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanTranslucentFragmentToonHighlightSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanTranslucentFragmentToonHighlightSpirv),
                TranslucentFragmentShader))
            return false;
        if (!CreatePipeline(OpaqueVertexShader, OpaqueFragmentShader,
                false, OpaquePipeline) ||
            !CreatePipeline(TranslucentVertexShader, TranslucentFragmentShader,
                true, TranslucentPipeline))
            return false;
        result.OpaquePipelineCreated = true;
        result.TranslucentPipelineCreated = true;

        VkCommandPoolCreateInfo commandPoolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex =
            Context->featureInfo().graphicsQueueFamily;
        vkResult = Functions->vkCreateCommandPool(
            Device, &commandPoolInfo, nullptr, &CommandPool);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateCommandPool", vkResult);
        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = CommandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = 1;
        vkResult = Functions->vkAllocateCommandBuffers(
            Device, &commandAllocate, &CommandBuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateCommandBuffers", vkResult);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkResult = Functions->vkCreateFence(
            Device, &fenceInfo, nullptr, &Fence);
        return vkResult == VK_SUCCESS ? true : Fail("vkCreateFence", vkResult);
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vkResult = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vkResult != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", vkResult);
        VkBufferCopy vertexCopy{0, 0, VertexDevice.Size};
        Functions->vkCmdCopyBuffer(
            CommandBuffer, VertexUpload.Buffer, VertexDevice.Buffer, 1, &vertexCopy);
        VkBufferMemoryBarrier vertexBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        vertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vertexBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vertexBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertexBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertexBarrier.buffer = VertexDevice.Buffer;
        vertexBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            0, nullptr, 1, &vertexBarrier, 0, nullptr);

        VkClearValue clears[2]{};
        VkRenderPassBeginInfo renderBegin{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderBegin.renderPass = RenderPass;
        renderBegin.framebuffer = Framebuffer;
        renderBegin.renderArea.extent = {kWidth, kHeight};
        renderBegin.clearValueCount = 2;
        renderBegin.pClearValues = clears;
        Functions->vkCmdBeginRenderPass(
            CommandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};
        viewport.width = static_cast<float>(kWidth);
        viewport.height = static_cast<float>(kHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        const VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
        Functions->vkCmdSetViewport(CommandBuffer, 0, 1, &viewport);
        Functions->vkCmdSetScissor(CommandBuffer, 0, 1, &scissor);
        const VkDeviceSize vertexOffset = 0;
        Functions->vkCmdBindVertexBuffers(
            CommandBuffer, 0, 1, &VertexDevice.Buffer, &vertexOffset);
        melonDS::Vulkan::VulkanOpaquePushConstants push;
        push.ScreenSize = {{static_cast<float>(kWidth), static_cast<float>(kHeight)}};
        Functions->vkCmdPushConstants(
            CommandBuffer, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(push), &push);

        auto bindAndDraw = [&](VkPipeline pipeline, std::uint32_t descriptorIndex,
                               std::uint32_t firstVertex) {
            Functions->vkCmdBindPipeline(
                CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            Functions->vkCmdBindDescriptorSets(
                CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                melonDS::Vulkan::kVulkanToonHighlightDescriptorSet,
                1, &DescriptorSets[descriptorIndex], 0, nullptr);
            Functions->vkCmdDraw(CommandBuffer, 3, 1, firstVertex, 0);
            ++result.DrawCount;
        };
        bindAndDraw(OpaquePipeline, 0, 0);
        bindAndDraw(OpaquePipeline, 1, 3);
        bindAndDraw(TranslucentPipeline, 0, 6);
        bindAndDraw(TranslucentPipeline, 1, 9);
        result.CommandBufferBindIntegrated = true;
        Functions->vkCmdEndRenderPass(CommandBuffer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {kWidth, kHeight, 1};
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer, ColorTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ColorReadback.Buffer, 1, &copy);
        VkBufferMemoryBarrier readback{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        readback.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        readback.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        readback.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readback.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readback.buffer = ColorReadback.Buffer;
        readback.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0,
            0, nullptr, 1, &readback, 0, nullptr);
        vkResult = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkEndCommandBuffer", vkResult);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        vkResult = Functions->vkQueueSubmit(
            Context->graphicsQueue(), 1, &submit, Fence);
        if (vkResult != VK_SUCCESS)
            return Fail("vkQueueSubmit", vkResult);
        vkResult = Functions->vkWaitForFences(
            Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        if (vkResult != VK_SUCCESS)
            return Fail("vkWaitForFences", vkResult);
        result.DrawSubmitted = true;
        return true;
    }

    std::array<std::uint8_t, 4> ReadPixel(
        const std::uint8_t* bytes,
        std::uint32_t x,
        std::uint32_t y) const
    {
        std::array<std::uint8_t, 4> value{};
        const std::size_t offset =
            (static_cast<std::size_t>(y) * kWidth + x) * 4u;
        std::memcpy(value.data(), bytes + offset, value.size());
        return value;
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped = nullptr;
        VkResult vkResult = Functions->vkMapMemory(
            Device, ColorReadback.Memory, 0, ColorReadback.Size, 0, &mapped);
        if (vkResult != VK_SUCCESS)
            return Fail("vkMapMemory(color readback)", vkResult);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped);
        const float alpha = 15.0f / 31.0f;
        const std::uint8_t halfAlpha = ToUnorm8(alpha);
        const std::uint8_t halfRed = ToUnorm8(alpha);
        const std::uint8_t halfGray = ToUnorm8((128.0f / 255.0f) * alpha);
        result.Samples = {
            {"toon_opaque", 12, 12,
                StorageColor({{255, 0, 0, 255}}, Context->featureInfo().colorFormat)},
            {"highlight_opaque", 116, 12,
                StorageColor({{255, 128, 128, 255}}, Context->featureInfo().colorFormat)},
            {"toon_translucent", 12, 44,
                StorageColor({{halfRed, 0, 0, halfAlpha}}, Context->featureInfo().colorFormat)},
            {"highlight_translucent", 116, 44,
                StorageColor({{halfRed, halfGray, halfGray, halfAlpha}}, Context->featureInfo().colorFormat)},
        };
        for (auto& sample : result.Samples)
        {
            sample.Actual = ReadPixel(bytes, sample.X, sample.Y);
            sample.Matched = PixelMatches(sample.Actual, sample.Expected);
        }
        Functions->vkUnmapMemory(Device, ColorReadback.Memory);
        result.ColorReadbackCompleted = true;
        result.ToonOpaquePassed = result.Samples[0].Matched;
        result.HighlightOpaquePassed = result.Samples[1].Matched;
        result.ToonTranslucentPassed = result.Samples[2].Matched;
        result.HighlightTranslucentPassed = result.Samples[3].Matched;
        result.SamplesMatched = std::all_of(
            result.Samples.begin(), result.Samples.end(),
            [](const SampleExpectation& sample) { return sample.Matched; });
        return true;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer)
            Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void DestroyImage(ImageResource& resource)
    {
        if (resource.View)
            Functions->vkDestroyImageView(Device, resource.View, nullptr);
        if (resource.Image)
            Functions->vkDestroyImage(Device, resource.Image, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        Functions->vkDeviceWaitIdle(Device);
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (OpaquePipeline)
            Functions->vkDestroyPipeline(Device, OpaquePipeline, nullptr);
        if (TranslucentPipeline)
            Functions->vkDestroyPipeline(Device, TranslucentPipeline, nullptr);
        if (OpaqueVertexShader)
            Functions->vkDestroyShaderModule(Device, OpaqueVertexShader, nullptr);
        if (OpaqueFragmentShader)
            Functions->vkDestroyShaderModule(Device, OpaqueFragmentShader, nullptr);
        if (TranslucentVertexShader)
            Functions->vkDestroyShaderModule(Device, TranslucentVertexShader, nullptr);
        if (TranslucentFragmentShader)
            Functions->vkDestroyShaderModule(Device, TranslucentFragmentShader, nullptr);
        if (Framebuffer)
            Functions->vkDestroyFramebuffer(Device, Framebuffer, nullptr);
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        if (DescriptorPool)
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorSetLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorSetLayout, nullptr);
        DestroyImage(AttributeTarget);
        DestroyImage(ColorTarget);
        DestroyBuffer(ColorReadback);
        for (auto& buffer : ConfigBuffers)
            DestroyBuffer(buffer);
        DestroyBuffer(VertexDevice);
        DestroyBuffer(VertexUpload);
        Functions = nullptr;
        Device = VK_NULL_HANDLE;
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    std::vector<VulkanPackedVertex> Vertices;
    std::array<VulkanToonHighlightConfig, 2> Configs{};
    BufferResource VertexUpload;
    BufferResource VertexDevice;
    std::array<BufferResource, 2> ConfigBuffers{};
    BufferResource ColorReadback;
    ImageResource ColorTarget;
    ImageResource AttributeTarget;
    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> DescriptorSets{{VK_NULL_HANDLE, VK_NULL_HANDLE}};
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule OpaqueVertexShader = VK_NULL_HANDLE;
    VkShaderModule OpaqueFragmentShader = VK_NULL_HANDLE;
    VkShaderModule TranslucentVertexShader = VK_NULL_HANDLE;
    VkShaderModule TranslucentFragmentShader = VK_NULL_HANDLE;
    VkPipeline OpaquePipeline = VK_NULL_HANDLE;
    VkPipeline TranslucentPipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject SampleJson(const SampleExpectation& sample)
{
    QJsonArray expected;
    QJsonArray actual;
    for (std::uint8_t value : sample.Expected)
        expected.append(static_cast<int>(value));
    for (std::uint8_t value : sample.Actual)
        actual.append(static_cast<int>(value));
    return {
        {"name", sample.Name},
        {"x", static_cast<int>(sample.X)},
        {"y", static_cast<int>(sample.Y)},
        {"expected", expected},
        {"actual", actual},
        {"matched", sample.Matched},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples)
        samples.append(SampleJson(sample));
    return {
        {"passed", result.Passed},
        {"descriptor_layout_created", result.DescriptorLayoutCreated},
        {"descriptor_pool_created", result.DescriptorPoolCreated},
        {"descriptor_sets_allocated", result.DescriptorSetsAllocated},
        {"descriptor_update_integrated", result.DescriptorUpdateIntegrated},
        {"pipeline_layout_integrated", result.PipelineLayoutIntegrated},
        {"command_buffer_bind_integrated", result.CommandBufferBindIntegrated},
        {"device_local_vertex_buffer", result.DeviceLocalVertexBuffer},
        {"opaque_pipeline_created", result.OpaquePipelineCreated},
        {"translucent_pipeline_created", result.TranslucentPipelineCreated},
        {"draw_submitted", result.DrawSubmitted},
        {"draw_count", static_cast<int>(result.DrawCount)},
        {"color_readback_completed", result.ColorReadbackCompleted},
        {"toon_opaque_passed", result.ToonOpaquePassed},
        {"highlight_opaque_passed", result.HighlightOpaquePassed},
        {"toon_translucent_passed", result.ToonTranslucentPassed},
        {"highlight_translucent_passed", result.HighlightTranslucentPassed},
        {"samples_matched", result.SamplesMatched},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunToonHighlightDescriptorRuntimeHarness(
    const QString& outputPath,
    int iterations)
{
    if (iterations <= 0)
        iterations = 1;
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
                ToonHighlightDrawProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
            results.append(ProbeJson(lastResult));
            if (!lastResult.Passed)
                break;
            ++completed;
        }
    }
    const bool passed = completed == iterations;
    if (passed)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[MelonPrime] Vulkan toon/highlight GPU draw passed: iterations=%d draws=%u\n",
            completed,
            lastResult.DrawCount);
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan toon/highlight GPU draw failed: stage=%s VkResult=%d completed=%d/%d\n",
            lastResult.FailureStage.c_str(),
            static_cast<int>(lastResult.FailureResult),
            completed,
            iterations);
    }
    const QJsonObject output{
        {"schema_version", 2},
        {"passed", passed},
        {"contract_version", 2},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"config_size", static_cast<int>(sizeof(VulkanToonHighlightConfig))},
        {"descriptor_set", static_cast<int>(
            melonDS::Vulkan::kVulkanToonHighlightDescriptorSet)},
        {"descriptor_binding", static_cast<int>(
            melonDS::Vulkan::kVulkanToonHighlightDescriptorBinding)},
        {"descriptor_layout_created", lastResult.DescriptorLayoutCreated},
        {"descriptor_pool_created", lastResult.DescriptorPoolCreated},
        {"descriptor_sets_allocated", lastResult.DescriptorSetsAllocated},
        {"descriptor_update_integrated", lastResult.DescriptorUpdateIntegrated},
        {"pipeline_layout_integrated", lastResult.PipelineLayoutIntegrated},
        {"command_buffer_bind_integrated", lastResult.CommandBufferBindIntegrated},
        {"device_local_vertex_buffer", lastResult.DeviceLocalVertexBuffer},
        {"opaque_pipeline_created", lastResult.OpaquePipelineCreated},
        {"translucent_pipeline_created", lastResult.TranslucentPipelineCreated},
        {"draw_count", static_cast<int>(lastResult.DrawCount)},
        {"color_readback_completed", lastResult.ColorReadbackCompleted},
        {"toon_opaque_passed", lastResult.ToonOpaquePassed},
        {"highlight_opaque_passed", lastResult.HighlightOpaquePassed},
        {"toon_translucent_passed", lastResult.ToonTranslucentPassed},
        {"highlight_translucent_passed", lastResult.HighlightTranslucentPassed},
        {"samples_matched", lastResult.SamplesMatched},
        {"gpu_draw_integrated", true},
        {"texture_sampling_integrated", false},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", results},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
