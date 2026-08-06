#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

// Deterministic structured-2D / Vulkan-3D compositor.
//
// This is the desktop-specific compositor described in the Vulkan full-scene
// rendering work. It is intentionally NOT the pinned Android compositor: that
// one inferred screen ownership and 3D placement from frame history and from
// per-screen pixel statistics, which cannot be correct on a game like Metroid
// Prime Hunters that flips POWCNT1 bit 15 every frame. It composes exactly what
// DX12Renderer3D::ComposeStructuredOutput composes, from exactly the same
// inputs, so both hardware backends agree with the software renderer.
//
// The Vulkan 3D raster path in src/GPU3D_Vulkan.* stays pinned to
// SapphireRhodonite/melonDS-android-lib and is untouched by this file.

#include "MelonPrimeVulkanOutput.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "GPU3D_Vulkan.h"
#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanDispatch.h"
#include "MelonPrimeVulkanCompositorShaderData.h"

namespace MelonPrime
{
// The pinned Android 3D renderer expects this hook to exist. Desktop has no
// renderer-debug UI, so the developer perf log is the only consumer.
bool areRendererDebugToolsEnabled() { return false; }

namespace
{
constexpr u32 kScreenWidth = 256;
constexpr u32 kScreenHeight = 192;

// Packed layout, one screen per buffer and one row per scanline:
//   [0 .. 255] below, [256 .. 511] above, [512 .. 767] control, [768] line meta.
// Interleaving the planes by row keeps each compositor invocation's four reads
// inside one cache line's worth of rows instead of three 192 KiB-apart regions.
constexpr u32 kPackedStride = (kScreenWidth * 3u) + 1u;
constexpr u32 kPackedLineMetaOffset = kScreenWidth * 3u;
constexpr VkDeviceSize kPackedBufferSize =
    static_cast<VkDeviceSize>(kScreenHeight)
    * static_cast<VkDeviceSize>(kPackedStride)
    * sizeof(melonDS::u32);

VkWriteDescriptorSet makeImageDescriptorWrite(
    VkDescriptorSet descriptorSet,
    melonDS::u32 binding,
    const VkDescriptorImageInfo* imageInfo)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = imageInfo;
    return write;
}

VkWriteDescriptorSet makeBufferDescriptorWrite(
    VkDescriptorSet descriptorSet,
    melonDS::u32 binding,
    const VkDescriptorBufferInfo* bufferInfo)
{
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = bufferInfo;
    return write;
}

void packStructuredScreen(
    melonDS::u32* destination,
    const melonDS::u32* below,
    const melonDS::u32* above,
    const melonDS::u32* control,
    const melonDS::u32* lineMeta)
{
    for (u32 line = 0; line < kScreenHeight; line++)
    {
        const std::size_t sourceRow = static_cast<std::size_t>(line) * kScreenWidth;
        melonDS::u32* row = destination + (static_cast<std::size_t>(line) * kPackedStride);
        std::memcpy(row, below + sourceRow, kScreenWidth * sizeof(melonDS::u32));
        std::memcpy(row + kScreenWidth, above + sourceRow, kScreenWidth * sizeof(melonDS::u32));
        std::memcpy(row + (kScreenWidth * 2u), control + sourceRow, kScreenWidth * sizeof(melonDS::u32));
        row[kPackedLineMetaOffset] = lineMeta[line];
    }
}

}

MelonPrimeVulkanOutput::MelonPrimeVulkanOutput() = default;

MelonPrimeVulkanOutput::~MelonPrimeVulkanOutput()
{
    shutdown();
}

bool MelonPrimeVulkanOutput::init()
{
    shutdown();

    if (!melonDS::VulkanContext::Get().Acquire())
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to acquire shared Vulkan context");
        return false;
    }

    contextAcquired = true;
    instance = melonDS::VulkanContext::Get().GetInstance();
    physicalDevice = melonDS::VulkanContext::Get().GetPhysicalDevice();
    device = melonDS::VulkanContext::Get().GetDevice();
    queue = melonDS::VulkanContext::Get().GetQueue();
    queueFamilyIndex = melonDS::VulkanContext::Get().GetQueueFamilyIndex();
    useTimelineSemaphores = melonDS::VulkanContext::Get().SupportsTimelineSemaphores();
    waitSemaphores = useTimelineSemaphores ? melonDS::VulkanContext::Get().GetWaitSemaphores() : nullptr;
    getSemaphoreCounterValue = useTimelineSemaphores ? melonDS::VulkanContext::Get().GetSemaphoreCounterValue() : nullptr;
    resetQueryPool = melonDS::VulkanContext::Get().GetResetQueryPool();
    timestampPeriodNs = melonDS::VulkanContext::Get().GetTimestampPeriod();
    timestampQueriesSupported = melonDS::VulkanContext::Get().SupportsTimestamps();

    if (useTimelineSemaphores && (waitSemaphores == nullptr || getSemaphoreCounterValue == nullptr))
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Warn,
            "MelonPrimeVulkanOutput: timeline semaphore support reported but required functions are unavailable; using fence-based fallback");
        useTimelineSemaphores = false;
        waitSemaphores = nullptr;
        getSemaphoreCounterValue = nullptr;
    }

    if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: shared context is incomplete");
        shutdown();
        return false;
    }

    if (!createSyncObjects() || !createCommandObjects() || !createCompositorResources()
        || !createPackedBuffers())
    {
        shutdown();
        return false;
    }

    initialized = true;
    timelineValue = 0;
    return true;
}

