#include <cstring>
#include <mutex>
#include <utility>

#include "GPU3D_TexcacheVulkan.h"

#include "Platform.h"
#include "VulkanContext.h"
#include <volk.h>

namespace melonDS
{

TexcacheVulkanLoader::TexcacheVulkanLoader()
    : State(std::make_shared<SharedState>())
{
}

TexcacheVulkanLoader::~TexcacheVulkanLoader()
{
    if (State != nullptr && State.use_count() == 1)
        CleanupVulkanState();
}

void TexcacheVulkanLoader::SetRetirementPoint(
    VkSemaphore timelineSemaphore,
    u64 timelineValue)
{
    if (State == nullptr)
        return;

    State->RetirementTimeline = timelineSemaphore;
    State->RetirementValue = timelineValue;
    if (State->Device != VK_NULL_HANDLE)
    {
        auto& context = VulkanContext::Get();
        State->RetiredTextures.Collect(
            State->Device,
            context.GetSemaphoreCounterValue(),
            context.IsDeviceLost());
    }
}

void TexcacheVulkanLoader::DrainRetiredTextures()
{
    if (State == nullptr || State->Device == VK_NULL_HANDLE)
        return;

    auto& context = VulkanContext::Get();
    State->RetiredTextures.Drain(
        State->Device,
        context.GetWaitSemaphores(),
        context.IsDeviceLost());
}

bool TexcacheVulkanLoader::EnsureVulkanState()
{
    if (State == nullptr)
        State = std::make_shared<SharedState>();

    if (State->Device != VK_NULL_HANDLE)
        return !VulkanContext::Get().IsDeviceLost();

    auto& context = VulkanContext::Get();
    if (context.IsDeviceLost())
        return false;
    if (!context.Acquire())
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to acquire Vulkan context");
        return false;
    }

    State->ContextAcquired = true;
    State->Device = context.GetDevice();
    State->Queue = context.GetQueue();
    State->QueueFamilyIndex = context.GetQueueFamilyIndex();

    if (State->Device == VK_NULL_HANDLE || State->Queue == VK_NULL_HANDLE)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: invalid Vulkan context handles");
        CleanupVulkanState();
        return false;
    }

    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCreateInfo.queueFamilyIndex = State->QueueFamilyIndex;
    if (vkCreateCommandPool(State->Device, &poolCreateInfo, nullptr, &State->CommandPool) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create command pool");
        CleanupVulkanState();
        return false;
    }

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = State->CommandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(State->Device, &commandBufferAllocateInfo, &State->CommandBuffer) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to allocate command buffer");
        CleanupVulkanState();
        return false;
    }

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(State->Device, &fenceCreateInfo, nullptr, &State->UploadFence) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create upload fence");
        CleanupVulkanState();
        return false;
    }

    return true;
}

void TexcacheVulkanLoader::CleanupVulkanState()
{
    if (State == nullptr)
        return;

    if (State->Device != VK_NULL_HANDLE)
    {
        auto& context = VulkanContext::Get();
        if (!context.IsDeviceLost() && State->Queue != VK_NULL_HANDLE)
        {
            std::scoped_lock queueLock(context.GetQueueLock());
            (void)context.CheckResult(
                vkQueueWaitIdle(State->Queue),
                "TexcacheVulkan vkQueueWaitIdle during shutdown");
        }
        State->RetiredTextures.Drain(
            State->Device,
            context.GetWaitSemaphores(),
            context.IsDeviceLost());
    }

    for (auto& [handle, textureArray] : State->TextureArrays)
    {
        (void)handle;
        DestroyTextureArray(textureArray);
    }
    State->TextureArrays.clear();
    State->NextHandle = 1;
    State->RetirementTimeline = VK_NULL_HANDLE;
    State->RetirementValue = 0;

    if (State->UploadFence != VK_NULL_HANDLE && State->Device != VK_NULL_HANDLE)
    {
        vkDestroyFence(State->Device, State->UploadFence, nullptr);
        State->UploadFence = VK_NULL_HANDLE;
    }

    if (State->CommandBuffer != VK_NULL_HANDLE && State->CommandPool != VK_NULL_HANDLE && State->Device != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(State->Device, State->CommandPool, 1, &State->CommandBuffer);
    }
    State->CommandBuffer = VK_NULL_HANDLE;

    if (State->CommandPool != VK_NULL_HANDLE && State->Device != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(State->Device, State->CommandPool, nullptr);
        State->CommandPool = VK_NULL_HANDLE;
    }

    if (State->ContextAcquired)
    {
        VulkanContext::Get().Release();
        State->ContextAcquired = false;
    }

    State->Device = VK_NULL_HANDLE;
    State->Queue = VK_NULL_HANDLE;
    State->QueueFamilyIndex = 0;
}

