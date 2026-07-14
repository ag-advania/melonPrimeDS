#include "MelonPrimeVulkanOpaqueBootstrap.h"

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

using melonDS::Vulkan::VulkanOpaquePipelineState;
using melonDS::Vulkan::VulkanPackedPolygon;
using melonDS::Vulkan::VulkanPackedVertex;
using melonDS::Vulkan::VulkanRasterBatch;
using melonDS::Vulkan::VulkanRasterBatchOptions;
using melonDS::Vulkan::VulkanRasterBatchPlan;
using melonDS::Vulkan::VulkanRasterPolygonFlag_WBuffer;
using melonDS::Vulkan::VulkanRasterPrimitive;
using melonDS::Vulkan::VulkanRasterUpload;

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 192;

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

struct ExpectedSample
{
    const char* Name = nullptr;
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::array<std::uint8_t, 4> Color{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> Attribute{{0, 0, 0, 0}};
    std::uint8_t Stencil = 0;
};

struct SampleResult
{
    ExpectedSample Expected;
    std::array<std::uint8_t, 4> ActualColor{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> ActualAttribute{{0, 0, 0, 0}};
    std::uint8_t ActualStencil = 0;
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool ContractPassed = false;
    bool StencilKeyBoundaryPassed = false;
    bool DeviceLocalVertexBuffer = false;
    bool DeviceLocalIndexBuffer = false;
    bool ZLessPipelineCreated = false;
    bool ZLessEqualPipelineCreated = false;
    bool WLessPipelineCreated = false;
    bool DrawSubmitted = false;
    bool ColorReadbackCompleted = false;
    bool AttributeReadbackCompleted = false;
    bool StencilReadbackCompleted = false;
    bool SamplesMatched = false;
    std::uint32_t BatchCount = 0;
    std::uint32_t DrawCount = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<SampleResult> Samples;
};

std::uint32_t PackPair(std::uint32_t low, std::uint32_t high)
{
    return (low & 0xFFFFu) | ((high & 0xFFFFu) << 16);
}

std::uint8_t ToUnorm8(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::array<std::uint8_t, 4> ColorStorage(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha,
    VkFormat format)
{
    const std::array<std::uint8_t, 4> rgba{{red, green, blue, alpha}};
    if (format == VK_FORMAT_B8G8R8A8_UNORM)
        return {{blue, green, red, alpha}};
    return rgba;
}

std::array<std::uint8_t, 4> AttributeStorage(std::uint32_t polyId, bool fog)
{
    return {{
        ToUnorm8(static_cast<float>(polyId & 0x3Fu) / 63.0f),
        0,
        static_cast<std::uint8_t>(fog ? 255u : 0u),
        255u,
    }};
}

bool PixelMatches(
    const std::array<std::uint8_t, 4>& actual,
    const std::array<std::uint8_t, 4>& expected)
{
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        if (std::abs(static_cast<int>(actual[index]) - static_cast<int>(expected[index])) > 1)
            return false;
    }
    return true;
}

std::uint32_t PackDepth(std::uint32_t depth24, std::uint32_t& zShift)
{
    std::uint32_t z = depth24;
    zShift = 0;
    while (z > 0xFFFFu)
    {
        z >>= 1;
        ++zShift;
    }
    return z;
}

void AddTriangle(
    VulkanRasterUpload& upload,
    std::uint32_t sourceOrder,
    const std::array<std::array<std::uint16_t, 2>, 3>& positions,
    std::uint32_t depth24,
    std::uint32_t attr,
    std::uint32_t flags,
    const std::array<std::uint8_t, 3>& rgb)
{
    VulkanPackedPolygon polygon;
    polygon.SourceOrder = sourceOrder;
    polygon.Primitive = static_cast<std::uint32_t>(VulkanRasterPrimitive::Triangles);
    polygon.VertexOffset = static_cast<std::uint32_t>(upload.Vertices.size());
    polygon.VertexCount = 3;
    polygon.IndexOffset = static_cast<std::uint32_t>(upload.Indices.size());
    polygon.IndexCount = 3;
    polygon.EdgeIndexOffset = static_cast<std::uint32_t>(upload.EdgeIndices.size());
    polygon.EdgeIndexCount = 6;
    polygon.Attr = attr;
    polygon.TextureLayer = 0xFFFFFFFFu;
    polygon.Flags = flags;

    std::uint32_t zShift = 0;
    const std::uint32_t packedZ = PackDepth(depth24, zShift);
    const std::uint32_t alpha = (attr >> 16) & 0x1Fu;
    for (const auto& position : positions)
    {
        VulkanPackedVertex vertex;
        vertex.PositionXY = PackPair(position[0], position[1]);
        vertex.DepthZW = PackPair(packedZ, 0xFFFFu);
        vertex.ColorRgba = static_cast<std::uint32_t>(rgb[0]) |
            (static_cast<std::uint32_t>(rgb[1]) << 8) |
            (static_cast<std::uint32_t>(rgb[2]) << 16) |
            (alpha << 24);
        vertex.PolygonFlags = (attr & 0x1F00C8F0u) | (zShift << 16);
        vertex.TextureLayer = 0xFFFFFFFFu;
        vertex.TextureSize = PackPair(8, 8);
        upload.Vertices.push_back(vertex);
    }
    upload.Indices.push_back(static_cast<std::uint16_t>(polygon.VertexOffset));
    upload.Indices.push_back(static_cast<std::uint16_t>(polygon.VertexOffset + 1u));
    upload.Indices.push_back(static_cast<std::uint16_t>(polygon.VertexOffset + 2u));
    upload.EdgeIndices.insert(upload.EdgeIndices.end(), {
        static_cast<std::uint16_t>(polygon.VertexOffset),
        static_cast<std::uint16_t>(polygon.VertexOffset + 1u),
        static_cast<std::uint16_t>(polygon.VertexOffset + 1u),
        static_cast<std::uint16_t>(polygon.VertexOffset + 2u),
        static_cast<std::uint16_t>(polygon.VertexOffset + 2u),
        static_cast<std::uint16_t>(polygon.VertexOffset),
    });
    upload.Polygons.push_back(polygon);
}

VulkanRasterUpload BuildScene()
{
    VulkanRasterUpload upload;
    upload.SourcePolygonCount = 5;
    const std::array<std::array<std::uint16_t, 2>, 3> large{{{{20, 20}}, {{230, 20}}, {{20, 180}}}};
    const std::array<std::array<std::uint16_t, 2>, 3> blue{{{{70, 50}}, {{160, 50}}, {{70, 140}}}};
    const std::array<std::array<std::uint16_t, 2>, 3> reject{{{{170, 50}}, {{245, 50}}, {{245, 140}}}};
    const std::array<std::array<std::uint16_t, 2>, 3> wbuffer{{{{80, 145}}, {{150, 145}}, {{115, 185}}}};

    const std::uint32_t redAttr = (31u << 16) | (5u << 24) | (1u << 15);
    const std::uint32_t blueAttr = (31u << 16) | (9u << 24) | (1u << 14);
    const std::uint32_t rejectAttr = (15u << 16) | (13u << 24);
    const std::uint32_t wAttr = (31u << 16) | (17u << 24);
    AddTriangle(upload, 0, large, 0x400000u, redAttr, 0, {{255, 0, 0}});
    AddTriangle(upload, 1, large, 0xC00000u, redAttr, 0, {{0, 255, 0}});
    AddTriangle(upload, 2, blue, 0x400000u, blueAttr, 0, {{0, 0, 255}});
    AddTriangle(upload, 3, reject, 0x200000u, rejectAttr, 0, {{255, 255, 0}});
    AddTriangle(upload, 4, wbuffer, 0x500000u, wAttr, VulkanRasterPolygonFlag_WBuffer, {{0, 255, 255}});
    upload.Valid = true;
    return upload;
}

class OpaqueProbe
{
public:
    OpaqueProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~OpaqueProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        Upload = BuildScene();
        VulkanRasterBatchOptions options;
        options.ColorFormat = Context->featureInfo().colorFormat;
        options.AttributeFormat = VK_FORMAT_R8G8B8A8_UNORM;
        options.DepthStencilFormat = Context->featureInfo().depthStencilFormat;
        std::string failure;
        if (!melonDS::Vulkan::BuildVulkanRasterBatchPlan(Upload, options, Plan, &failure))
        {
            result.FailureStage = failure;
            return result;
        }
        Contract = melonDS::Vulkan::BuildRasterTargetContract(
            1, options.ColorFormat, options.DepthStencilFormat);
        result.BatchCount = static_cast<std::uint32_t>(Plan.Batches.size());
        result.StencilKeyBoundaryPassed = Plan.Batches.size() == 4 &&
            Plan.Batches[0].PolygonCount == 2 &&
            Plan.Batches[0].PipelineKey.StencilReference == 5 &&
            Plan.Batches[1].PipelineKey.StencilReference == 9 &&
            Plan.Batches[2].PipelineKey.StencilReference == 13 &&
            Plan.Batches[3].PipelineKey.StencilReference == 17;
        result.ContractPassed = Contract.Valid && result.StencilKeyBoundaryPassed;
        if (!result.ContractPassed)
        {
            result.FailureStage = "opaque pipeline contract validation";
            return result;
        }
        if (!CreateResources())
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.DeviceLocalVertexBuffer = VertexDevice.Buffer != VK_NULL_HANDLE;
        result.DeviceLocalIndexBuffer = IndexDevice.Buffer != VK_NULL_HANDLE;
        result.ZLessPipelineCreated = ZLessPipeline != VK_NULL_HANDLE;
        result.ZLessEqualPipelineCreated = ZLessEqualPipeline != VK_NULL_HANDLE;
        result.WLessPipelineCreated = WLessPipeline != VK_NULL_HANDLE;
        if (!RecordAndSubmit(result.DrawCount))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.DrawSubmitted = true;
        if (!Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.Passed = result.ContractPassed && result.DeviceLocalVertexBuffer &&
            result.DeviceLocalIndexBuffer && result.ZLessPipelineCreated &&
            result.ZLessEqualPipelineCreated && result.WLessPipelineCreated &&
            result.DrawSubmitted && result.ColorReadbackCompleted &&
            result.AttributeReadbackCompleted && result.StencilReadbackCompleted &&
            result.SamplesMatched && result.DrawCount == 4;
        if (!result.Passed)
            result.FailureStage = "opaque color/attribute/stencil comparison";
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
        VkResult result = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateBuffer", result);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, required, preferred);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(buffer)", result);
        result = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        return result == VK_SUCCESS ? true : Fail("vkBindBufferMemory", result);
    }

