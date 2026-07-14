#include "MelonPrimeVulkanTexturedPolygonBootstrap.h"

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
using melonDS::Vulkan::VulkanTextureCombinerInput;
using melonDS::Vulkan::VulkanTextureCombinerMode;
using melonDS::Vulkan::VulkanToonHighlightConfig;
using melonDS::Vulkan::VulkanToonHighlightMode;

constexpr std::uint32_t kWidth = 192;
constexpr std::uint32_t kHeight = 96;
constexpr std::uint32_t kTextureWidth = 4;
constexpr std::uint32_t kTextureHeight = 4;
constexpr std::uint32_t kTriangleCount = 6;
constexpr std::uint32_t kDescriptorCount = 4;

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
    bool IntegerTextureCreated = false;
    bool TextureUploadCompleted = false;
    bool ClampSamplerCreated = false;
    bool RepeatSamplerCreated = false;
    bool MirrorSamplerCreated = false;
    bool DescriptorLayoutCreated = false;
    bool DescriptorPoolCreated = false;
    bool DescriptorSetsAllocated = false;
    bool DescriptorUpdateIntegrated = false;
    bool PipelineLayoutIntegrated = false;
    bool CommandBufferBindIntegrated = false;
    bool DeviceLocalVertexBuffer = false;
    bool OpaqueModulatePipelineCreated = false;
    bool OpaqueDecalPipelineCreated = false;
    bool TranslucentModulatePipelineCreated = false;
    bool TranslucentDecalPipelineCreated = false;
    bool DrawSubmitted = false;
    bool ColorReadbackCompleted = false;
    bool ClampModulatePassed = false;
    bool RepeatDecalPassed = false;
    bool MirrorToonPassed = false;
    bool ClampHighlightPassed = false;
    bool TranslucentModulatePassed = false;
    bool TranslucentDecalPassed = false;
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

std::uint32_t PackColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a5)
{
    return static_cast<std::uint32_t>(r) |
        (static_cast<std::uint32_t>(g) << 8) |
        (static_cast<std::uint32_t>(b) << 16) |
        (static_cast<std::uint32_t>(a5 & 31u) << 24);
}

std::array<std::uint8_t, 4> StorageColor(std::array<std::uint8_t, 4> rgba, VkFormat format)
{
    if (format == VK_FORMAT_B8G8R8A8_UNORM)
        std::swap(rgba[0], rgba[2]);
    return rgba;
}

bool PixelMatches(const std::array<std::uint8_t, 4>& actual,
                  const std::array<std::uint8_t, 4>& expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (std::abs(static_cast<int>(actual[i]) - static_cast<int>(expected[i])) > 3)
            return false;
    return true;
}