void TexcacheVulkanLoader::DestroyTextureArray(TextureArray& textureArray)
{
    if (State == nullptr || State->Device == VK_NULL_HANDLE)
    {
        textureArray = TextureArray{};
        return;
    }

    VkDevice device = State->Device;

    if (textureArray.Sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, textureArray.Sampler, nullptr);
        textureArray.Sampler = VK_NULL_HANDLE;
    }

    if (textureArray.ArrayView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, textureArray.ArrayView, nullptr);
        textureArray.ArrayView = VK_NULL_HANDLE;
    }

    if (textureArray.StagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, textureArray.StagingBuffer, nullptr);
        textureArray.StagingBuffer = VK_NULL_HANDLE;
    }

    if (textureArray.StagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, textureArray.StagingMemory, nullptr);
        textureArray.StagingMemory = VK_NULL_HANDLE;
    }
    textureArray.StagingSize = 0;

    if (textureArray.Image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, textureArray.Image, nullptr);
        textureArray.Image = VK_NULL_HANDLE;
    }

    if (textureArray.Memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, textureArray.Memory, nullptr);
        textureArray.Memory = VK_NULL_HANDLE;
    }

    textureArray.Width = 0;
    textureArray.Height = 0;
    textureArray.Layers = 0;
}

TexcacheVulkanLoader::TextureHandle TexcacheVulkanLoader::GenerateTexture(u32 width, u32 height, u32 layers)
{
    if (width == 0 || height == 0 || layers == 0)
        return 0;

    if (!EnsureVulkanState())
        return 0;

    TextureArray textureArray{};
    textureArray.Width = width;
    textureArray.Height = height;
    textureArray.Layers = layers;
    textureArray.LayerOpaque.assign(layers, 0u);

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UINT;
    imageCreateInfo.extent.width = width;
    imageCreateInfo.extent.height = height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = layers;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(State->Device, &imageCreateInfo, nullptr, &textureArray.Image) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create image");
        return 0;
    }

    VkMemoryRequirements imageMemoryRequirements{};
    vkGetImageMemoryRequirements(State->Device, textureArray.Image, &imageMemoryRequirements);

    VkMemoryAllocateInfo imageMemoryAllocateInfo{};
    imageMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageMemoryAllocateInfo.allocationSize = imageMemoryRequirements.size;
    imageMemoryAllocateInfo.memoryTypeIndex = VulkanContext::Get().FindMemoryType(
        imageMemoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (imageMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX)
        imageMemoryAllocateInfo.memoryTypeIndex = VulkanContext::Get().FindMemoryType(imageMemoryRequirements.memoryTypeBits, 0);
    if (imageMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(State->Device, &imageMemoryAllocateInfo, nullptr, &textureArray.Memory) != VK_SUCCESS
        || vkBindImageMemory(State->Device, textureArray.Image, textureArray.Memory, 0) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to allocate image memory");
        DestroyTextureArray(textureArray);
        return 0;
    }

    VkImageViewCreateInfo arrayViewCreateInfo{};
    arrayViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayViewCreateInfo.image = textureArray.Image;
    arrayViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UINT;
    arrayViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    arrayViewCreateInfo.subresourceRange.baseMipLevel = 0;
    arrayViewCreateInfo.subresourceRange.levelCount = 1;
    arrayViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    arrayViewCreateInfo.subresourceRange.layerCount = layers;

    if (vkCreateImageView(State->Device, &arrayViewCreateInfo, nullptr, &textureArray.ArrayView) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create array view");
        DestroyTextureArray(textureArray);
        return 0;
    }

    VkSamplerCreateInfo samplerCreateInfo{};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.anisotropyEnable = VK_FALSE;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;
    samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(State->Device, &samplerCreateInfo, nullptr, &textureArray.Sampler) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create sampler");
        DestroyTextureArray(textureArray);
        return 0;
    }

    textureArray.StagingSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * sizeof(u32);
    VkBufferCreateInfo stagingBufferCreateInfo{};
    stagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferCreateInfo.size = textureArray.StagingSize;
    stagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(State->Device, &stagingBufferCreateInfo, nullptr, &textureArray.StagingBuffer) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to create staging buffer");
        DestroyTextureArray(textureArray);
        return 0;
    }

    VkMemoryRequirements stagingMemoryRequirements{};
    vkGetBufferMemoryRequirements(State->Device, textureArray.StagingBuffer, &stagingMemoryRequirements);

    VkMemoryAllocateInfo stagingMemoryAllocateInfo{};
    stagingMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingMemoryAllocateInfo.allocationSize = stagingMemoryRequirements.size;
    stagingMemoryAllocateInfo.memoryTypeIndex = VulkanContext::Get().FindMemoryType(
        stagingMemoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if (stagingMemoryAllocateInfo.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(State->Device, &stagingMemoryAllocateInfo, nullptr, &textureArray.StagingMemory) != VK_SUCCESS
        || vkBindBufferMemory(State->Device, textureArray.StagingBuffer, textureArray.StagingMemory, 0) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to allocate staging memory");
        DestroyTextureArray(textureArray);
        return 0;
    }

    const VkResult initialWaitResult = vkWaitForFences(
        State->Device, 1, &State->UploadFence, VK_TRUE, UINT64_MAX);
    const VkResult initialResetFenceResult = initialWaitResult == VK_SUCCESS
        ? vkResetFences(State->Device, 1, &State->UploadFence)
        : initialWaitResult;
    const VkResult initialResetCommandResult = initialResetFenceResult == VK_SUCCESS
        ? vkResetCommandBuffer(State->CommandBuffer, 0)
        : initialResetFenceResult;
    if (!VulkanContext::Get().CheckResult(
            initialWaitResult, "TexcacheVulkan initial upload fence")
        || !VulkanContext::Get().CheckResult(
            initialResetFenceResult, "TexcacheVulkan initial fence reset")
        || !VulkanContext::Get().CheckResult(
            initialResetCommandResult, "TexcacheVulkan initial command reset"))
    {
        Platform::Log(Platform::LogLevel::Error, "TexcacheVulkan: failed to prepare upload sync objects");
        DestroyTextureArray(textureArray);
        return 0;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(State->CommandBuffer, &beginInfo) != VK_SUCCESS)
    {
        DestroyTextureArray(textureArray);
        return 0;
    }

    constexpr VkPipelineStageFlags kTextureShaderStages =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    VkImageMemoryBarrier toGeneralBarrier{};
    toGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneralBarrier.srcAccessMask = 0;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = textureArray.Image;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = layers;
    vkCmdPipelineBarrier(
        State->CommandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        kTextureShaderStages,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toGeneralBarrier
    );

    if (vkEndCommandBuffer(State->CommandBuffer) != VK_SUCCESS)
    {
        DestroyTextureArray(textureArray);
        return 0;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &State->CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(
            State->Queue, 1, &submitInfo, State->UploadFence);
        if (!VulkanContext::Get().CheckResult(
                submitResult, "TexcacheVulkan initial texture upload"))
        {
            DestroyTextureArray(textureArray);
            return 0;
        }
    }

    if (!VulkanContext::Get().CheckResult(
            vkWaitForFences(State->Device, 1, &State->UploadFence, VK_TRUE, UINT64_MAX),
            "TexcacheVulkan initial upload completion"))
    {
        DestroyTextureArray(textureArray);
        return 0;
    }

    TextureHandle handle = State->NextHandle++;
    State->TextureArrays.emplace(handle, std::move(textureArray));
    return handle;
}

