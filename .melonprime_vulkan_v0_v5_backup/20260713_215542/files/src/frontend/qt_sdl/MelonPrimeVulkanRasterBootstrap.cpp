#include "MelonPrimeVulkanRasterBootstrap.h"

#include <QApplication>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWindow>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"
#include "../../../Vulkan_shaders/generated/VulkanShaders.h"

namespace MelonPrime::Vulkan
{
namespace
{

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 192;
constexpr std::array<std::uint8_t, 4> kLogicalRgba{{0x33, 0x66, 0xCC, 0xFF}};

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
};

struct ProbeResult
{
    bool Passed = false;
    bool DrawSubmitted = false;
    bool ReadbackCompleted = false;
    bool SamplesMatched = false;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::array<std::uint8_t, 4> ExpectedStorage{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> FirstSample{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> CenterSample{{0, 0, 0, 0}};
    std::array<std::uint8_t, 4> LastSample{{0, 0, 0, 0}};
};

class RasterBootstrapProbe
{
public:
    RasterBootstrapProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~RasterBootstrapProbe()
    {
        Destroy();
    }

    ProbeResult Run()
    {
        ProbeResult result;
        result.ExpectedStorage = StorageBytesForFormat(Context->featureInfo().colorFormat);

        if (!CreateResources(result.ExpectedStorage))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        if (!RecordAndSubmit())
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
        result.ReadbackCompleted = true;
        result.SamplesMatched = SamplesMatch(result);
        result.Passed = result.SamplesMatched;
        if (!result.Passed)
            result.FailureStage = "readback pixel comparison";
        return result;
    }

private:
    static std::array<std::uint8_t, 4> StorageBytesForFormat(VkFormat format)
    {
        if (format == VK_FORMAT_B8G8R8A8_UNORM)
            return {{kLogicalRgba[2], kLogicalRgba[1], kLogicalRgba[0], kLogicalRgba[3]}};
        return kLogicalRgba;
    }

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
            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) &&
                (flags & required) == required &&
                (flags & preferred) == preferred)
            {
                return index;
            }
        }
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            const VkMemoryPropertyFlags flags = properties.memoryTypes[index].propertyFlags;
            if ((typeBits & (1u << index)) && (flags & required) == required)
                return index;
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        BufferResource& resource)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vkResult = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateBuffer", vkResult);

        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, required, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("host-visible coherent memory type", VK_ERROR_FEATURE_NOT_PRESENT);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        vkResult = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateMemory(buffer)", vkResult);
        vkResult = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (vkResult != VK_SUCCESS)
            return Fail("vkBindBufferMemory", vkResult);
        return true;
    }

    bool CreateImage(
        std::uint32_t width,
        std::uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        ImageResource& resource)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vkResult = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateImage", vkResult);

        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        const std::uint32_t memoryType = FindMemoryType(
            requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        vkResult = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateMemory(image)", vkResult);
        vkResult = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (vkResult != VK_SUCCESS)
            return Fail("vkBindImageMemory", vkResult);

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        vkResult = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateImageView", vkResult);
        return true;
    }

    bool CreateShaderModule(
        const std::uint32_t* code,
        std::size_t size,
        VkShaderModule& module)
    {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        const VkResult result = Functions->vkCreateShaderModule(Device, &info, nullptr, &module);
        if (result != VK_SUCCESS)
            return Fail("vkCreateShaderModule", result);
        return true;
    }

    bool CreateResources(const std::array<std::uint8_t, 4>& textureBytes)
    {
        const FeatureInfo& info = Context->featureInfo();
        const VkDeviceSize frameBytes = static_cast<VkDeviceSize>(kWidth) * kHeight * 4u;
        if (!CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                UploadBuffer))
        {
            return false;
        }
        if (!CreateBuffer(frameBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                ReadbackBuffer))
        {
            return false;
        }

        void* uploadMap = nullptr;
        VkResult vkResult = Functions->vkMapMemory(
            Device, UploadBuffer.Memory, 0, 4, 0, &uploadMap);
        if (vkResult != VK_SUCCESS)
            return Fail("vkMapMemory(upload)", vkResult);
        std::memcpy(uploadMap, textureBytes.data(), textureBytes.size());
        Functions->vkUnmapMemory(Device, UploadBuffer.Memory);

        if (!CreateImage(1, 1, info.colorFormat,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, TextureImage))
        {
            return false;
        }
        if (!CreateImage(kWidth, kHeight, info.colorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, ColorImage))
        {
            return false;
        }
        VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (info.depthStencilFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            info.depthStencilFormat == VK_FORMAT_D24_UNORM_S8_UINT)
        {
            depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (!CreateImage(kWidth, kHeight, info.depthStencilFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                depthAspect, DepthImage))
        {
            return false;
        }

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 0.0f;
        vkResult = Functions->vkCreateSampler(Device, &samplerInfo, nullptr, &Sampler);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateSampler", vkResult);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptorLayoutInfo.bindingCount = 1;
        descriptorLayoutInfo.pBindings = &binding;
        vkResult = Functions->vkCreateDescriptorSetLayout(
            Device, &descriptorLayoutInfo, nullptr, &DescriptorLayout);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateDescriptorSetLayout", vkResult);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkResult = Functions->vkCreateDescriptorPool(Device, &poolInfo, nullptr, &DescriptorPool);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateDescriptorPool", vkResult);

        VkDescriptorSetAllocateInfo setAllocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAllocate.descriptorPool = DescriptorPool;
        setAllocate.descriptorSetCount = 1;
        setAllocate.pSetLayouts = &DescriptorLayout;
        vkResult = Functions->vkAllocateDescriptorSets(Device, &setAllocate, &DescriptorSet);
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateDescriptorSets", vkResult);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = Sampler;
        imageInfo.imageView = TextureImage.View;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = DescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        Functions->vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);

        VkAttachmentDescription attachments[2]{};
        attachments[0].format = info.colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        attachments[1].format = info.depthStencilFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;
        vkResult = Functions->vkCreateRenderPass(Device, &renderPassInfo, nullptr, &RenderPass);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateRenderPass", vkResult);

        const VkImageView framebufferAttachments[] = {ColorImage.View, DepthImage.View};
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = RenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = framebufferAttachments;
        framebufferInfo.width = kWidth;
        framebufferInfo.height = kHeight;
        framebufferInfo.layers = 1;
        vkResult = Functions->vkCreateFramebuffer(Device, &framebufferInfo, nullptr, &Framebuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateFramebuffer", vkResult);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &DescriptorLayout;
        vkResult = Functions->vkCreatePipelineLayout(
            Device, &pipelineLayoutInfo, nullptr, &PipelineLayout);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreatePipelineLayout", vkResult);

        if (!CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanPresenterVertexSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPresenterVertexSpirv), VertexShader) ||
            !CreateShaderModule(
                melonDS::Vulkan::Shaders::kVulkanPresenterFragmentSpirv,
                sizeof(melonDS::Vulkan::Shaders::kVulkanPresenterFragmentSpirv), FragmentShader))
        {
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = VertexShader;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = FragmentShader;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttachment;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = PipelineLayout;
        pipelineInfo.renderPass = RenderPass;
        pipelineInfo.subpass = 0;
        vkResult = Functions->vkCreateGraphicsPipelines(
            Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &Pipeline);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateGraphicsPipelines", vkResult);

        VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = info.graphicsQueueFamily;
        vkResult = Functions->vkCreateCommandPool(
            Device, &commandPoolInfo, nullptr, &CommandPool);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateCommandPool", vkResult);
        VkCommandBufferAllocateInfo commandAllocate{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandAllocate.commandPool = CommandPool;
        commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAllocate.commandBufferCount = 1;
        vkResult = Functions->vkAllocateCommandBuffers(Device, &commandAllocate, &CommandBuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkAllocateCommandBuffers", vkResult);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vkResult = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &Fence);
        if (vkResult != VK_SUCCESS)
            return Fail("vkCreateFence", vkResult);
        return true;
    }

    bool RecordAndSubmit()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vkResult = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vkResult != VK_SUCCESS)
            return Fail("vkBeginCommandBuffer", vkResult);

        VkImageMemoryBarrier textureToTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        textureToTransfer.srcAccessMask = 0;
        textureToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        textureToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        textureToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        textureToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        textureToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        textureToTransfer.image = TextureImage.Image;
        textureToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        textureToTransfer.subresourceRange.levelCount = 1;
        textureToTransfer.subresourceRange.layerCount = 1;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &textureToTransfer);

        VkBufferImageCopy textureCopy{};
        textureCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        textureCopy.imageSubresource.mipLevel = 0;
        textureCopy.imageSubresource.baseArrayLayer = 0;
        textureCopy.imageSubresource.layerCount = 1;
        textureCopy.imageExtent = {1, 1, 1};
        Functions->vkCmdCopyBufferToImage(
            CommandBuffer,
            UploadBuffer.Buffer,
            TextureImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &textureCopy);

        VkImageMemoryBarrier textureToShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        textureToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        textureToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        textureToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        textureToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        textureToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        textureToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        textureToShader.image = TextureImage.Image;
        textureToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        textureToShader.subresourceRange.levelCount = 1;
        textureToShader.subresourceRange.layerCount = 1;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &textureToShader);

        VkClearValue clearValues[2]{};
        clearValues[0].color.float32[0] = 0.0f;
        clearValues[0].color.float32[1] = 0.0f;
        clearValues[0].color.float32[2] = 0.0f;
        clearValues[0].color.float32[3] = 1.0f;
        clearValues[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo renderPassBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassBegin.renderPass = RenderPass;
        renderPassBegin.framebuffer = Framebuffer;
        renderPassBegin.renderArea.extent = {kWidth, kHeight};
        renderPassBegin.clearValueCount = 2;
        renderPassBegin.pClearValues = clearValues;
        Functions->vkCmdBeginRenderPass(
            CommandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
        Functions->vkCmdBindPipeline(
            CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
        Functions->vkCmdBindDescriptorSets(
            CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            PipelineLayout,
            0,
            1,
            &DescriptorSet,
            0,
            nullptr);
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(kWidth);
        viewport.height = static_cast<float>(kHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = {kWidth, kHeight};
        Functions->vkCmdSetViewport(CommandBuffer, 0, 1, &viewport);
        Functions->vkCmdSetScissor(CommandBuffer, 0, 1, &scissor);
        Functions->vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
        Functions->vkCmdEndRenderPass(CommandBuffer);

        VkBufferImageCopy frameCopy{};
        frameCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        frameCopy.imageSubresource.mipLevel = 0;
        frameCopy.imageSubresource.baseArrayLayer = 0;
        frameCopy.imageSubresource.layerCount = 1;
        frameCopy.imageExtent = {kWidth, kHeight, 1};
        Functions->vkCmdCopyImageToBuffer(
            CommandBuffer,
            ColorImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ReadbackBuffer.Buffer,
            1,
            &frameCopy);

        VkBufferMemoryBarrier hostReadBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostReadBarrier.buffer = ReadbackBuffer.Buffer;
        hostReadBarrier.offset = 0;
        hostReadBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(
            CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &hostReadBarrier, 0, nullptr);

        vkResult = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vkResult != VK_SUCCESS)
            return Fail("vkEndCommandBuffer", vkResult);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        vkResult = Functions->vkQueueSubmit(Context->graphicsQueue(), 1, &submit, Fence);
        if (vkResult != VK_SUCCESS)
            return Fail("vkQueueSubmit", vkResult);
        vkResult = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, 5'000'000'000ull);
        if (vkResult != VK_SUCCESS)
            return Fail("vkWaitForFences", vkResult);
        return true;
    }

    static std::array<std::uint8_t, 4> ReadPixel(
        const std::uint8_t* data,
        std::size_t pixelIndex)
    {
        const std::size_t offset = pixelIndex * 4u;
        return {{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]}};
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped = nullptr;
        const VkDeviceSize frameBytes = static_cast<VkDeviceSize>(kWidth) * kHeight * 4u;
        const VkResult vkResult = Functions->vkMapMemory(
            Device, ReadbackBuffer.Memory, 0, frameBytes, 0, &mapped);
        if (vkResult != VK_SUCCESS)
            return Fail("vkMapMemory(readback)", vkResult);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped);
        result.FirstSample = ReadPixel(bytes, 0);
        result.CenterSample = ReadPixel(
            bytes, static_cast<std::size_t>(kHeight / 2) * kWidth + (kWidth / 2));
        result.LastSample = ReadPixel(bytes, static_cast<std::size_t>(kWidth) * kHeight - 1);
        Functions->vkUnmapMemory(Device, ReadbackBuffer.Memory);
        return true;
    }

    static bool PixelMatches(
        const std::array<std::uint8_t, 4>& actual,
        const std::array<std::uint8_t, 4>& expected)
    {
        for (std::size_t index = 0; index < actual.size(); ++index)
        {
            const int delta = static_cast<int>(actual[index]) - static_cast<int>(expected[index]);
            if (std::abs(delta) > 1)
                return false;
        }
        return true;
    }

    static bool SamplesMatch(const ProbeResult& result)
    {
        return PixelMatches(result.FirstSample, result.ExpectedStorage) &&
            PixelMatches(result.CenterSample, result.ExpectedStorage) &&
            PixelMatches(result.LastSample, result.ExpectedStorage);
    }

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        if (Fence)
            Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool)
            Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Pipeline)
            Functions->vkDestroyPipeline(Device, Pipeline, nullptr);
        if (VertexShader)
            Functions->vkDestroyShaderModule(Device, VertexShader, nullptr);
        if (FragmentShader)
            Functions->vkDestroyShaderModule(Device, FragmentShader, nullptr);
        if (PipelineLayout)
            Functions->vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (Framebuffer)
            Functions->vkDestroyFramebuffer(Device, Framebuffer, nullptr);
        if (RenderPass)
            Functions->vkDestroyRenderPass(Device, RenderPass, nullptr);
        if (DescriptorPool)
            Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (DescriptorLayout)
            Functions->vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        if (Sampler)
            Functions->vkDestroySampler(Device, Sampler, nullptr);
        DestroyImage(DepthImage);
        DestroyImage(ColorImage);
        DestroyImage(TextureImage);
        DestroyBuffer(ReadbackBuffer);
        DestroyBuffer(UploadBuffer);
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

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer)
            Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory)
            Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    QVulkanDeviceFunctions* Functions = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    BufferResource UploadBuffer;
    BufferResource ReadbackBuffer;
    ImageResource TextureImage;
    ImageResource ColorImage;
    ImageResource DepthImage;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkFramebuffer Framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkShaderModule VertexShader = VK_NULL_HANDLE;
    VkShaderModule FragmentShader = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonArray PixelArray(const std::array<std::uint8_t, 4>& pixel)
{
    return QJsonArray{
        static_cast<int>(pixel[0]),
        static_cast<int>(pixel[1]),
        static_cast<int>(pixel[2]),
        static_cast<int>(pixel[3])};
}

} // namespace

int RunRasterBootstrapHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0)
        iterations = 1;

    FeatureInfo lastInfo;
    ProbeResult lastResult;
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
                RasterBootstrapProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
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
            "[MelonPrime] Vulkan native raster bootstrap passed: iterations=%d size=%ux%u format=%d\n",
            completed,
            kWidth,
            kHeight,
            static_cast<int>(lastInfo.colorFormat));
    }
    else
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan native raster bootstrap failed: stage=%s VkResult=%d completed=%d/%d\n",
            lastResult.FailureStage.empty() ? "unknown" : lastResult.FailureStage.c_str(),
            static_cast<int>(lastResult.FailureResult),
            completed,
            iterations);
    }

    const QJsonObject object{
        {"schema_version", 1},
        {"passed", passed},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"width", static_cast<int>(kWidth)},
        {"height", static_cast<int>(kHeight)},
        {"device_name", QString::fromStdString(lastInfo.deviceName)},
        {"color_format", static_cast<int>(lastInfo.colorFormat)},
        {"depth_stencil_format", static_cast<int>(lastInfo.depthStencilFormat)},
        {"draw_submitted", lastResult.DrawSubmitted},
        {"readback_completed", lastResult.ReadbackCompleted},
        {"samples_matched", lastResult.SamplesMatched},
        {"expected_storage_bytes", PixelArray(lastResult.ExpectedStorage)},
        {"first_sample", PixelArray(lastResult.FirstSample)},
        {"center_sample", PixelArray(lastResult.CenterSample)},
        {"last_sample", PixelArray(lastResult.LastSample)},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"failure_vk_result", static_cast<int>(lastResult.FailureResult)},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
    };

    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    output.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    output.close();
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