void MelonPrimeVulkanOutput::shutdown()
{
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);

    destroyFrameResources();
    destroyCompositorResources();

    if (timelineSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, timelineSemaphore, nullptr);
        timelineSemaphore = VK_NULL_HANDLE;
    }

    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (contextAcquired)
    {
        melonDS::VulkanContext::Get().Release();
        contextAcquired = false;
    }

    instance = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    queueFamilyIndex = 0;
    waitSemaphores = nullptr;
    getSemaphoreCounterValue = nullptr;
    resetQueryPool = nullptr;
    timestampPeriodNs = 0.0f;
    timestampQueriesSupported = false;
    timelineValue = 0;
    useTimelineSemaphores = false;
    lastComposedFrame = nullptr;
    packedWriteWhileSubmitted = 0;
    generationMismatch = 0;
    fenceRecoveryFailure = 0;
    staleTimelinePresented = 0;
    initialized = false;
}

void MelonPrimeVulkanOutput::releaseFrameReferences()
{
    // Composition never reads another frame's resources, so the only thing a
    // renderer transition has to undo is the cached descriptor binding: the
    // VkImageView it points at belongs to the outgoing 3D renderer.
    lastComposedFrame = nullptr;
    for (auto& [frame, resource] : resources)
    {
        (void)frame;
        resource.descriptorSetReady = false;
        resource.cachedRendererImageView = VK_NULL_HANDLE;
        resource.hasPreparedInputs = false;
    }
}

bool MelonPrimeVulkanOutput::createSyncObjects()
{
    if (!useTimelineSemaphores)
        return true;

    VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
    semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphoreTypeInfo.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = &semaphoreTypeInfo;

    if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &timelineSemaphore) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create timeline semaphore");
        return false;
    }

    return true;
}

bool MelonPrimeVulkanOutput::createCommandObjects()
{
    VkCommandPoolCreateInfo commandPoolCreateInfo{};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create command pool");
        return false;
    }

    return true;
}

bool MelonPrimeVulkanOutput::createTimestampQueryPool(VkQueryPool& queryPool)
{
    if (!timestampQueriesSupported)
        return true;

    VkQueryPoolCreateInfo queryPoolCreateInfo{};
    queryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolCreateInfo.queryCount = 2;

    if (vkCreateQueryPool(device, &queryPoolCreateInfo, nullptr, &queryPool) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Warn,
            "MelonPrimeVulkanOutput: failed to create timestamp query pool");
        queryPool = VK_NULL_HANDLE;
    }

    return true;
}

void MelonPrimeVulkanOutput::destroyTimestampQueryPool(VkQueryPool& queryPool)
{
    if (queryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(device, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }
}

bool MelonPrimeVulkanOutput::createCompositorResources()
{
    // Binding 0 output image, 1 live 3D color target, 2/3 packed structured
    // planes. There is deliberately no history or capture binding.
    std::array<VkDescriptorSetLayoutBinding, 4> compositorBindings{};
    for (u32 i = 0; i < compositorBindings.size(); i++)
    {
        compositorBindings[i].binding = i;
        compositorBindings[i].descriptorCount = 1;
        compositorBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        compositorBindings[i].descriptorType = i < 2u
            ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
            : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
    descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount = static_cast<u32>(compositorBindings.size());
    descriptorSetLayoutCreateInfo.pBindings = compositorBindings.data();

    if (vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &compositorDescriptorSetLayout) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create compositor descriptor set layout");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes{};
    descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorPoolSizes[0].descriptorCount = static_cast<u32>(MELONPRIME_VULKAN_FRAME_QUEUE_SIZE * 2);
    descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorPoolSizes[1].descriptorCount = static_cast<u32>(MELONPRIME_VULKAN_FRAME_QUEUE_SIZE * 2);

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptorPoolCreateInfo.maxSets = static_cast<u32>(MELONPRIME_VULKAN_FRAME_QUEUE_SIZE);
    descriptorPoolCreateInfo.poolSizeCount = static_cast<u32>(descriptorPoolSizes.size());
    descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();

    if (vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &compositorDescriptorPool) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create compositor descriptor pool");
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CompositorPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &compositorDescriptorSetLayout;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &compositorPipelineLayout) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create compositor pipeline layout");
        return false;
    }

    if (melonDS_android_vulkan_compositor_comp_spv_len == 0)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: compositor SPIR-V blob is empty");
        return false;
    }

    std::vector<u32> shaderWords((melonDS_android_vulkan_compositor_comp_spv_len + sizeof(u32) - 1u) / sizeof(u32));
    std::memcpy(shaderWords.data(), melonDS_android_vulkan_compositor_comp_spv, melonDS_android_vulkan_compositor_comp_spv_len);

    VkShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = melonDS_android_vulkan_compositor_comp_spv_len;
    shaderModuleCreateInfo.pCode = shaderWords.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create compositor shader module");
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageCreateInfo{};
    shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCreateInfo.module = shaderModule;
    shaderStageCreateInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.stage = shaderStageCreateInfo;
    computePipelineCreateInfo.layout = compositorPipelineLayout;

    const VkResult pipelineResult = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &compositorPipeline);

    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (pipelineResult != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create compositor pipeline (%d)",
            static_cast<int>(pipelineResult));
        return false;
    }

    return true;
}