    bool UploadBuffer(const BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult result = Functions->vkMapMemory(Device, resource.Memory, 0, size, 0, &mapped);
        if (result != VK_SUCCESS)
            return Fail("vkMapMemory(upload)", result);
        std::memcpy(mapped, data, size);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return true;
    }

    bool CreateImage(
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspects,
        ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {kWidth, kHeight, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult result = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (result != VK_SUCCESS)
            return Fail("vkCreateImage", result);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        result = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateMemory(image)", result);
        result = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (result != VK_SUCCESS)
            return Fail("vkBindImageMemory", result);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = aspects;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        result = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        return result == VK_SUCCESS ? true : Fail("vkCreateImageView", result);
    }

    bool CreateShaderModule(const std::uint32_t* code, std::size_t size, VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        const VkResult result = Functions->vkCreateShaderModule(Device, &info, nullptr, &module);
        return result == VK_SUCCESS ? true : Fail("vkCreateShaderModule", result);
    }

    bool CreatePipeline(
        VkShaderModule vertex,
        VkShaderModule fragment,
        VkCompareOp compare,
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
        VkStencilOpState stencil{};
        stencil.failOp = VK_STENCIL_OP_KEEP;
        stencil.passOp = VK_STENCIL_OP_REPLACE;
        stencil.depthFailOp = VK_STENCIL_OP_KEEP;
        stencil.compareOp = VK_COMPARE_OP_ALWAYS;
        stencil.compareMask = 0xFFu;
        stencil.writeMask = 0xFFu;
        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = compare;
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front = stencil;
        depthStencil.back = stencil;
        VkPipelineColorBlendAttachmentState blendAttachments[2]{};
        for (auto& attachment : blendAttachments)
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 2;
        blend.pAttachments = blendAttachments;
        const VkDynamicState states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 3;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = PipelineLayout;
        info.renderPass = RenderPass;
        VkResult result = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        return result == VK_SUCCESS ? true : Fail("vkCreateGraphicsPipelines", result);
    }

    bool CreateResources()
    {
        const VkDeviceSize vertexBytes = Upload.Vertices.size() * sizeof(VulkanPackedVertex);
        const VkDeviceSize indexBytes = Upload.Indices.size() * sizeof(std::uint16_t);
        const VkMemoryPropertyFlags host =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, 0, VertexUpload) ||
            !CreateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, 0, IndexUpload) ||
            !CreateBuffer(vertexBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VertexDevice) ||
            !CreateBuffer(indexBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, IndexDevice))
            return false;
        if (!UploadBuffer(VertexUpload, Upload.Vertices.data(), vertexBytes) ||
            !UploadBuffer(IndexUpload, Upload.Indices.data(), indexBytes))
            return false;