void TexcacheVulkanLoader::UploadTexture(TextureHandle handle, u32 width, u32 height, u32 layer, void* data)
{
    if (data == nullptr)
        return;

    if (!EnsureVulkanState())
        return;

    auto it = State->TextureArrays.find(handle);
    if (it == State->TextureArrays.end())
        return;

    TextureArray& textureArray = it->second;
    if (layer >= textureArray.Layers)
        return;
    if (textureArray.Width != width || textureArray.Height != height)
        return;

    const size_t layerPixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    bool layerOpaque = true;
    const u32* sourcePixels = static_cast<const u32*>(data);
    for (size_t pixel = 0; pixel < layerPixelCount; pixel++)
    {
        if (((sourcePixels[pixel] >> 24u) & 0x1Fu) != 0x1Fu)
        {
            layerOpaque = false;
            break;
        }
    }
    if (layer < textureArray.LayerOpaque.size())
        textureArray.LayerOpaque[layer] = layerOpaque ? 1u : 0u;

    void* mappedMemory = nullptr;
    if (vkMapMemory(State->Device, textureArray.StagingMemory, 0, textureArray.StagingSize, 0, &mappedMemory) != VK_SUCCESS)
        return;
    std::memcpy(mappedMemory, data, layerPixelCount * sizeof(u32));
    vkUnmapMemory(State->Device, textureArray.StagingMemory);

    const VkResult updateWaitResult = vkWaitForFences(
        State->Device, 1, &State->UploadFence, VK_TRUE, UINT64_MAX);
    const VkResult updateResetFenceResult = updateWaitResult == VK_SUCCESS
        ? vkResetFences(State->Device, 1, &State->UploadFence)
        : updateWaitResult;
    const VkResult updateResetCommandResult = updateResetFenceResult == VK_SUCCESS
        ? vkResetCommandBuffer(State->CommandBuffer, 0)
        : updateResetFenceResult;
    if (!VulkanContext::Get().CheckResult(
            updateWaitResult, "TexcacheVulkan update upload fence")
        || !VulkanContext::Get().CheckResult(
            updateResetFenceResult, "TexcacheVulkan update fence reset")
        || !VulkanContext::Get().CheckResult(
            updateResetCommandResult, "TexcacheVulkan update command reset"))
        return;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(State->CommandBuffer, &beginInfo) != VK_SUCCESS)
        return;

    constexpr VkPipelineStageFlags kTextureShaderStages =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    VkImageMemoryBarrier toTransferBarrier{};
    toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.image = textureArray.Image;
    toTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferBarrier.subresourceRange.baseMipLevel = 0;
    toTransferBarrier.subresourceRange.levelCount = 1;
    toTransferBarrier.subresourceRange.baseArrayLayer = layer;
    toTransferBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        State->CommandBuffer,
        kTextureShaderStages,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransferBarrier
    );

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = layer;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent.width = width;
    copyRegion.imageExtent.height = height;
    copyRegion.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(
        State->CommandBuffer,
        textureArray.StagingBuffer,
        textureArray.Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );

    VkImageMemoryBarrier backToGeneralBarrier{};
    backToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    backToGeneralBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    backToGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    backToGeneralBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    backToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    backToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToGeneralBarrier.image = textureArray.Image;
    backToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    backToGeneralBarrier.subresourceRange.baseMipLevel = 0;
    backToGeneralBarrier.subresourceRange.levelCount = 1;
    backToGeneralBarrier.subresourceRange.baseArrayLayer = layer;
    backToGeneralBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
        State->CommandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        kTextureShaderStages,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &backToGeneralBarrier
    );

    if (vkEndCommandBuffer(State->CommandBuffer) != VK_SUCCESS)
        return;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &State->CommandBuffer;
    {
        std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
        const VkResult submitResult = vkQueueSubmit(
            State->Queue, 1, &submitInfo, State->UploadFence);
        if (!VulkanContext::Get().CheckResult(
                submitResult, "TexcacheVulkan texture upload"))
            return;
    }

    (void)VulkanContext::Get().CheckResult(
        vkWaitForFences(State->Device, 1, &State->UploadFence, VK_TRUE, UINT64_MAX),
        "TexcacheVulkan texture upload fence");
}