bool MelonPrimeVulkanOutput::createPackedBuffers()
{
    const auto createMappedStorageBuffer = [&](
        VkBuffer& buffer, VkDeviceMemory& memory, void*& mappedMemory, const char* label) -> bool {
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = kPackedBufferSize;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Error,
                "MelonPrimeVulkanOutput: failed to create the %s packed buffer", label);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo{};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = findMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (memoryAllocateInfo.memoryTypeIndex == UINT32_MAX
            || vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS
            || vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS
            || vkMapMemory(device, memory, 0, kPackedBufferSize, 0, &mappedMemory) != VK_SUCCESS)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Error,
                "MelonPrimeVulkanOutput: failed to allocate the %s packed buffer", label);
            return false;
        }

        return true;
    };

    if (!createMappedStorageBuffer(topPackedBuffer, topPackedMemory, topPackedMapped, "top")
        || !createMappedStorageBuffer(bottomPackedBuffer, bottomPackedMemory, bottomPackedMapped, "bottom"))
    {
        destroyPackedBuffers();
        return false;
    }

    packedBufferSize = kPackedBufferSize;
    return true;
}

void MelonPrimeVulkanOutput::destroyPackedBuffers()
{
    const auto destroyMappedStorageBuffer = [this](VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped) {
        if (mapped != nullptr)
        {
            vkUnmapMemory(device, memory);
            mapped = nullptr;
        }
        if (buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    };
    destroyMappedStorageBuffer(topPackedBuffer, topPackedMemory, topPackedMapped);
    destroyMappedStorageBuffer(bottomPackedBuffer, bottomPackedMemory, bottomPackedMapped);
    packedBufferSize = 0;
}

void MelonPrimeVulkanOutput::destroyCompositorResources()
{
    destroyPackedBuffers();

    if (compositorPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, compositorPipeline, nullptr);
        compositorPipeline = VK_NULL_HANDLE;
    }

    if (compositorPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, compositorPipelineLayout, nullptr);
        compositorPipelineLayout = VK_NULL_HANDLE;
    }

    if (compositorDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, compositorDescriptorPool, nullptr);
        compositorDescriptorPool = VK_NULL_HANDLE;
    }

    if (compositorDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, compositorDescriptorSetLayout, nullptr);
        compositorDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

u32 MelonPrimeVulkanOutput::findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const
{
    return melonDS::VulkanContext::Get().FindMemoryType(typeBits, properties);
}

bool MelonPrimeVulkanOutput::createFrameResource(VulkanFrame* frame, u32 width, u32 height)
{
    std::scoped_lock commandLock(commandPoolLock);

    FrameResource resource{};
    resource.width = width;
    resource.height = height;

    // Every allocation below is registered in this list so a failure at any
    // step unwinds exactly what was created, in reverse order.
    std::vector<std::function<void()>> rollback;
    const auto unwind = [&rollback]() {
        for (auto step = rollback.rbegin(); step != rollback.rend(); ++step)
            (*step)();
    };

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.extent = {width, height, 1};
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageCreateInfo, nullptr, &resource.image) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: failed to create frame image");
        return false;
    }
    rollback.emplace_back([&] { vkDestroyImage(device, resource.image, nullptr); });

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device, resource.image, &imageRequirements);

    u32 imageMemoryType = findMemoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageMemoryType == UINT32_MAX)
        imageMemoryType = findMemoryType(imageRequirements.memoryTypeBits, 0);
    if (imageMemoryType == UINT32_MAX)
    {
        unwind();
        return false;
    }

    VkMemoryAllocateInfo imageMemoryAllocateInfo{};
    imageMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageMemoryAllocateInfo.allocationSize = imageRequirements.size;
    imageMemoryAllocateInfo.memoryTypeIndex = imageMemoryType;

    if (vkAllocateMemory(device, &imageMemoryAllocateInfo, nullptr, &resource.imageMemory) != VK_SUCCESS
        || vkBindImageMemory(device, resource.image, resource.imageMemory, 0) != VK_SUCCESS)
    {
        if (resource.imageMemory != VK_NULL_HANDLE)
            vkFreeMemory(device, resource.imageMemory, nullptr);
        unwind();
        return false;
    }
    rollback.emplace_back([&] { vkFreeMemory(device, resource.imageMemory, nullptr); });

    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = resource.image;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &imageViewCreateInfo, nullptr, &resource.imageView) != VK_SUCCESS)
    {
        unwind();
        return false;
    }
    rollback.emplace_back([&] { vkDestroyImageView(device, resource.imageView, nullptr); });

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &resource.commandBuffer) != VK_SUCCESS)
    {
        unwind();
        return false;
    }
    rollback.emplace_back([&] { vkFreeCommandBuffers(device, commandPool, 1, &resource.commandBuffer); });

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(device, &fenceCreateInfo, nullptr, &resource.submitFence) != VK_SUCCESS)
    {
        unwind();
        return false;
    }
    rollback.emplace_back([&] { vkDestroyFence(device, resource.submitFence, nullptr); });

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.descriptorPool = compositorDescriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &compositorDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &resource.descriptorSet) != VK_SUCCESS)
    {
        unwind();
        return false;
    }
    rollback.emplace_back([&] { vkFreeDescriptorSets(device, compositorDescriptorPool, 1, &resource.descriptorSet); });

    (void)createTimestampQueryPool(resource.timestampQueryPool);

    const auto insertResult = resources.emplace(frame, resource);
    if (!insertResult.second)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: frame resource unexpectedly already existed during creation");
        destroyTimestampQueryPool(resource.timestampQueryPool);
        unwind();
        return false;
    }

    frame->backend = FrameBackend::VulkanImage;
    frame->renderTimelineValue = 0;
    return true;
}

