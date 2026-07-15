#include "MelonPrimeVulkanOverlayRenderer.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

#include <sapphire/MelonPrimeVulkanOverlayFragShaderData.h>
#include <sapphire/MelonPrimeVulkanOverlayVertShaderData.h>

#include "VulkanContext.h"
#include "VulkanR24Sync.h"
#include "Platform.h"

using namespace melonDS::Vulkan::GeneratedShaders;

namespace
{
VkShaderModule CreateShaderModule(VkDevice dev, const std::vector<melonDS::u32>& code)
{
    if (code.empty())
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(melonDS::u32);
    createInfo.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &createInfo, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

void DestroyShaderModule(VkDevice dev, VkShaderModule& module)
{
    if (module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(dev, module, nullptr);
        module = VK_NULL_HANDLE;
    }
}

VkDeviceSize RoundUpToPowerOfTwo(VkDeviceSize value)
{
    if (value <= 1)
        return 1;
    VkDeviceSize result = 1;
    while (result < value)
        result <<= 1;
    return result;
}
} // namespace

MelonPrimeVulkanOverlayRenderer::~MelonPrimeVulkanOverlayRenderer()
{
    shutdown();
}

bool MelonPrimeVulkanOverlayRenderer::init()
{
    if (initialized)
        return true;

    auto& context = melonDS::VulkanContext::Get();
    if (context.GetDevice() == VK_NULL_HANDLE)
        return false;

    device = context.GetDevice();
    physicalDevice = context.GetPhysicalDevice();
    transferQueue = context.GetGraphicsQueue();
    transferQueueFamilyIndex = context.GetGraphicsQueueFamily();
    if (device == VK_NULL_HANDLE || transferQueue == VK_NULL_HANDLE)
        return false;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
        return false;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(OverlayPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
        return false;

    if (!createTransferResources())
        return false;

    initialized = true;
    return true;
}

void MelonPrimeVulkanOverlayRenderer::shutdown()
{
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);

    destroyPipeline();
    destroyTexture();
    destroyTransferResources();

    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    descriptorSet = VK_NULL_HANDLE;

    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    transferQueue = VK_NULL_HANDLE;
    transferQueueFamilyIndex = UINT32_MAX;
    initialized = false;
    compositeRequested = false;
    pendingUpload = false;
    hasValidUploadedOverlay = false;
    textureInitialized = false;
    textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool MelonPrimeVulkanOverlayRenderer::createTransferResources()
{
    stagingCapacity = 0;
    return true;
}

void MelonPrimeVulkanOverlayRenderer::destroyStagingBuffer()
{
    if (stagingMapped != nullptr && stagingMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(device, stagingMemory);
        stagingMapped = nullptr;
    }
    if (stagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, stagingMemory, nullptr);
        stagingMemory = VK_NULL_HANDLE;
    }
    if (stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
    }
    stagingCapacity = 0;
}

bool MelonPrimeVulkanOverlayRenderer::createMappedStagingBuffer(VkDeviceSize capacity)
{
    if (capacity == 0)
        return false;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacity;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        melonDS::VulkanContext::Get().FindMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS)
        return false;
    if (vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS)
        return false;
    if (vkMapMemory(device, stagingMemory, 0, capacity, 0, &stagingMapped) != VK_SUCCESS)
        return false;

    stagingCapacity = capacity;
    return true;
}

bool MelonPrimeVulkanOverlayRenderer::ensureStagingCapacity(VkDeviceSize required)
{
    if (required <= stagingCapacity)
        return true;

    destroyStagingBuffer();
    return createMappedStagingBuffer(RoundUpToPowerOfTwo(required));
}

void MelonPrimeVulkanOverlayRenderer::destroyTransferResources()
{
    destroyStagingBuffer();
    stagingCapacity = 0;
    pendingUploadBytes = 0;
}

bool MelonPrimeVulkanOverlayRenderer::createTexture(melonDS::u32 width, melonDS::u32 height)
{
    destroyTexture();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, textureImage, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = melonDS::VulkanContext::Get().FindMemoryType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocInfo, nullptr, &textureMemory) != VK_SUCCESS)
        return false;
    if (vkBindImageMemory(device, textureImage, textureMemory, 0) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &textureView) != VK_SUCCESS)
        return false;

    VkDescriptorImageInfo imageInfoDesc{};
    imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfoDesc.imageView = textureView;
    imageInfoDesc.sampler = sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfoDesc;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    textureWidth = width;
    textureHeight = height;
    boundRenderPass = VK_NULL_HANDLE;
    return true;
}