void TexcacheVulkanLoader::DeleteTexture(TextureHandle handle)
{
    if (State == nullptr)
        return;

    auto it = State->TextureArrays.find(handle);
    if (it == State->TextureArrays.end())
        return;

    TextureArray retired = std::move(it->second);
    State->TextureArrays.erase(it);
    const VkDevice retireDevice = State->Device;
    const u64 retirementValue = State->RetirementValue;
    const VkSemaphore retirementTimeline = retirementValue != 0
        ? State->RetirementTimeline
        : VK_NULL_HANDLE;
    State->RetiredTextures.Retire(
        retirementTimeline,
        retirementValue,
        VK_NULL_HANDLE,
        [retireDevice, retired = std::move(retired)]() mutable {
            if (retireDevice == VK_NULL_HANDLE)
                return;
            const VkDevice device = retireDevice;
            if (retired.Sampler != VK_NULL_HANDLE)
                vkDestroySampler(device, retired.Sampler, nullptr);
            if (retired.ArrayView != VK_NULL_HANDLE)
                vkDestroyImageView(device, retired.ArrayView, nullptr);
            if (retired.StagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(device, retired.StagingBuffer, nullptr);
            if (retired.StagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(device, retired.StagingMemory, nullptr);
            if (retired.Image != VK_NULL_HANDLE)
                vkDestroyImage(device, retired.Image, nullptr);
            if (retired.Memory != VK_NULL_HANDLE)
                vkFreeMemory(device, retired.Memory, nullptr);
        });
}

bool TexcacheVulkanLoader::GetTextureDescriptor(TextureHandle handle, VkDescriptorImageInfo* outImageInfo) const
{
    if (State == nullptr || outImageInfo == nullptr)
        return false;

    auto it = State->TextureArrays.find(handle);
    if (it == State->TextureArrays.end())
        return false;

    const TextureArray& textureArray = it->second;
    if (textureArray.ArrayView == VK_NULL_HANDLE || textureArray.Sampler == VK_NULL_HANDLE)
        return false;

    outImageInfo->sampler = textureArray.Sampler;
    outImageInfo->imageView = textureArray.ArrayView;
    outImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

bool TexcacheVulkanLoader::IsTextureLayerOpaque(TextureHandle handle, u32 layer) const
{
    if (State == nullptr)
        return false;

    auto it = State->TextureArrays.find(handle);
    if (it == State->TextureArrays.end())
        return false;

    const TextureArray& textureArray = it->second;
    if (layer >= textureArray.LayerOpaque.size())
        return false;

    return textureArray.LayerOpaque[layer] != 0u;
}

}