void MelonPrimeVulkanOutput::destroyFrameResource(VulkanFrame* frame)
{
    std::scoped_lock commandLock(commandPoolLock);

    auto iterator = resources.find(frame);
    if (iterator == resources.end())
        return;

    FrameResource& resource = iterator->second;

    if (resource.submitFence != VK_NULL_HANDLE)
        vkWaitForFences(device, 1, &resource.submitFence, VK_TRUE, UINT64_MAX);

    if (resource.descriptorSet != VK_NULL_HANDLE && compositorDescriptorPool != VK_NULL_HANDLE)
        vkFreeDescriptorSets(device, compositorDescriptorPool, 1, &resource.descriptorSet);

    destroyTimestampQueryPool(resource.timestampQueryPool);

    if (resource.submitFence != VK_NULL_HANDLE)
        vkDestroyFence(device, resource.submitFence, nullptr);

    if (resource.commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device, commandPool, 1, &resource.commandBuffer);

    if (resource.imageView != VK_NULL_HANDLE)
        vkDestroyImageView(device, resource.imageView, nullptr);
    if (resource.image != VK_NULL_HANDLE)
        vkDestroyImage(device, resource.image, nullptr);
    if (resource.imageMemory != VK_NULL_HANDLE)
        vkFreeMemory(device, resource.imageMemory, nullptr);

    if (frame != nullptr)
        frame->renderTimelineValue = 0;

    if (lastComposedFrame == frame)
        lastComposedFrame = nullptr;

    resources.erase(iterator);
}

void MelonPrimeVulkanOutput::destroyFrameResources()
{
    while (!resources.empty())
        destroyFrameResource(resources.begin()->first);
}

bool MelonPrimeVulkanOutput::ensureFrameResources(VulkanFrame* frame, u32 width, u32 height)
{
    if (!initialized || frame == nullptr || width == 0 || height == 0)
        return false;

    auto iterator = resources.find(frame);
    if (iterator != resources.end())
    {
        const FrameResource& resource = iterator->second;
        if (resource.width == width && resource.height == height)
        {
            frame->backend = FrameBackend::VulkanImage;
            return true;
        }

        destroyFrameResource(frame);
    }

    return createFrameResource(frame, width, height);
}

bool MelonPrimeVulkanOutput::waitForResourceSubmission(FrameResource& resource, u64 timeoutNs)
{
    if (resource.submitFence == VK_NULL_HANDLE)
        return true;

    // Fast path: a frame that has already completed costs one status query.
    // A wait only happens when a frame really is reused while still in flight.
    const VkResult status = vkGetFenceStatus(device, resource.submitFence);
    if (status == VK_SUCCESS)
    {
        consumeFrameGpuTiming(resource);
        return true;
    }
    if (status != VK_NOT_READY)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: compositor fence is in an error state (%d)",
            static_cast<int>(status));
        return false;
    }

    const u64 waitStartNs = PerfNowNs();
    if (vkWaitForFences(device, 1, &resource.submitFence, VK_TRUE, timeoutNs) != VK_SUCCESS)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: timed out waiting for a compositor dispatch to finish");
        return false;
    }
    waitCpuWindow.Add(PerfNowNs() - waitStartNs);
    consumeFrameGpuTiming(resource);
    return true;
}

bool MelonPrimeVulkanOutput::acquireFrameForCpuWrite(VulkanFrame* frame, u64 timeoutNs)
{
    if (!initialized || frame == nullptr)
        return false;

    auto iterator = resources.find(frame);
    if (iterator == resources.end())
        return false;

    FrameResource& resource = iterator->second;

    // The frame queue hands frames back for reuse on logical grounds alone --
    // a backlog trim, a stale drop or a discard all return a frame whose
    // dispatch may still be running. Whether the emulation thread may write is
    // decided here, by this resource's fence, and nowhere else.
    //
    // The compositor fence is the only one this needs. The surface presenter
    // reads a frame's output image from its own submission, but it waits on its
    // own fence before each submit and submits on the same queue under the same
    // VulkanContext queue lock, so a later compositor dispatch cannot overtake a
    // present that is still reading. The rare teardown paths (renderer switch,
    // savestate, swapchain rebuild) go through presenter Shutdown() and
    // output shutdown(), which drain the device explicitly.
    if (resource.submissionState == SubmissionState::Submitted)
    {
        if (!waitForResourceSubmission(resource, timeoutNs))
            return false;
        resource.completedGeneration = resource.submittedGeneration;
    }

    resource.submissionState = SubmissionState::Idle;
    resource.cpuWriteOwnership = true;
    resource.hasPreparedInputs = false;
    resource.preparedGeneration = 0;
    return true;
}

bool MelonPrimeVulkanOutput::recoverFrameResourceAfterAbortedSubmission(FrameResource& resource)
{
    // vkResetFences has already run by the time recording or submission can
    // fail, so the fence would stay unsignalled and the next reuse of this
    // frame would wait on it forever. Replace it with a fresh signalled fence.
    if (resource.submitFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, resource.submitFence, nullptr);
        resource.submitFence = VK_NULL_HANDLE;
    }

    VkFenceCreateInfo fenceCreateInfo{};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fenceCreateInfo, nullptr, &resource.submitFence) != VK_SUCCESS)
    {
        fenceRecoveryFailure++;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: could not recreate a compositor fence after an aborted submission");
        return false;
    }

    resource.timestampPending = false;
    resource.submissionState = SubmissionState::Idle;
    resource.cpuWriteOwnership = false;
    resource.hasPreparedInputs = false;
    resource.preparedGeneration = 0;
    return true;
}