        const VkDeviceSize rgbaBytes = static_cast<VkDeviceSize>(kWidth) * kHeight * 4u;
        const VkDeviceSize stencilBytes = static_cast<VkDeviceSize>(kWidth) * kHeight;
        if (!CreateBuffer(rgbaBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, 0, ColorReadback) ||
            !CreateBuffer(rgbaBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, 0, AttributeReadback) ||
            !CreateBuffer(stencilBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, 0, StencilReadback))
            return false;
        if (!CreateImage(Contract.ColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, ColorTarget) ||
            !CreateImage(Contract.AttributeFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, AttributeTarget) ||
            !CreateImage(Contract.DepthStencilFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                melonDS::Vulkan::DepthStencilAspectMask(Contract.DepthStencilFormat), DepthTarget))
            return false;

        VkAttachmentDescription attachments[3]{};
        attachments[0].format = Contract.ColorFormat;
        attachments[1].format = Contract.AttributeFormat;
        for (int index = 0; index < 2; ++index)
        {
            attachments[index].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[index].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        attachments[2].format = Contract.DepthStencilFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference colors[2] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        };
        VkAttachmentReference depth{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colors;
        subpass.pDepthStencilAttachment = &depth;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
            VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 3;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;
        VkResult result = Functions->vkCreateRenderPass(Device, &renderPassInfo, nullptr, &RenderPass);
        if (result != VK_SUCCESS)
            return Fail("vkCreateRenderPass", result);
        const VkImageView views[] = {ColorTarget.View, AttributeTarget.View, DepthTarget.View};
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = RenderPass;
        framebufferInfo.attachmentCount = 3;
        framebufferInfo.pAttachments = views;
        framebufferInfo.width = kWidth;
        framebufferInfo.height = kHeight;
        framebufferInfo.layers = 1;
        result = Functions->vkCreateFramebuffer(Device, &framebufferInfo, nullptr, &Framebuffer);
        if (result != VK_SUCCESS)
            return Fail("vkCreateFramebuffer", result);

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(melonDS::Vulkan::VulkanOpaquePushConstants);
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        result = Functions->vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
        if (result != VK_SUCCESS)
            return Fail("vkCreatePipelineLayout", result);
        if (!CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueVertexSpirv), ZVertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentSpirv), ZFragmentShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueVertexWBufferSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueVertexWBufferSpirv), WVertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentWBufferSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanOpaqueFragmentWBufferSpirv), WFragmentShader))
            return false;
        if (!CreatePipeline(ZVertexShader, ZFragmentShader, VK_COMPARE_OP_LESS, ZLessPipeline) ||
            !CreatePipeline(ZVertexShader, ZFragmentShader, VK_COMPARE_OP_LESS_OR_EQUAL, ZLessEqualPipeline) ||
            !CreatePipeline(WVertexShader, WFragmentShader, VK_COMPARE_OP_LESS, WLessPipeline))
            return false;

        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        result = Functions->vkCreateCommandPool(Device, &poolInfo, nullptr, &CommandPool);
        if (result != VK_SUCCESS)
            return Fail("vkCreateCommandPool", result);
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = CommandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        result = Functions->vkAllocateCommandBuffers(Device, &allocate, &CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkAllocateCommandBuffers", result);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        result = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &Fence);
        return result == VK_SUCCESS ? true : Fail("vkCreateFence", result);
    }

    bool RecordAndSubmit(std::uint32_t& drawCount)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult result = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (result != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", result);
        VkBufferCopy vertexCopy{0, 0, VertexDevice.Size};
        VkBufferCopy indexCopy{0, 0, IndexDevice.Size};
        Functions->vkCmdCopyBuffer(CommandBuffer, VertexUpload.Buffer, VertexDevice.Buffer, 1, &vertexCopy);
        Functions->vkCmdCopyBuffer(CommandBuffer, IndexUpload.Buffer, IndexDevice.Buffer, 1, &indexCopy);
        VkBufferMemoryBarrier bufferBarriers[2]{};
        bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufferBarriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarriers[0].buffer = VertexDevice.Buffer;
        bufferBarriers[0].size = VK_WHOLE_SIZE;
        bufferBarriers[1] = bufferBarriers[0];
        bufferBarriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        bufferBarriers[1].buffer = IndexDevice.Buffer;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            0, nullptr, 2, bufferBarriers, 0, nullptr);

        const auto clear = melonDS::Vulkan::DecodeClearPlaneState(63u << 24, 0x7FFFu);
        const auto clearValues = melonDS::Vulkan::BuildClearPlaneAttachmentValues(clear);
        VkRenderPassBeginInfo renderBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderBegin.renderPass = RenderPass;
        renderBegin.framebuffer = Framebuffer;
        renderBegin.renderArea.extent = {kWidth, kHeight};
        renderBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        renderBegin.pClearValues = clearValues.data();
        Functions->vkCmdBeginRenderPass(CommandBuffer, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{};
        viewport.width = static_cast<float>(kWidth);
        viewport.height = static_cast<float>(kHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
        Functions->vkCmdSetViewport(CommandBuffer, 0, 1, &viewport);
        Functions->vkCmdSetScissor(CommandBuffer, 0, 1, &scissor);
        const VkDeviceSize vertexOffset = 0;
        Functions->vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &VertexDevice.Buffer, &vertexOffset);
        Functions->vkCmdBindIndexBuffer(CommandBuffer, IndexDevice.Buffer, 0, VK_INDEX_TYPE_UINT16);
        melonDS::Vulkan::VulkanOpaquePushConstants push;
        push.ScreenSize = {{static_cast<float>(kWidth), static_cast<float>(kHeight)}};
        Functions->vkCmdPushConstants(CommandBuffer, PipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

        drawCount = 0;
        for (const VulkanRasterBatch& batch : Plan.Batches)
        {
            const VulkanOpaquePipelineState state =
                melonDS::Vulkan::BuildVulkanOpaquePipelineState(batch.PipelineKey);
            if (!state.Valid)
                return Fail("unsupported opaque bootstrap batch", VK_ERROR_FEATURE_NOT_PRESENT);
            VkPipeline pipeline = state.WBuffer
                ? WLessPipeline
                : (state.DepthCompare == VK_COMPARE_OP_LESS_OR_EQUAL
                    ? ZLessEqualPipeline : ZLessPipeline);
            Functions->vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            Functions->vkCmdSetStencilReference(
                CommandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, state.StencilReference);
            Functions->vkCmdDrawIndexed(
                CommandBuffer, batch.IndexCount, 1, batch.IndexOffset, 0, 0);
            ++drawCount;
        }
        Functions->vkCmdEndRenderPass(CommandBuffer);

        VkBufferImageCopy rgbaCopy{};
        rgbaCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rgbaCopy.imageSubresource.layerCount = 1;
        rgbaCopy.imageExtent = {kWidth, kHeight, 1};
        Functions->vkCmdCopyImageToBuffer(CommandBuffer, ColorTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ColorReadback.Buffer, 1, &rgbaCopy);
        Functions->vkCmdCopyImageToBuffer(CommandBuffer, AttributeTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, AttributeReadback.Buffer, 1, &rgbaCopy);
        VkBufferImageCopy stencilCopy{};
        stencilCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        stencilCopy.imageSubresource.layerCount = 1;
        stencilCopy.imageExtent = {kWidth, kHeight, 1};
        Functions->vkCmdCopyImageToBuffer(CommandBuffer, DepthTarget.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StencilReadback.Buffer, 1, &stencilCopy);
        VkBufferMemoryBarrier readback[3]{};
        BufferResource* resources[3] = {&ColorReadback, &AttributeReadback, &StencilReadback};
        for (int index = 0; index < 3; ++index)
        {
            readback[index].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            readback[index].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            readback[index].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            readback[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            readback[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            readback[index].buffer = resources[index]->Buffer;
            readback[index].size = VK_WHOLE_SIZE;
        }
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
            0, nullptr, 3, readback, 0, nullptr);
        result = Functions->vkEndCommandBuffer(CommandBuffer);
        if (result != VK_SUCCESS)
            return Fail("vkEndCommandBuffer", result);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        result = Functions->vkQueueSubmit(Context->graphicsQueue(), 1, &submit, Fence);
        if (result != VK_SUCCESS)
            return Fail("vkQueueSubmit", result);
        result = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        return result == VK_SUCCESS ? true : Fail("vkWaitForFences", result);
    }

    std::array<std::uint8_t, 4> ReadRgba(const std::uint8_t* bytes, std::uint32_t x, std::uint32_t y)
    {
        std::array<std::uint8_t, 4> value{};
        const std::size_t offset = (static_cast<std::size_t>(y) * kWidth + x) * 4u;
        std::memcpy(value.data(), bytes + offset, 4u);
        return value;
    }

    bool Readback(ProbeResult& result)
    {
        void* colorMapped = nullptr;
        void* attrMapped = nullptr;
        void* stencilMapped = nullptr;
        VkResult vkResult = Functions->vkMapMemory(
            Device, ColorReadback.Memory, 0, ColorReadback.Size, 0, &colorMapped);
        if (vkResult != VK_SUCCESS)
            return Fail("vkMapMemory(color readback)", vkResult);
        vkResult = Functions->vkMapMemory(
            Device, AttributeReadback.Memory, 0, AttributeReadback.Size, 0, &attrMapped);
        if (vkResult != VK_SUCCESS)
        {
            Functions->vkUnmapMemory(Device, ColorReadback.Memory);
            return Fail("vkMapMemory(attribute readback)", vkResult);
        }
        vkResult = Functions->vkMapMemory(
            Device, StencilReadback.Memory, 0, StencilReadback.Size, 0, &stencilMapped);
        if (vkResult != VK_SUCCESS)
        {
            Functions->vkUnmapMemory(Device, AttributeReadback.Memory);
            Functions->vkUnmapMemory(Device, ColorReadback.Memory);
            return Fail("vkMapMemory(stencil readback)", vkResult);
        }

        const auto clearColor = ColorStorage(0, 0, 0, 0, Contract.ColorFormat);
        const auto clearAttr = AttributeStorage(63, false);
        const std::array<ExpectedSample, 5> expected{{
            {"z_less_depth_reject", 40, 120,
                ColorStorage(255, 0, 0, 255, Contract.ColorFormat), AttributeStorage(5, true), 5},
            {"z_lequal_overwrite", 100, 80,
                ColorStorage(0, 0, 255, 255, Contract.ColorFormat), AttributeStorage(9, false), 9},
            {"opaque_alpha_reject", 220, 80, clearColor, clearAttr, 0xFFu},
            {"w_buffer_write", 115, 160,
                ColorStorage(0, 255, 255, 255, Contract.ColorFormat), AttributeStorage(17, false), 17},
            {"clear_background", 240, 170, clearColor, clearAttr, 0xFFu},
        }};
        const auto* colorBytes = static_cast<const std::uint8_t*>(colorMapped);
        const auto* attrBytes = static_cast<const std::uint8_t*>(attrMapped);
        const auto* stencilBytes = static_cast<const std::uint8_t*>(stencilMapped);
        result.SamplesMatched = true;
        for (const ExpectedSample& item : expected)
        {
            SampleResult sample;
            sample.Expected = item;
            sample.ActualColor = ReadRgba(colorBytes, item.X, item.Y);
            sample.ActualAttribute = ReadRgba(attrBytes, item.X, item.Y);
            sample.ActualStencil = stencilBytes[static_cast<std::size_t>(item.Y) * kWidth + item.X];
            sample.Matched = PixelMatches(sample.ActualColor, item.Color) &&
                PixelMatches(sample.ActualAttribute, item.Attribute) &&
                sample.ActualStencil == item.Stencil;
            result.SamplesMatched = result.SamplesMatched && sample.Matched;
            result.Samples.push_back(sample);
        }
        Functions->vkUnmapMemory(Device, StencilReadback.Memory);
        Functions->vkUnmapMemory(Device, AttributeReadback.Memory);
        Functions->vkUnmapMemory(Device, ColorReadback.Memory);
        result.ColorReadbackCompleted = true;
        result.AttributeReadbackCompleted = true;
        result.StencilReadbackCompleted = true;
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
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (ZLessPipeline)
            Functions->vkDestroyPipeline(Device, ZLessPipeline, nullptr);
        if (ZLessEqualPipeline)
            Functions->vkDestroyPipeline(Device, ZLessEqualPipeline, nullptr);
        if (WLessPipeline)
            Functions->vkDestroyPipeline(Device, WLessPipeline, nullptr);
        if (ZVertexShader)
            Functions->vkDestroyShaderModule(Device, ZVertexShader, nullptr);
        if (ZFragmentShader)
            Functions->vkDestroyShaderModule(Device, ZFragmentShader, nullptr);
        if (WVertexShader)
            Functions->vkDestroyShaderModule(Device, WVertexShader, nullptr);
        if (WFragmentShader)
            Functions->vkDestroyShaderModule(Device, WFragmentShader, nullptr);
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (Framebuffer)
            Functions->vkDestroyFramebuffer(Device, Framebuffer, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        DestroyImage(DepthTarget);
        DestroyImage(AttributeTarget);
        DestroyImage(ColorTarget);
        DestroyBuffer(StencilReadback);
        DestroyBuffer(AttributeReadback);
        DestroyBuffer(ColorReadback);
        DestroyBuffer(IndexDevice);
        DestroyBuffer(VertexDevice);
        DestroyBuffer(IndexUpload);
        DestroyBuffer(VertexUpload);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    VulkanRasterUpload Upload;
    VulkanRasterBatchPlan Plan;
    melonDS::Vulkan::RasterTargetContract Contract;
    BufferResource VertexUpload;
    BufferResource IndexUpload;
    BufferResource VertexDevice;
    BufferResource IndexDevice;
    BufferResource ColorReadback;
    BufferResource AttributeReadback;
    BufferResource StencilReadback;
    ImageResource ColorTarget;
    ImageResource AttributeTarget;
    ImageResource DepthTarget;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule ZVertexShader = VK_NULL_HANDLE;
    VkShaderModule ZFragmentShader = VK_NULL_HANDLE;
    VkShaderModule WVertexShader = VK_NULL_HANDLE;
    VkShaderModule WFragmentShader = VK_NULL_HANDLE;
    VkPipeline ZLessPipeline = VK_NULL_HANDLE;
    VkPipeline ZLessEqualPipeline = VK_NULL_HANDLE;
    VkPipeline WLessPipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonArray ByteArrayJson(const std::array<std::uint8_t, 4>& value)
{
    return QJsonArray{
        static_cast<int>(value[0]), static_cast<int>(value[1]),
        static_cast<int>(value[2]), static_cast<int>(value[3])};
}

QJsonObject SampleJson(const SampleResult& sample)
{
    return QJsonObject{
        {"name", QString::fromLatin1(sample.Expected.Name)},
        {"x", static_cast<int>(sample.Expected.X)},
        {"y", static_cast<int>(sample.Expected.Y)},
        {"expected_color", ByteArrayJson(sample.Expected.Color)},
        {"actual_color", ByteArrayJson(sample.ActualColor)},
        {"expected_attribute", ByteArrayJson(sample.Expected.Attribute)},
        {"actual_attribute", ByteArrayJson(sample.ActualAttribute)},
        {"expected_stencil", static_cast<int>(sample.Expected.Stencil)},
        {"actual_stencil", static_cast<int>(sample.ActualStencil)},
        {"matched", sample.Matched},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples)
        samples.append(SampleJson(sample));
    return QJsonObject{
        {"passed", result.Passed},
        {"contract_passed", result.ContractPassed},
        {"stencil_key_boundary_passed", result.StencilKeyBoundaryPassed},
        {"device_local_vertex_buffer", result.DeviceLocalVertexBuffer},
        {"device_local_index_buffer", result.DeviceLocalIndexBuffer},
        {"z_less_pipeline_created", result.ZLessPipelineCreated},
        {"z_less_equal_pipeline_created", result.ZLessEqualPipelineCreated},
        {"w_less_pipeline_created", result.WLessPipelineCreated},
        {"draw_submitted", result.DrawSubmitted},
        {"draw_count", static_cast<int>(result.DrawCount)},
        {"batch_count", static_cast<int>(result.BatchCount)},
        {"color_readback_completed", result.ColorReadbackCompleted},
        {"attribute_readback_completed", result.AttributeReadbackCompleted},
        {"stencil_readback_completed", result.StencilReadbackCompleted},
        {"samples_matched", result.SamplesMatched},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunOpaquePipelineBootstrapHarness(const QString& outputPath, int iterations)
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
                OpaqueProbe probe(context, &window);
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
            "[MelonPrime] Vulkan opaque pipeline bootstrap passed: iterations=%d draws=%u batches=%u\n",
            completed,
            lastResult.DrawCount,
            lastResult.BatchCount);
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan opaque pipeline bootstrap failed: stage=%s VkResult=%d completed=%d/%d\n",
            lastResult.FailureStage.c_str(),
            static_cast<int>(lastResult.FailureResult),
            completed,
            iterations);
    }

    const QJsonObject output{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(melonDS::Vulkan::kOpaquePipelineContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"batch_count", static_cast<int>(lastResult.BatchCount)},
        {"draw_count", static_cast<int>(lastResult.DrawCount)},
        {"stencil_key_boundary_passed", lastResult.StencilKeyBoundaryPassed},
        {"z_less_pipeline_created", lastResult.ZLessPipelineCreated},
        {"z_less_equal_pipeline_created", lastResult.ZLessEqualPipelineCreated},
        {"w_less_pipeline_created", lastResult.WLessPipelineCreated},
        {"color_readback_completed", lastResult.ColorReadbackCompleted},
        {"attribute_readback_completed", lastResult.AttributeReadbackCompleted},
        {"stencil_readback_completed", lastResult.StencilReadbackCompleted},
        {"samples_matched", lastResult.SamplesMatched},
        {"software_game_rendering_preserved", true},
        {"native_opaque_bootstrap_available", true},
        {"native_ds_polygon_raster_integrated", false},
        {"textured_opaque_supported", false},
        {"translucent_supported", false},
        {"shadow_supported", false},
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