std::array<std::uint8_t, kTextureWidth * kTextureHeight * 4> BuildTexture()
{
    std::array<std::uint8_t, kTextureWidth * kTextureHeight * 4> bytes{};
    const std::array<std::array<std::uint8_t, 4>, 4> columns{{
        {{63, 0, 0, 31}},
        {{0, 63, 0, 16}},
        {{0, 0, 63, 31}},
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

std::array<std::uint16_t, 32> BuildToonTable()
{
    std::array<std::uint16_t, 32> table{};
    table[15] = static_cast<std::uint16_t>(31u << 10); // blue
    return table;
}

void AddTriangle(std::vector<VulkanPackedVertex>& vertices,
                 const std::array<std::array<std::uint16_t, 2>, 3>& positions,
                 std::array<std::uint8_t, 3> color, std::uint8_t alpha5,
                 std::int16_t s16, std::int16_t t16)
{
    for (const auto& position : positions)
    {
        VulkanPackedVertex vertex;
        vertex.PositionXY = PackPair(position[0], position[1]);
        vertex.DepthZW = PackPair(0x4000u, 0xFFFFu);
        vertex.ColorRgba = PackColor(color[0], color[1], color[2], alpha5);
        vertex.TexcoordST = PackPair(static_cast<std::uint16_t>(s16), static_cast<std::uint16_t>(t16));
        vertex.PolygonFlags = 0;
        vertex.TextureLayer = 0;
        vertex.TextureSize = PackPair(kTextureWidth, kTextureHeight);
        vertices.push_back(vertex);
    }
}

std::vector<VulkanPackedVertex> BuildVertices()
{
    std::vector<VulkanPackedVertex> vertices;
    vertices.reserve(kTriangleCount * 3u);
    const std::int16_t outside = 88; // 1.375 * 16 * 4
    const std::int16_t first = 8;    // 0.125 * 16 * 4
    const std::int16_t second = 24;  // 0.375 * 16 * 4
    AddTriangle(vertices, {{{{4, 4}}, {{60, 4}}, {{4, 42}}}}, {{255,255,255}}, 31, outside, first);
    AddTriangle(vertices, {{{{68, 4}}, {{124, 4}}, {{124, 42}}}}, {{255,0,0}}, 31, outside, first);
    AddTriangle(vertices, {{{{132, 4}}, {{188, 4}}, {{132, 42}}}}, {{128,128,128}}, 31, outside, first);
    AddTriangle(vertices, {{{{4, 52}}, {{60, 52}}, {{4, 92}}}}, {{128,128,128}}, 31, first, first);
    AddTriangle(vertices, {{{{68, 52}}, {{124, 52}}, {{124, 92}}}}, {{255,255,255}}, 15, second, first);
    AddTriangle(vertices, {{{{132, 52}}, {{188, 52}}, {{132, 92}}}}, {{255,0,0}}, 15, outside, first);
    return vertices;
}

class TexturedPolygonProbe
{
public:
    TexturedPolygonProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context) { Device = Context->device(); Functions = Context->functions(); }
    }
    ~TexturedPolygonProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        Vertices = BuildVertices();
        const auto table = BuildToonTable();
        Configs[0] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(table, 0u, VulkanToonHighlightMode::None, true);
        Configs[1] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(table, 0u, VulkanToonHighlightMode::None, true);
        Configs[2] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(table, 0u, VulkanToonHighlightMode::Toon, true);
        Configs[3] = melonDS::Vulkan::BuildVulkanToonHighlightConfig(table, 0u, VulkanToonHighlightMode::Highlight, true);
        const auto contract = melonDS::Vulkan::DescribeVulkanTexturedPolygonDescriptorContract();
        if (contract.DescriptorSet != 0u || contract.UniformBinding != 0u ||
            contract.TextureBinding != 1u || contract.TextureFormat != VK_FORMAT_R8G8B8A8_UINT)
        {
            result.FailureStage = "textured polygon descriptor contract mismatch";
            return result;
        }
        if (!CreateResources(result) || !RecordAndSubmit(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.Passed = result.IntegerTextureCreated && result.TextureUploadCompleted &&
            result.ClampSamplerCreated && result.RepeatSamplerCreated && result.MirrorSamplerCreated &&
            result.DescriptorLayoutCreated && result.DescriptorPoolCreated &&
            result.DescriptorSetsAllocated && result.DescriptorUpdateIntegrated &&
            result.PipelineLayoutIntegrated && result.CommandBufferBindIntegrated &&
            result.DeviceLocalVertexBuffer && result.OpaqueModulatePipelineCreated &&
            result.OpaqueDecalPipelineCreated && result.TranslucentModulatePipelineCreated &&
            result.TranslucentDecalPipelineCreated && result.DrawSubmitted &&
            result.ColorReadbackCompleted && result.SamplesMatched && result.DrawCount == 6u;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage; FailureResult = result; return false;
    }

    std::uint32_t FindMemoryType(std::uint32_t bits, VkMemoryPropertyFlags required,
                                 VkMemoryPropertyFlags preferred) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(Context->physicalDevice(), &properties);
        for (std::uint32_t i=0;i<properties.memoryTypeCount;++i)
        {
            const auto flags=properties.memoryTypes[i].propertyFlags;
            if ((bits&(1u<<i)) && (flags&required)==required && (flags&preferred)==preferred) return i;
        }
        for (std::uint32_t i=0;i<properties.memoryTypeCount;++i)
        {
            const auto flags=properties.memoryTypes[i].propertyFlags;
            if ((bits&(1u<<i)) && (flags&required)==required) return i;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                      BufferResource& resource)
    {
        resource.Size=size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size=size; info.usage=usage; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr=Functions->vkCreateBuffer(Device,&info,nullptr,&resource.Buffer);
        if (vr!=VK_SUCCESS) return Fail("vkCreateBuffer",vr);
        VkMemoryRequirements req{}; Functions->vkGetBufferMemoryRequirements(Device,resource.Buffer,&req);
        auto type=FindMemoryType(req.memoryTypeBits,required,preferred);
        if (type==std::numeric_limits<std::uint32_t>::max()) return Fail("buffer memory type",VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size; alloc.memoryTypeIndex=type;
        vr=Functions->vkAllocateMemory(Device,&alloc,nullptr,&resource.Memory);
        if (vr!=VK_SUCCESS) return Fail("vkAllocateMemory(buffer)",vr);
        vr=Functions->vkBindBufferMemory(Device,resource.Buffer,resource.Memory,0);
        return vr==VK_SUCCESS?true:Fail("vkBindBufferMemory",vr);
    }

    bool Upload(const BufferResource& resource,const void* data,std::size_t size)
    {
        void* mapped=nullptr; VkResult vr=Functions->vkMapMemory(Device,resource.Memory,0,size,0,&mapped);
        if (vr!=VK_SUCCESS) return Fail("vkMapMemory(upload)",vr);
        std::memcpy(mapped,data,size); Functions->vkUnmapMemory(Device,resource.Memory); return true;
    }

    bool CreateColorImage(VkFormat format, ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D; info.format=format; info.extent={kWidth,kHeight,1};
        info.mipLevels=1; info.arrayLayers=1; info.samples=VK_SAMPLE_COUNT_1_BIT;
        info.tiling=VK_IMAGE_TILING_OPTIMAL; info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode=VK_SHARING_MODE_EXCLUSIVE; info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr=Functions->vkCreateImage(Device,&info,nullptr,&resource.Image);
        if (vr!=VK_SUCCESS) return Fail("vkCreateImage(color)",vr);
        VkMemoryRequirements req{}; Functions->vkGetImageMemoryRequirements(Device,resource.Image,&req);
        auto type=FindMemoryType(req.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type==std::numeric_limits<std::uint32_t>::max()) return Fail("color image memory type",VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size; alloc.memoryTypeIndex=type;
        vr=Functions->vkAllocateMemory(Device,&alloc,nullptr,&resource.Memory);
        if (vr!=VK_SUCCESS) return Fail("vkAllocateMemory(color)",vr);
        vr=Functions->vkBindImageMemory(Device,resource.Image,resource.Memory,0);
        if (vr!=VK_SUCCESS) return Fail("vkBindImageMemory(color)",vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=resource.Image; view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=format; view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount=1; view.subresourceRange.layerCount=1;
        vr=Functions->vkCreateImageView(Device,&view,nullptr,&resource.View);
        return vr==VK_SUCCESS?true:Fail("vkCreateImageView(color)",vr);
    }

    bool CreateTexture(ProbeResult& result)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D; info.format=VK_FORMAT_R8G8B8A8_UINT; info.extent={kTextureWidth,kTextureHeight,1};
        info.mipLevels=1; info.arrayLayers=1; info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr=Functions->vkCreateImage(Device,&info,nullptr,&Texture.Image);
        if (vr!=VK_SUCCESS) return Fail("vkCreateImage(texture)",vr);
        VkMemoryRequirements req{}; Functions->vkGetImageMemoryRequirements(Device,Texture.Image,&req);
        auto type=FindMemoryType(req.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type==std::numeric_limits<std::uint32_t>::max()) return Fail("texture memory type",VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size; alloc.memoryTypeIndex=type;
        vr=Functions->vkAllocateMemory(Device,&alloc,nullptr,&Texture.Memory);
        if (vr!=VK_SUCCESS) return Fail("vkAllocateMemory(texture)",vr);
        vr=Functions->vkBindImageMemory(Device,Texture.Image,Texture.Memory,0);
        if (vr!=VK_SUCCESS) return Fail("vkBindImageMemory(texture)",vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=Texture.Image; view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.format=info.format; view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount=1; view.subresourceRange.layerCount=1;
        vr=Functions->vkCreateImageView(Device,&view,nullptr,&Texture.View);
        if (vr!=VK_SUCCESS) return Fail("vkCreateImageView(texture)",vr);
        result.IntegerTextureCreated=true; return true;
    }

    bool CreateSampler(VkSamplerAddressMode mode,VkSampler& sampler)
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; info.magFilter=VK_FILTER_NEAREST; info.minFilter=VK_FILTER_NEAREST;
        info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST; info.addressModeU=mode; info.addressModeV=mode; info.addressModeW=mode;
        info.minLod=0.0f; info.maxLod=0.0f; info.maxAnisotropy=1.0f;
        VkResult vr=Functions->vkCreateSampler(Device,&info,nullptr,&sampler);
        return vr==VK_SUCCESS?true:Fail("vkCreateSampler",vr);
    }

    bool CreateShaderModule(const std::uint32_t* code,std::size_t size,VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize=size; info.pCode=code;
        VkResult vr=Functions->vkCreateShaderModule(Device,&info,nullptr,&module);
        return vr==VK_SUCCESS?true:Fail("vkCreateShaderModule",vr);
    }

    bool CreatePipeline(VkShaderModule vertex,VkShaderModule fragment,bool translucent,VkPipeline& pipeline)
    {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex; stages[0].pName="main";
        stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragment; stages[1].pName="main";
        VkVertexInputBindingDescription binding{0,sizeof(VulkanPackedVertex),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[4]{{0,0,VK_FORMAT_R32G32B32A32_UINT,0},{1,0,VK_FORMAT_R32_UINT,16},{2,0,VK_FORMAT_R32_UINT,20},{3,0,VK_FORMAT_R32_UINT,24}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding; vi.vertexAttributeDescriptionCount=4; vi.pVertexAttributeDescriptions=attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        if (translucent) { attachment.blendEnable=VK_TRUE; attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; attachment.colorBlendOp=VK_BLEND_OP_ADD; attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE; attachment.alphaBlendOp=VK_BLEND_OP_MAX; }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount=1; blend.pAttachments=&attachment;
        VkDynamicState states[2]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dyn.dynamicStateCount=2; dyn.pDynamicStates=states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.stageCount=2; info.pStages=stages; info.pVertexInputState=&vi; info.pInputAssemblyState=&ia; info.pViewportState=&vp; info.pRasterizationState=&rs; info.pMultisampleState=&ms; info.pColorBlendState=&blend; info.pDynamicState=&dyn; info.layout=PipelineLayout; info.renderPass=RenderPass;
        VkResult vr=Functions->vkCreateGraphicsPipelines(Device,VK_NULL_HANDLE,1,&info,nullptr,&pipeline);
        return vr==VK_SUCCESS?true:Fail("vkCreateGraphicsPipelines",vr);
    }

    bool CreateResources(ProbeResult& result)
    {
        const VkMemoryPropertyFlags host=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const VkDeviceSize vertexBytes=Vertices.size()*sizeof(VulkanPackedVertex);
        const auto textureBytes=BuildTexture();
        if (!CreateBuffer(vertexBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,host,0,VertexUpload) ||
            !CreateBuffer(vertexBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,VertexDevice) ||
            !Upload(VertexUpload,Vertices.data(),static_cast<std::size_t>(vertexBytes)) ||
            !CreateBuffer(textureBytes.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,host,0,TextureUpload) ||
            !Upload(TextureUpload,textureBytes.data(),textureBytes.size())) return false;
        result.DeviceLocalVertexBuffer=true;
        for (std::size_t i=0;i<ConfigBuffers.size();++i)
            if (!CreateBuffer(sizeof(VulkanToonHighlightConfig),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,host,0,ConfigBuffers[i]) || !Upload(ConfigBuffers[i],&Configs[i],sizeof(Configs[i]))) return false;
        const VkDeviceSize rgbaBytes=static_cast<VkDeviceSize>(kWidth)*kHeight*4u;
        if (!CreateBuffer(rgbaBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,host,0,ColorReadback) || !CreateColorImage(Context->featureInfo().colorFormat,ColorTarget) || !CreateTexture(result)) return false;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, ClampSampler))
            return false;
        result.ClampSamplerCreated = true;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, RepeatSampler))
            return false;
        result.RepeatSamplerCreated = true;
        if (!CreateSampler(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, MirrorSampler))
            return false;
        result.MirrorSamplerCreated = true;

        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0]={0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        bindings[1]={1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=2; layout.pBindings=bindings;
        VkResult vr=Functions->vkCreateDescriptorSetLayout(Device,&layout,nullptr,&DescriptorSetLayout); if (vr!=VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout",vr); result.DescriptorLayoutCreated=true;
        VkDescriptorPoolSize poolSizes[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,kDescriptorCount},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,kDescriptorCount}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=kDescriptorCount; pool.poolSizeCount=2; pool.pPoolSizes=poolSizes;
        vr=Functions->vkCreateDescriptorPool(Device,&pool,nullptr,&DescriptorPool); if (vr!=VK_SUCCESS) return Fail("vkCreateDescriptorPool",vr); result.DescriptorPoolCreated=true;
        std::array<VkDescriptorSetLayout,kDescriptorCount> layouts{}; layouts.fill(DescriptorSetLayout);
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; alloc.descriptorPool=DescriptorPool; alloc.descriptorSetCount=kDescriptorCount; alloc.pSetLayouts=layouts.data();
        vr=Functions->vkAllocateDescriptorSets(Device,&alloc,DescriptorSets.data()); if (vr!=VK_SUCCESS) return Fail("vkAllocateDescriptorSets",vr); result.DescriptorSetsAllocated=true;
        const std::array<VkSampler,kDescriptorCount> samplers{{ClampSampler,RepeatSampler,MirrorSampler,ClampSampler}};
        std::array<VkDescriptorBufferInfo,kDescriptorCount> buffers{}; std::array<VkDescriptorImageInfo,kDescriptorCount> images{}; std::array<VkWriteDescriptorSet,kDescriptorCount*2> writes{};
        for (std::uint32_t i=0;i<kDescriptorCount;++i)
        {
            buffers[i]={ConfigBuffers[i].Buffer,0,sizeof(VulkanToonHighlightConfig)};
            images[i]={samplers[i],Texture.View,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[i*2]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,DescriptorSets[i],0,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,nullptr,&buffers[i],nullptr};
            writes[i*2+1]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,DescriptorSets[i],1,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&images[i],nullptr,nullptr};
        }
        Functions->vkUpdateDescriptorSets(Device,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr); result.DescriptorUpdateIntegrated=true;

        VkAttachmentDescription attachment{}; attachment.format=Context->featureInfo().colorFormat; attachment.samples=VK_SAMPLE_COUNT_1_BIT; attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE; attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&ref;
        VkSubpassDependency deps[2]{}; deps[0].srcSubpass=VK_SUBPASS_EXTERNAL; deps[0].dstSubpass=0; deps[0].srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].srcSubpass=0; deps[1].dstSubpass=VK_SUBPASS_EXTERNAL; deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; deps[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT; deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; deps[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rp.attachmentCount=1; rp.pAttachments=&attachment; rp.subpassCount=1; rp.pSubpasses=&subpass; rp.dependencyCount=2; rp.pDependencies=deps;
        vr=Functions->vkCreateRenderPass(Device,&rp,nullptr,&RenderPass); if (vr!=VK_SUCCESS) return Fail("vkCreateRenderPass",vr);
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass=RenderPass; fb.attachmentCount=1; fb.pAttachments=&ColorTarget.View; fb.width=kWidth; fb.height=kHeight; fb.layers=1;
        vr=Functions->vkCreateFramebuffer(Device,&fb,nullptr,&Framebuffer); if (vr!=VK_SUCCESS) return Fail("vkCreateFramebuffer",vr);
        VkPushConstantRange push{}; push.stageFlags=VK_SHADER_STAGE_VERTEX_BIT; push.size=sizeof(melonDS::Vulkan::VulkanOpaquePushConstants);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount=1; pl.pSetLayouts=&DescriptorSetLayout; pl.pushConstantRangeCount=1; pl.pPushConstantRanges=&push;
        vr=Functions->vkCreatePipelineLayout(Device,&pl,nullptr,&PipelineLayout); if (vr!=VK_SUCCESS) return Fail("vkCreatePipelineLayout",vr); result.PipelineLayoutIntegrated=true;

        if (!CreateShaderModule(melonDS::Vulkan::Shaders::kVulkanTexturedPolygonVertexSpirv,melonDS::Vulkan::Shaders::kVulkanTexturedPolygonVertexSpirvSize,VertexShader) ||
            !CreateShaderModule(melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentOpaqueModulateSpirv,melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentOpaqueModulateSpirvSize,OpaqueModulateShader) ||
            !CreateShaderModule(melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentOpaqueDecalSpirv,melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentOpaqueDecalSpirvSize,OpaqueDecalShader) ||
            !CreateShaderModule(melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentTranslucentModulateSpirv,melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentTranslucentModulateSpirvSize,TranslucentModulateShader) ||
            !CreateShaderModule(melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentTranslucentDecalSpirv,melonDS::Vulkan::Shaders::kVulkanTexturedPolygonFragmentTranslucentDecalSpirvSize,TranslucentDecalShader)) return false;
        if (!CreatePipeline(VertexShader,OpaqueModulateShader,false,OpaqueModulatePipeline) || !CreatePipeline(VertexShader,OpaqueDecalShader,false,OpaqueDecalPipeline) || !CreatePipeline(VertexShader,TranslucentModulateShader,true,TranslucentModulatePipeline) || !CreatePipeline(VertexShader,TranslucentDecalShader,true,TranslucentDecalPipeline)) return false;
        result.OpaqueModulatePipelineCreated=result.OpaqueDecalPipelineCreated=result.TranslucentModulatePipelineCreated=result.TranslucentDecalPipelineCreated=true;
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex=Context->featureInfo().graphicsQueueFamily;
        vr=Functions->vkCreateCommandPool(Device,&cp,nullptr,&CommandPool); if (vr!=VK_SUCCESS) return Fail("vkCreateCommandPool",vr);
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool=CommandPool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
        vr=Functions->vkAllocateCommandBuffers(Device,&ca,&CommandBuffer); if (vr!=VK_SUCCESS) return Fail("vkAllocateCommandBuffers",vr);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; vr=Functions->vkCreateFence(Device,&fi,nullptr,&Fence); return vr==VK_SUCCESS?true:Fail("vkCreateFence",vr);
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr=Functions->vkBeginCommandBuffer(CommandBuffer,&begin); if (vr!=VK_SUCCESS) return Fail("vkBeginCommandBuffer",vr);
        VkBufferCopy vertexCopy{0,0,VertexDevice.Size}; Functions->vkCmdCopyBuffer(CommandBuffer,VertexUpload.Buffer,VertexDevice.Buffer,1,&vertexCopy);
        VkBufferMemoryBarrier vb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; vb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; vb.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT; vb.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; vb.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; vb.buffer=VertexDevice.Buffer; vb.size=VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(CommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&vb,0,nullptr);
        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; toTransfer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; toTransfer.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; toTransfer.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; toTransfer.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toTransfer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; toTransfer.image=Texture.Image; toTransfer.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; toTransfer.subresourceRange.levelCount=1; toTransfer.subresourceRange.layerCount=1;
        Functions->vkCmdPipelineBarrier(CommandBuffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toTransfer);
        VkBufferImageCopy texCopy{}; texCopy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; texCopy.imageSubresource.layerCount=1; texCopy.imageExtent={kTextureWidth,kTextureHeight,1};
        Functions->vkCmdCopyBufferToImage(CommandBuffer,TextureUpload.Buffer,Texture.Image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&texCopy);
        VkImageMemoryBarrier toSample=toTransfer; toSample.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; toSample.dstAccessMask=VK_ACCESS_SHADER_READ_BIT; toSample.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; toSample.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Functions->vkCmdPipelineBarrier(CommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&toSample); result.TextureUploadCompleted=true;
        VkClearValue clear{}; VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass=RenderPass; rb.framebuffer=Framebuffer; rb.renderArea.extent={kWidth,kHeight}; rb.clearValueCount=1; rb.pClearValues=&clear;
        Functions->vkCmdBeginRenderPass(CommandBuffer,&rb,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{}; viewport.width=static_cast<float>(kWidth); viewport.height=static_cast<float>(kHeight); viewport.minDepth=0.0f; viewport.maxDepth=1.0f;
        VkRect2D scissor{{0,0},{kWidth,kHeight}}; Functions->vkCmdSetViewport(CommandBuffer,0,1,&viewport); Functions->vkCmdSetScissor(CommandBuffer,0,1,&scissor);
        VkDeviceSize offset=0; Functions->vkCmdBindVertexBuffers(CommandBuffer,0,1,&VertexDevice.Buffer,&offset);
        melonDS::Vulkan::VulkanOpaquePushConstants push; push.ScreenSize={{static_cast<float>(kWidth),static_cast<float>(kHeight)}};
        Functions->vkCmdPushConstants(CommandBuffer,PipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        auto draw=[&](VkPipeline pipeline,std::uint32_t set,std::uint32_t first){ Functions->vkCmdBindPipeline(CommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); Functions->vkCmdBindDescriptorSets(CommandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,PipelineLayout,0,1,&DescriptorSets[set],0,nullptr); Functions->vkCmdDraw(CommandBuffer,3,1,first,0); ++result.DrawCount; };
        draw(OpaqueModulatePipeline,0,0); draw(OpaqueDecalPipeline,1,3); draw(OpaqueModulatePipeline,2,6); draw(OpaqueModulatePipeline,3,9); draw(TranslucentModulatePipeline,0,12); draw(TranslucentDecalPipeline,1,15);
        result.CommandBufferBindIntegrated=true; Functions->vkCmdEndRenderPass(CommandBuffer);
        VkBufferImageCopy copy{}; copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount=1; copy.imageExtent={kWidth,kHeight,1};
        Functions->vkCmdCopyImageToBuffer(CommandBuffer,ColorTarget.Image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,ColorReadback.Buffer,1,&copy);
        VkBufferMemoryBarrier read{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; read.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; read.dstAccessMask=VK_ACCESS_HOST_READ_BIT; read.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; read.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; read.buffer=ColorReadback.Buffer; read.size=VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(CommandBuffer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&read,0,nullptr);
        vr=Functions->vkEndCommandBuffer(CommandBuffer); if (vr!=VK_SUCCESS) return Fail("vkEndCommandBuffer",vr);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&CommandBuffer;
        vr=Functions->vkQueueSubmit(Context->graphicsQueue(),1,&submit,Fence); if (vr!=VK_SUCCESS) return Fail("vkQueueSubmit",vr);
        vr=Functions->vkWaitForFences(Device,1,&Fence,VK_TRUE,UINT64_MAX); if (vr!=VK_SUCCESS) return Fail("vkWaitForFences",vr);
        result.DrawSubmitted=true; return true;
    }

    std::array<std::uint8_t,4> ReadPixel(const std::uint8_t* bytes,std::uint32_t x,std::uint32_t y) const
    {
        std::array<std::uint8_t,4> value{}; std::memcpy(value.data(),bytes+(static_cast<std::size_t>(y)*kWidth+x)*4u,4u); return value;
    }

    std::array<std::uint8_t,4> Expected(VulkanTextureCombinerMode mode,
        std::array<float,4> vertex,std::array<float,4> texture,std::array<float,4> toon,
        bool translucent) const
    {
        VulkanTextureCombinerInput input; input.Mode=mode; input.VertexColor=vertex; input.TextureColor=texture; input.ToonColor=toon;
        auto color=melonDS::Vulkan::EvaluateVulkanTextureCombiner(input);
        if (translucent) { const float a=color[3]; color[0]*=a; color[1]*=a; color[2]*=a; }
        return StorageColor(melonDS::Vulkan::QuantizeVulkanColor8(color),Context->featureInfo().colorFormat);
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped=nullptr; VkResult vr=Functions->vkMapMemory(Device,ColorReadback.Memory,0,ColorReadback.Size,0,&mapped); if (vr!=VK_SUCCESS) return Fail("vkMapMemory(readback)",vr);
        const auto* bytes=static_cast<const std::uint8_t*>(mapped);
        const std::array<float,4> yellow{{1,1,0,1}}, green{{0,1,0,16.0f/31.0f}},
            blue{{0,0,1,1}}, red{{1,0,0,1}};
        const std::array<float,4> white{{1,1,1,1}}, fullRed{{1,0,0,1}}, gray{{128.0f/255.0f,128.0f/255.0f,128.0f/255.0f,1}}, toonBlue{{0,0,1,1}};
        const std::array<float,4> whiteTrans{{1,1,1,15.0f/31.0f}}, redTrans{{1,0,0,15.0f/31.0f}};
        result.Samples={
            {"opaque_modulate_clamp",12,12,Expected(VulkanTextureCombinerMode::Modulate,white,yellow,toonBlue,false)},
            {"opaque_decal_repeat",116,12,Expected(VulkanTextureCombinerMode::Decal,fullRed,green,toonBlue,false)},
            {"opaque_toon_mirror",140,12,Expected(VulkanTextureCombinerMode::Toon,gray,blue,toonBlue,false)},
            {"opaque_highlight_clamp",12,60,Expected(VulkanTextureCombinerMode::Highlight,gray,red,toonBlue,false)},
            {"translucent_modulate",116,60,Expected(VulkanTextureCombinerMode::Modulate,whiteTrans,green,toonBlue,true)},
            {"translucent_decal",140,60,Expected(VulkanTextureCombinerMode::Decal,redTrans,green,toonBlue,true)},
        };
        for (auto& s:result.Samples) { s.Actual=ReadPixel(bytes,s.X,s.Y); s.Matched=PixelMatches(s.Actual,s.Expected); }
        Functions->vkUnmapMemory(Device,ColorReadback.Memory); result.ColorReadbackCompleted=true;
        result.ClampModulatePassed=result.Samples[0].Matched; result.RepeatDecalPassed=result.Samples[1].Matched; result.MirrorToonPassed=result.Samples[2].Matched; result.ClampHighlightPassed=result.Samples[3].Matched; result.TranslucentModulatePassed=result.Samples[4].Matched; result.TranslucentDecalPassed=result.Samples[5].Matched;
        result.SamplesMatched=std::all_of(result.Samples.begin(),result.Samples.end(),[](const SampleExpectation& s){return s.Matched;}); return true;
    }

    void DestroyBuffer(BufferResource& r) { if (r.Buffer) Functions->vkDestroyBuffer(Device,r.Buffer,nullptr); if (r.Memory) Functions->vkFreeMemory(Device,r.Memory,nullptr); r={}; }
    void DestroyImage(ImageResource& r) { if (r.View) Functions->vkDestroyImageView(Device,r.View,nullptr); if (r.Image) Functions->vkDestroyImage(Device,r.Image,nullptr); if (r.Memory) Functions->vkFreeMemory(Device,r.Memory,nullptr); r={}; }
    void Destroy()
    {
        if (!Functions || !Device)
            return;
        Functions->vkDeviceWaitIdle(Device);
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        for (VkPipeline p:{OpaqueModulatePipeline,OpaqueDecalPipeline,TranslucentModulatePipeline,TranslucentDecalPipeline}) if (p) Functions->vkDestroyPipeline(Device,p,nullptr);
        for (VkShaderModule s:{VertexShader,OpaqueModulateShader,OpaqueDecalShader,TranslucentModulateShader,TranslucentDecalShader}) if (s) Functions->vkDestroyShaderModule(Device,s,nullptr);
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
        for (VkSampler s:{ClampSampler,RepeatSampler,MirrorSampler}) if (s) Functions->vkDestroySampler(Device,s,nullptr);
        DestroyImage(Texture); DestroyImage(ColorTarget); DestroyBuffer(ColorReadback); for (auto& b:ConfigBuffers) DestroyBuffer(b); DestroyBuffer(TextureUpload); DestroyBuffer(VertexDevice); DestroyBuffer(VertexUpload);
        Functions=nullptr; Device=VK_NULL_HANDLE;
    }

    std::shared_ptr<DeviceContext> Context; QWindow* Window=nullptr; VkDevice Device=VK_NULL_HANDLE; QVulkanDeviceFunctions* Functions=nullptr;
    std::vector<VulkanPackedVertex> Vertices; std::array<VulkanToonHighlightConfig,kDescriptorCount> Configs{};
    BufferResource VertexUpload,VertexDevice,TextureUpload,ColorReadback; std::array<BufferResource,kDescriptorCount> ConfigBuffers{};
    ImageResource Texture,ColorTarget; VkSampler ClampSampler=VK_NULL_HANDLE,RepeatSampler=VK_NULL_HANDLE,MirrorSampler=VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorSetLayout=VK_NULL_HANDLE; VkDescriptorPool DescriptorPool=VK_NULL_HANDLE; std::array<VkDescriptorSet,kDescriptorCount> DescriptorSets{};
    VkRenderPass RenderPass=VK_NULL_HANDLE; VkFramebuffer Framebuffer=VK_NULL_HANDLE; VkPipelineLayout PipelineLayout=VK_NULL_HANDLE;
    VkShaderModule VertexShader=VK_NULL_HANDLE,OpaqueModulateShader=VK_NULL_HANDLE,OpaqueDecalShader=VK_NULL_HANDLE,TranslucentModulateShader=VK_NULL_HANDLE,TranslucentDecalShader=VK_NULL_HANDLE;
    VkPipeline OpaqueModulatePipeline=VK_NULL_HANDLE,OpaqueDecalPipeline=VK_NULL_HANDLE,TranslucentModulatePipeline=VK_NULL_HANDLE,TranslucentDecalPipeline=VK_NULL_HANDLE;
    VkCommandPool CommandPool=VK_NULL_HANDLE; VkCommandBuffer CommandBuffer=VK_NULL_HANDLE; VkFence Fence=VK_NULL_HANDLE;
    std::string FailureStage; VkResult FailureResult=VK_SUCCESS;
};

QJsonObject SampleJson(const SampleExpectation& s)
{
    QJsonArray expected,actual; for (auto v:s.Expected) expected.append(static_cast<int>(v)); for (auto v:s.Actual) actual.append(static_cast<int>(v));
    return {{"name",s.Name},{"x",static_cast<int>(s.X)},{"y",static_cast<int>(s.Y)},{"expected",expected},{"actual",actual},{"matched",s.Matched}};
}

QJsonObject ProbeJson(const ProbeResult& r)
{
    QJsonArray samples; for (const auto& s:r.Samples) samples.append(SampleJson(s));
    return {{"passed",r.Passed},{"integer_texture_created",r.IntegerTextureCreated},{"texture_upload_completed",r.TextureUploadCompleted},{"clamp_sampler_created",r.ClampSamplerCreated},{"repeat_sampler_created",r.RepeatSamplerCreated},{"mirror_sampler_created",r.MirrorSamplerCreated},{"descriptor_layout_created",r.DescriptorLayoutCreated},{"descriptor_pool_created",r.DescriptorPoolCreated},{"descriptor_sets_allocated",r.DescriptorSetsAllocated},{"descriptor_update_integrated",r.DescriptorUpdateIntegrated},{"pipeline_layout_integrated",r.PipelineLayoutIntegrated},{"command_buffer_bind_integrated",r.CommandBufferBindIntegrated},{"device_local_vertex_buffer",r.DeviceLocalVertexBuffer},{"opaque_modulate_pipeline_created",r.OpaqueModulatePipelineCreated},{"opaque_decal_pipeline_created",r.OpaqueDecalPipelineCreated},{"translucent_modulate_pipeline_created",r.TranslucentModulatePipelineCreated},{"translucent_decal_pipeline_created",r.TranslucentDecalPipelineCreated},{"draw_submitted",r.DrawSubmitted},{"draw_count",static_cast<int>(r.DrawCount)},{"color_readback_completed",r.ColorReadbackCompleted},{"clamp_modulate_passed",r.ClampModulatePassed},{"repeat_decal_passed",r.RepeatDecalPassed},{"mirror_toon_passed",r.MirrorToonPassed},{"clamp_highlight_passed",r.ClampHighlightPassed},{"translucent_modulate_passed",r.TranslucentModulatePassed},{"translucent_decal_passed",r.TranslucentDecalPassed},{"samples_matched",r.SamplesMatched},{"failure_stage",QString::fromStdString(r.FailureStage)},{"vk_result",static_cast<int>(r.FailureResult)},{"samples",samples}};
}

} // namespace

int RunTexturedPolygonBootstrapHarness(const QString& outputPath,int iterations)
{
    if (iterations <= 0)
        iterations = 1;
    FeatureInfo lastInfo;
    ProbeResult last;
    QJsonArray results;
    int completed = 0;
    auto& host=static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated()) last.FailureStage=host.unavailableReason();
    else for (int iteration=0;iteration<iterations;++iteration)
    {
        QWindow window; window.setSurfaceType(QSurface::VulkanSurface); window.setVulkanInstance(&host.instance()); window.resize(1,1); window.create();
        auto context=CreateDeviceContext(&window,lastInfo); if (!context) { last.FailureStage=lastInfo.unavailableReason; window.destroy(); break; }
        { TexturedPolygonProbe probe(context,&window); last=probe.Run(); }
        context.reset(); window.destroy(); results.append(ProbeJson(last)); if (!last.Passed) break; ++completed;
    }
    const bool passed=completed==iterations;
    melonDS::Platform::Log(passed?melonDS::Platform::LogLevel::Info:melonDS::Platform::LogLevel::Error,
        passed?"[MelonPrime] Vulkan textured polygon draw passed: iterations=%d draws=%u\n":"[MelonPrime] Vulkan textured polygon draw failed: completed=%d draws=%u\n",completed,last.DrawCount);
    const QJsonObject output{{"schema_version",1},{"passed",passed},{"contract_version",1},{"requested_iterations",iterations},{"completed_iterations",completed},{"draw_count",static_cast<int>(last.DrawCount)},{"integer_texture_created",last.IntegerTextureCreated},{"texture_upload_completed",last.TextureUploadCompleted},{"clamp_sampler_created",last.ClampSamplerCreated},{"repeat_sampler_created",last.RepeatSamplerCreated},{"mirror_sampler_created",last.MirrorSamplerCreated},{"descriptor_layout_created",last.DescriptorLayoutCreated},{"descriptor_pool_created",last.DescriptorPoolCreated},{"descriptor_sets_allocated",last.DescriptorSetsAllocated},{"descriptor_update_integrated",last.DescriptorUpdateIntegrated},{"pipeline_layout_integrated",last.PipelineLayoutIntegrated},{"command_buffer_bind_integrated",last.CommandBufferBindIntegrated},{"device_local_vertex_buffer",last.DeviceLocalVertexBuffer},{"opaque_modulate_pipeline_created",last.OpaqueModulatePipelineCreated},{"opaque_decal_pipeline_created",last.OpaqueDecalPipelineCreated},{"translucent_modulate_pipeline_created",last.TranslucentModulatePipelineCreated},{"translucent_decal_pipeline_created",last.TranslucentDecalPipelineCreated},{"clamp_modulate_passed",last.ClampModulatePassed},{"repeat_decal_passed",last.RepeatDecalPassed},{"mirror_toon_passed",last.MirrorToonPassed},{"clamp_highlight_passed",last.ClampHighlightPassed},{"translucent_modulate_passed",last.TranslucentModulatePassed},{"translucent_decal_passed",last.TranslucentDecalPassed},{"samples_matched",last.SamplesMatched},{"textured_polygon_gpu_draw_integrated",true},{"texture_cache_integrated",false},{"software_game_rendering_preserved",true},{"native_ds_polygon_raster_integrated",false},{"failure_stage",QString::fromStdString(last.FailureStage)},{"vk_result",static_cast<int>(last.FailureResult)},{"iterations",results}};
    QFile file(outputPath); if (!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) return 2; file.write(QJsonDocument(output).toJson(QJsonDocument::Indented)); return passed?0:1;
}

} // namespace MelonPrime::Vulkan