void MelonPrimeVulkanOverlayRenderer::destroyTexture()
{
    if (textureView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, textureView, nullptr);
        textureView = VK_NULL_HANDLE;
    }
    if (textureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, textureImage, nullptr);
        textureImage = VK_NULL_HANDLE;
    }
    if (textureMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, textureMemory, nullptr);
        textureMemory = VK_NULL_HANDLE;
    }
    textureWidth = 0;
    textureHeight = 0;
    textureInitialized = false;
    textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    hasValidUploadedOverlay = false;
}

bool MelonPrimeVulkanOverlayRenderer::ensureTexture(melonDS::u32 width, melonDS::u32 height)
{
    if (!initialized)
        return false;
    if (width == 0 || height == 0)
        return false;
    if (textureImage != VK_NULL_HANDLE && textureWidth == width && textureHeight == height)
        return true;
    return createTexture(width, height);
}

bool MelonPrimeVulkanOverlayRenderer::uploadRegion(const QImage& image, const QRect& rect)
{
    if (!initialized || rect.isEmpty())
        return false;

    const QImage source = image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!ensureTexture(static_cast<melonDS::u32>(source.width()),
            static_cast<melonDS::u32>(source.height())))
    {
        return false;
    }

    pendingUploadImage = source;
    pendingUploadRect = rect;
    pendingUpload = true;
    return stagePendingRegion();
}

bool MelonPrimeVulkanOverlayRenderer::stagePendingRegion()
{
    if (!pendingUpload || pendingUploadRect.isEmpty())
        return false;

    const QRect rect = pendingUploadRect.intersected(pendingUploadImage.rect());
    if (rect.isEmpty())
    {
        pendingUpload = false;
        return true;
    }

#ifndef NDEBUG
    assert(rect.x() >= 0);
    assert(rect.y() >= 0);
    assert(rect.right() < pendingUploadImage.width());
    assert(rect.bottom() < pendingUploadImage.height());
#endif

    const melonDS::u32 rowBytes = static_cast<melonDS::u32>(rect.width()) * 4u;
    const VkDeviceSize uploadBytes =
        static_cast<VkDeviceSize>(rowBytes) * static_cast<VkDeviceSize>(rect.height());
    if (!ensureStagingCapacity(uploadBytes))
    {
        if (uploadFailureLogBudget == 0)
        {
            uploadFailureLogBudget = 60;
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Warn,
                "[VulkanHUD] upload failed: staging capacity=%llu required=%llu rect=%dx%d@%d,%d\n",
                static_cast<unsigned long long>(stagingCapacity),
                static_cast<unsigned long long>(uploadBytes),
                rect.width(),
                rect.height(),
                rect.x(),
                rect.y());
        }
        else
        {
            --uploadFailureLogBudget;
        }
        return false;
    }

    auto* dst = static_cast<melonDS::u8*>(stagingMapped);
    for (int y = 0; y < rect.height(); ++y)
    {
        const melonDS::u8* src = pendingUploadImage.constScanLine(rect.y() + y)
            + rect.x() * 4;
        std::memcpy(dst + static_cast<size_t>(y) * rowBytes, src, rowBytes);
    }

    pendingUploadBytes = uploadBytes;
    return true;
}

bool MelonPrimeVulkanOverlayRenderer::recordPendingTransfer(VkCommandBuffer commandBuffer)
{
    if (!pendingUpload || pendingUploadRect.isEmpty() || stagingBuffer == VK_NULL_HANDLE)
        return false;

    const QRect rect = pendingUploadRect.intersected(pendingUploadImage.rect());
    if (rect.isEmpty())
    {
        pendingUpload = false;
        pendingUploadBytes = 0;
        return true;
    }

    const VkDeviceSize uploadBytes = pendingUploadBytes;
    melonDS::VulkanR24Barrier::HostWriteToShaderRead(
        commandBuffer,
        stagingBuffer,
        uploadBytes,
        VK_PIPELINE_STAGE_TRANSFER_BIT);

    const VkImageLayout oldLayout = textureInitialized
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    const VkPipelineStageFlags srcStage = textureInitialized
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkAccessFlags srcAccess = textureInitialized
        ? VK_ACCESS_SHADER_READ_BIT
        : 0;

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = srcAccess;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = oldLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = textureImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {
        static_cast<melonDS::u32>(rect.x()),
        static_cast<melonDS::u32>(rect.y()),
        0};
    region.imageExtent = {
        static_cast<melonDS::u32>(rect.width()),
        static_cast<melonDS::u32>(rect.height()),
        1};
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    melonDS::VulkanR24Barrier::TransferWriteToShaderRead(
        commandBuffer,
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textureInitialized = true;
    textureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hasValidUploadedOverlay = true;
    ++lastUploadedHudGeneration;
    pendingUpload = false;
    pendingUploadBytes = 0;
    return true;
}

void MelonPrimeVulkanOverlayRenderer::recordTransfer(VkCommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE)
        return;
    (void)recordPendingTransfer(commandBuffer);
}