bool MelonPrimeVulkanOutput::beginFrameCommand(FrameResource& resource, u64 waitTimeoutNs)
{
    // acquireFrameForCpuWrite() already waited for any dispatch still reading
    // this frame's inputs, so the normal path must not wait a second time.
    // The guard below only catches a caller that skipped the acquire.
    assert(resource.submissionState == SubmissionState::Idle);
    if (resource.submissionState == SubmissionState::Submitted
        && !waitForResourceSubmission(resource, waitTimeoutNs))
    {
        return false;
    }

    if (resource.timestampQueryPool != VK_NULL_HANDLE && resetQueryPool != nullptr)
        resetQueryPool(device, resource.timestampQueryPool, 0, 2);

    if (vkResetFences(device, 1, &resource.submitFence) != VK_SUCCESS)
        return false;

    // From here the fence is unsignalled, so every failure path has to go
    // through recoverFrameResourceAfterAbortedSubmission().
    if (vkResetCommandBuffer(resource.commandBuffer, 0) != VK_SUCCESS)
    {
        (void)recoverFrameResourceAfterAbortedSubmission(resource);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(resource.commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        (void)recoverFrameResourceAfterAbortedSubmission(resource);
        return false;
    }

    resource.submissionState = SubmissionState::Recording;
    return true;
}

bool MelonPrimeVulkanOutput::submitFrameCommand(VulkanFrame* frame, FrameResource& resource, bool signalTimeline)
{
    if (vkEndCommandBuffer(resource.commandBuffer) != VK_SUCCESS)
    {
        (void)recoverFrameResourceAfterAbortedSubmission(resource);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &resource.commandBuffer;

    u64 signalValue = resource.submissionValue;
    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo{};
    const bool shouldSignalTimelineSemaphore =
        signalTimeline && useTimelineSemaphores && timelineSemaphore != VK_NULL_HANDLE;
    if (signalTimeline)
    {
        signalValue = ++timelineValue;
        if (shouldSignalTimelineSemaphore)
        {
            timelineSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            timelineSubmitInfo.signalSemaphoreValueCount = 1;
            timelineSubmitInfo.pSignalSemaphoreValues = &signalValue;

            submitInfo.pNext = &timelineSubmitInfo;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &timelineSemaphore;
        }
    }

    {
        std::scoped_lock queueLock(melonDS::VulkanContext::Get().GetQueueLock());
        if (vkQueueSubmit(queue, 1, &submitInfo, resource.submitFence) != VK_SUCCESS)
        {
            (void)recoverFrameResourceAfterAbortedSubmission(resource);
            return false;
        }
    }

    // The GPU now owns this frame's inputs until its fence signals.
    resource.submissionState = SubmissionState::Submitted;
    resource.cpuWriteOwnership = false;

    if (frame != nullptr)
    {
        frame->backend = FrameBackend::VulkanImage;
        if (signalTimeline)
            frame->renderTimelineValue = signalValue;
    }

    if (signalTimeline)
    {
        resource.submissionValue = signalValue;
        if (resource.timestampQueryPool != VK_NULL_HANDLE)
            resource.timestampPending = true;
    }

    return true;
}

bool MelonPrimeVulkanOutput::updateCompositorPackedBuffers(
    FrameResource& resource,
    const StructuredCompositionFrame& structured)
{
    // Writing while the GPU still owns the planes is the defect this whole
    // state machine exists to prevent, so it is counted, not tolerated.
    assert(resource.cpuWriteOwnership);
    assert(resource.submissionState == SubmissionState::Idle);
    if (!resource.cpuWriteOwnership || resource.submissionState != SubmissionState::Idle)
    {
        packedWriteWhileSubmitted++;
        return false;
    }

    if (topPackedMapped == nullptr || bottomPackedMapped == nullptr || packedBufferSize == 0)
        return false;

    // The mapped memory is host-coherent, so this single pass off the producer's
    // arrays is the whole upload: no intermediate snapshot, no second copy.
    packStructuredScreen(
        static_cast<melonDS::u32*>(topPackedMapped),
        structured.Plane[0][0],
        structured.Plane[0][1],
        structured.Plane[0][2],
        structured.LineMeta[0]);
    packStructuredScreen(
        static_cast<melonDS::u32*>(bottomPackedMapped),
        structured.Plane[1][0],
        structured.Plane[1][1],
        structured.Plane[1][2],
        structured.LineMeta[1]);
    return true;
}

bool MelonPrimeVulkanOutput::prepareFrameForPresentation(
    VulkanFrame* frame,
    const StructuredCompositionFrame& structured)
{
    if (!initialized
        || frame == nullptr
        || !structured.IsComplete()
        || structured.Generation == 0)
    {
        return false;
    }

    // Ownership first, then the write. The structured planes live in one shared
    // buffer pair that the compute dispatch reads directly, so the emulation
    // thread may only touch them once the dispatch that was reading them has
    // finished. Doing this the other way round let a new frame's metadata land
    // underneath a running dispatch, which then sampled one frame's control
    // words next to another frame's 2D planes -- the background appearing in
    // front of the UI on alternating frames.
    if (!acquireFrameForCpuWrite(frame))
        return false;

    auto iterator = resources.find(frame);
    if (iterator == resources.end())
        return false;

    FrameResource& resource = iterator->second;
    assert(resource.submissionState == SubmissionState::Idle);
    assert(resource.cpuWriteOwnership);
    assert(resource.preparedGeneration == 0);

    const u64 packedUploadStartNs = PerfNowNs();
    if (!updateCompositorPackedBuffers(resource, structured))
    {
        resource.cpuWriteOwnership = false;
        return false;
    }
    packedUploadCpuWindow.Add(PerfNowNs() - packedUploadStartNs);

    resource.preparedGeneration = structured.Generation;
    resource.hasPreparedInputs = true;
    lastComposedFrame = frame;
    return true;
}

bool MelonPrimeVulkanOutput::buildCompositionInputs(
    const VulkanFrame* frame,
    const melonDS::VulkanRenderer3D& renderer3D,
    int scale,
    bool has3D,
    u64 generation,
    VulkanCompositionInputs& outInputs) const
{
    outInputs = {};

    if (!initialized || frame == nullptr || scale < 1 || generation == 0)
        return false;

    auto iterator = resources.find(const_cast<VulkanFrame*>(frame));
    if (iterator == resources.end())
        return false;

    const FrameResource& resource = iterator->second;
    if (!resource.hasPreparedInputs || !renderer3D.HasColorTarget())
        return false;

    // These inputs must describe the same emulated frame whose planes are
    // currently in the shared buffers, and the emulation thread must still own
    // that write. Anything else would compose two frames together.
    if (!resource.cpuWriteOwnership
        || resource.preparedGeneration == 0
        || resource.preparedGeneration != generation)
    {
        return false;
    }

    outInputs.generation = generation;
    outInputs.rendererSubmissionSerial = renderer3D.GetRenderSubmissionSerial();

    // The live color target, never a snapshot of an earlier frame. The Vulkan
    // 3D renderer finished this frame's render before the frontend drew, so
    // this image and the structured planes describe the same emulated frame.
    outInputs.sourceImage = renderer3D.GetColorTargetImage();
    outInputs.sourceImageView = renderer3D.GetColorTargetImageView();
    outInputs.rendererWidth = renderer3D.GetColorTargetWidth();
    outInputs.rendererHeight = renderer3D.GetColorTargetHeight();
    outInputs.topPackedBuffer = topPackedBuffer;
    outInputs.bottomPackedBuffer = bottomPackedBuffer;
    outInputs.packedBufferSize = packedBufferSize;
    outInputs.packedStride = kPackedStride;
    outInputs.scale = static_cast<u32>(scale);
    // An uninitialized color target holds whatever the allocator handed out, so
    // treat it exactly like an aborted 3D frame instead of sampling garbage.
    outInputs.has3D = has3D && renderer3D.IsColorTargetInitialized();

    return outInputs.sourceImage != VK_NULL_HANDLE
        && outInputs.sourceImageView != VK_NULL_HANDLE
        && outInputs.topPackedBuffer != VK_NULL_HANDLE
        && outInputs.bottomPackedBuffer != VK_NULL_HANDLE;
}

bool MelonPrimeVulkanOutput::composeAndSubmitFrame(VulkanFrame* frame, const VulkanCompositionInputs& inputs)
{
    if (!initialized || frame == nullptr)
        return false;

    auto iterator = resources.find(frame);
    if (iterator == resources.end())
        return false;

    FrameResource& resource = iterator->second;

    // Never compose a frame whose inputs and submission disagree: that is
    // exactly how one frame's 2D ends up over another frame's 3D. Drop the
    // frame instead of guessing which half is current.
    if (inputs.generation == 0
        || inputs.generation != resource.preparedGeneration
        || !resource.cpuWriteOwnership)
    {
        generationMismatch++;
        assert(false && "compositor inputs do not match the prepared frame");
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrimeVulkanOutput: generation mismatch (inputs=%llu prepared=%llu owned=%d); dropping the frame",
            static_cast<unsigned long long>(inputs.generation),
            static_cast<unsigned long long>(resource.preparedGeneration),
            resource.cpuWriteOwnership ? 1 : 0);
        return false;
    }

    const u64 composeStartNs = PerfNowNs();
    const bool composed = dispatchCompositor(frame, resource, inputs);
    composeCpuWindow.Add(PerfNowNs() - composeStartNs);
    return composed;
}

bool MelonPrimeVulkanOutput::dispatchCompositor(
    VulkanFrame* frame,
    FrameResource& resource,
    const VulkanCompositionInputs& inputs)
{
    std::scoped_lock commandLock(commandPoolLock);

    if (compositorPipeline == VK_NULL_HANDLE || !beginFrameCommand(resource))
        return false;

    if (resource.timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(resource.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, resource.timestampQueryPool, 0);

    // Output image: whatever read it last (the presenter, or nothing on the
    // first use) must finish before this dispatch writes it.
    VkImageMemoryBarrier outputToGeneralBarrier{};
    outputToGeneralBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputToGeneralBarrier.srcAccessMask = resource.hasContent
        ? (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)
        : 0;
    outputToGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputToGeneralBarrier.oldLayout = resource.hasContent
        ? VK_IMAGE_LAYOUT_GENERAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    outputToGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputToGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputToGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputToGeneralBarrier.image = resource.image;
    outputToGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputToGeneralBarrier.subresourceRange.levelCount = 1;
    outputToGeneralBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        resource.commandBuffer,
        resource.hasContent ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &outputToGeneralBarrier);

    // 3D color target: the Vulkan 3D renderer wrote it as a color attachment or
    // via its compute raster path, both of which leave it in GENERAL.
    VkImageMemoryBarrier renderer3dReadableBarrier{};
    renderer3dReadableBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    renderer3dReadableBarrier.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT;
    renderer3dReadableBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    renderer3dReadableBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    renderer3dReadableBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    renderer3dReadableBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    renderer3dReadableBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    renderer3dReadableBarrier.image = inputs.sourceImage;
    renderer3dReadableBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    renderer3dReadableBarrier.subresourceRange.levelCount = 1;
    renderer3dReadableBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        resource.commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &renderer3dReadableBarrier);

    // Packed planes: host writes from prepareFrameForPresentation.
    std::array<VkBufferMemoryBarrier, 2> packedBarriers{};
    for (std::size_t i = 0; i < packedBarriers.size(); i++)
    {
        packedBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        packedBarriers[i].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        packedBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        packedBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        packedBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        packedBarriers[i].buffer = i == 0 ? topPackedBuffer : bottomPackedBuffer;
        packedBarriers[i].offset = 0;
        packedBarriers[i].size = packedBufferSize;
    }

    vkCmdPipelineBarrier(
        resource.commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr,
        static_cast<u32>(packedBarriers.size()), packedBarriers.data(),
        0, nullptr);

    VkDescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageView = resource.imageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo input3dImageInfo{};
    input3dImageInfo.imageView = inputs.sourceImageView;
    input3dImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo topPackedBufferInfo{};
    topPackedBufferInfo.buffer = topPackedBuffer;
    topPackedBufferInfo.range = packedBufferSize;

    VkDescriptorBufferInfo bottomPackedBufferInfo{};
    bottomPackedBufferInfo.buffer = bottomPackedBuffer;
    bottomPackedBufferInfo.range = packedBufferSize;

    // Only the 3D image view can change between frames (renderer restart or a
    // resolution change), so the descriptor set is otherwise written once.
    if (!resource.descriptorSetReady || resource.cachedRendererImageView != inputs.sourceImageView)
    {
        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};
        descriptorWrites[0] = makeImageDescriptorWrite(resource.descriptorSet, 0, &outputImageInfo);
        descriptorWrites[1] = makeImageDescriptorWrite(resource.descriptorSet, 1, &input3dImageInfo);
        descriptorWrites[2] = makeBufferDescriptorWrite(resource.descriptorSet, 2, &topPackedBufferInfo);
        descriptorWrites[3] = makeBufferDescriptorWrite(resource.descriptorSet, 3, &bottomPackedBufferInfo);

        vkUpdateDescriptorSets(device, static_cast<u32>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        resource.descriptorSetReady = true;
        resource.cachedRendererImageView = inputs.sourceImageView;
    }

    vkCmdBindPipeline(resource.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compositorPipeline);
    vkCmdBindDescriptorSets(
        resource.commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        compositorPipelineLayout,
        0, 1, &resource.descriptorSet, 0, nullptr);

    CompositorPushConstants pushConstants{};
    pushConstants.outputWidth = resource.width;
    pushConstants.outputHeight = resource.height;
    pushConstants.scale = inputs.scale;
    pushConstants.rendererWidth = inputs.rendererWidth;
    pushConstants.rendererHeight = inputs.rendererHeight;
    pushConstants.packedStride = inputs.packedStride;
    pushConstants.has3D = inputs.has3D ? 1u : 0u;

    vkCmdPushConstants(
        resource.commandBuffer,
        compositorPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    vkCmdDispatch(resource.commandBuffer, (resource.width + 7u) / 8u, (resource.height + 7u) / 8u, 1);

    if (resource.timestampQueryPool != VK_NULL_HANDLE)
        vkCmdWriteTimestamp(resource.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, resource.timestampQueryPool, 1);

    VkImageMemoryBarrier outputReadableBarrier = outputToGeneralBarrier;
    outputReadableBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    outputReadableBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    outputReadableBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputReadableBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    vkCmdPipelineBarrier(
        resource.commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &outputReadableBarrier);

    if (!submitFrameCommand(frame, resource, true))
        return false;

    resource.hasContent = true;
    resource.submittedGeneration = inputs.generation;
    frame->emulatedGeneration = inputs.generation;
    lastComposedFrame = frame;
    return true;
}

bool MelonPrimeVulkanOutput::waitForFrame(const VulkanFrame* frame, u64 timeoutNs)
{
    if (!initialized || frame == nullptr || frame->backend != FrameBackend::VulkanImage)
    {
        waitFailureInvalidFrame++;
        return false;
    }

    if (frame->renderTimelineValue == 0)
    {
        waitFailureTimelineZero++;
        return false;
    }

    // A frame is only presentable once a composition for a real emulated frame
    // has been submitted into it. getRenderFrame() clears this on acquisition,
    // so a slot that was recycled without being recomposed cannot slip through.
    if (frame->emulatedGeneration == 0)
    {
        staleTimelinePresented++;
        return false;
    }

    const u64 waitStartNs = PerfNowNs();
    bool waitSucceeded = false;

    if (useTimelineSemaphores && waitSemaphores != nullptr && timelineSemaphore != VK_NULL_HANDLE)
    {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timelineSemaphore;
        waitInfo.pValues = &frame->renderTimelineValue;
        waitSucceeded = waitSemaphores(device, &waitInfo, timeoutNs) == VK_SUCCESS;
    }
    else
    {
        auto iterator = resources.find(const_cast<VulkanFrame*>(frame));
        if (iterator == resources.end())
        {
            waitFailureResourceMissing++;
            return false;
        }
        waitSucceeded = vkWaitForFences(device, 1, &iterator->second.submitFence, VK_TRUE, timeoutNs) == VK_SUCCESS;
    }

    if (!waitSucceeded)
    {
        if (timeoutNs == UINT64_MAX)
            waitFailureInfinite++;
        else
            waitFailureFiniteTimeout++;
        return false;
    }

    waitCpuWindow.Add(PerfNowNs() - waitStartNs);

    auto iterator = resources.find(const_cast<VulkanFrame*>(frame));
    if (iterator != resources.end())
        consumeFrameGpuTiming(iterator->second);

    logPerformanceIfNeeded();
    return true;
}

void MelonPrimeVulkanOutput::consumeFrameGpuTiming(FrameResource& resource)
{
    if (!resource.timestampPending
        || resource.timestampQueryPool == VK_NULL_HANDLE
        || timestampPeriodNs <= 0.0f)
        return;

    u64 timestamps[2]{};
    const VkResult queryResult = vkGetQueryPoolResults(
        device,
        resource.timestampQueryPool,
        0, 2,
        sizeof(timestamps),
        timestamps,
        sizeof(u64),
        VK_QUERY_RESULT_64_BIT);
    if (queryResult == VK_SUCCESS && timestamps[1] >= timestamps[0])
    {
        const u64 gpuTimeNs = static_cast<u64>(
            static_cast<double>(timestamps[1] - timestamps[0]) * static_cast<double>(timestampPeriodNs));
        compositorGpuWindow.Add(gpuTimeNs);
    }

    resource.timestampPending = false;
}

void MelonPrimeVulkanOutput::logPerformanceIfNeeded()
{
    if (!areRendererDebugToolsEnabled())
        return;

    if (!composeCpuWindow.Ready())
        return;

    const PerfSampleWindow<120>::Summary composeSummary = composeCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary packedSummary = packedUploadCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary waitSummary = waitCpuWindow.SummarizeAndReset();
    const PerfSampleWindow<120>::Summary gpuSummary = compositorGpuWindow.SummarizeAndReset();

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Warn,
        "VulkanPerf[Output]: compose cpu avg=%.3fms p95=%.3fms max=%.3fms packed avg=%.3fms p95=%.3fms max=%.3fms wait avg=%.3fms p95=%.3fms max=%.3fms gpu avg=%.3fms p95=%.3fms max=%.3fms ownership(packedWriteWhileSubmitted=%llu generationMismatch=%llu fenceRecoveryFailure=%llu staleTimelinePresented=%llu) waitFail(invalid=%llu timelineZero=%llu resourceMissing=%llu finiteTimeout=%llu infinite=%llu)",
        PerfNsToMs(composeSummary.MeanNs),
        PerfNsToMs(composeSummary.P95Ns),
        PerfNsToMs(composeSummary.MaxNs),
        PerfNsToMs(packedSummary.MeanNs),
        PerfNsToMs(packedSummary.P95Ns),
        PerfNsToMs(packedSummary.MaxNs),
        PerfNsToMs(waitSummary.MeanNs),
        PerfNsToMs(waitSummary.P95Ns),
        PerfNsToMs(waitSummary.MaxNs),
        PerfNsToMs(gpuSummary.MeanNs),
        PerfNsToMs(gpuSummary.P95Ns),
        PerfNsToMs(gpuSummary.MaxNs),
        static_cast<unsigned long long>(packedWriteWhileSubmitted),
        static_cast<unsigned long long>(generationMismatch),
        static_cast<unsigned long long>(fenceRecoveryFailure),
        static_cast<unsigned long long>(staleTimelinePresented),
        static_cast<unsigned long long>(waitFailureInvalidFrame),
        static_cast<unsigned long long>(waitFailureTimelineZero),
        static_cast<unsigned long long>(waitFailureResourceMissing),
        static_cast<unsigned long long>(waitFailureFiniteTimeout),
        static_cast<unsigned long long>(waitFailureInfinite));
    waitFailureInvalidFrame = 0;
    waitFailureTimelineZero = 0;
    waitFailureResourceMissing = 0;
    waitFailureFiniteTimeout = 0;
    waitFailureInfinite = 0;
}

VkImage MelonPrimeVulkanOutput::getFrameImage(const VulkanFrame* frame) const
{
    if (frame == nullptr)
        return VK_NULL_HANDLE;

    auto iterator = resources.find(const_cast<VulkanFrame*>(frame));
    return iterator == resources.end() ? VK_NULL_HANDLE : iterator->second.image;
}

VkImageView MelonPrimeVulkanOutput::getFrameImageView(const VulkanFrame* frame) const
{
    if (frame == nullptr)
        return VK_NULL_HANDLE;

    auto iterator = resources.find(const_cast<VulkanFrame*>(frame));
    return iterator == resources.end() ? VK_NULL_HANDLE : iterator->second.imageView;
}

}
#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