void MelonPrimeVulkanOverlayRenderer::setCompositeRect(
    melonDS::u32 x, melonDS::u32 y, melonDS::u32 width, melonDS::u32 height)
{
    compositeX = x;
    compositeY = y;
    compositeWidth = width;
    compositeHeight = height;
    compositeRequested = width > 0 && height > 0;
}

void MelonPrimeVulkanOverlayRenderer::clearCompositeRequest()
{
    compositeRequested = false;
    hasValidUploadedOverlay = false;
}

bool MelonPrimeVulkanOverlayRenderer::ensurePipeline(VkRenderPass renderPass, VkFormat format)
{
    if (pipeline != VK_NULL_HANDLE
        && boundRenderPass == renderPass
        && boundSwapchainFormat == format)
    {
        return true;
    }

    destroyPipeline();

    const std::vector<melonDS::u32> vertCode(
        melonprime_vulkan_overlay_vert_spv,
        melonprime_vulkan_overlay_vert_spv + melonprime_vulkan_overlay_vert_spv_len);
    const std::vector<melonDS::u32> fragCode(
        melonprime_vulkan_overlay_frag_spv,
        melonprime_vulkan_overlay_frag_spv + melonprime_vulkan_overlay_frag_spv_len);

    VkShaderModule vertModule = CreateShaderModule(device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(device, fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE)
    {
        DestroyShaderModule(device, vertModule);
        DestroyShaderModule(device, fragModule);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
        | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT
        | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    DestroyShaderModule(device, vertModule);
    DestroyShaderModule(device, fragModule);
    if (result != VK_SUCCESS)
        return false;

    boundRenderPass = renderPass;
    boundSwapchainFormat = format;
    return true;
}

void MelonPrimeVulkanOverlayRenderer::destroyPipeline()
{
    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    boundRenderPass = VK_NULL_HANDLE;
    boundSwapchainFormat = VK_FORMAT_UNDEFINED;
}

void MelonPrimeVulkanOverlayRenderer::record(
    const MelonDSAndroid::VulkanSurfacePresenter::VulkanDesktopOverlayTarget& target)
{
    if (!initialized || !compositeRequested || textureView == VK_NULL_HANDLE)
        return;
    if (!hasValidUploadedOverlay)
        return;
    if (target.commandBuffer == VK_NULL_HANDLE || target.renderPass == VK_NULL_HANDLE)
        return;
    if (!ensurePipeline(target.renderPass, target.swapchainFormat))
        return;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(target.extent.height);
    viewport.width = static_cast<float>(target.extent.width);
    viewport.height = -static_cast<float>(target.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = target.extent;

    OverlayPushConstants push{};
    push.surfaceSize[0] = static_cast<float>(target.extent.width);
    push.surfaceSize[1] = static_cast<float>(target.extent.height);
    push.drawOrigin[0] = static_cast<float>(compositeX);
    push.drawOrigin[1] = static_cast<float>(compositeY);
    push.drawSize[0] = static_cast<float>(compositeWidth);
    push.drawSize[1] = static_cast<float>(compositeHeight);

    vkCmdBindPipeline(target.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(target.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(target.commandBuffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(
        target.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    vkCmdPushConstants(
        target.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(push),
        &push);
    vkCmdDraw(target.commandBuffer, 6, 1, 0, 0);
}

void MelonPrimeVulkanOverlayRenderer::RecordTransferCallback(
    VkCommandBuffer commandBuffer,
    void* userData)
{
    if (userData == nullptr)
        return;
    static_cast<MelonPrimeVulkanOverlayRenderer*>(userData)->recordTransfer(commandBuffer);
}

void MelonPrimeVulkanOverlayRenderer::RecordCallback(
    const MelonDSAndroid::VulkanSurfacePresenter::VulkanDesktopOverlayTarget& target,
    void* userData)
{
    if (userData == nullptr)
        return;
    static_cast<MelonPrimeVulkanOverlayRenderer*>(userData)->record(target);
}
