#pragma once

// MELONPRIME_VULKAN_R24_COMPLETE_V1
// Desktop-only synchronization/lifetime adapter for the R24 Vulkan phase.
// This file does not alter Sapphire renderer algorithms or descriptor ABI.

#include <deque>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <volk.h>

#include "types.h"

namespace melonDS
{

class VulkanRetireQueue
{
public:
    using DestroyFunction = std::function<void()>;

    void Retire(
        VkSemaphore timelineSemaphore,
        u64 timelineValue,
        VkFence fence,
        DestroyFunction destroy)
    {
        if (!destroy)
            return;

        std::lock_guard<std::mutex> lock(Mutex);
        Entries.push_back(Entry{
            timelineSemaphore,
            timelineValue,
            fence,
            std::move(destroy),
        });
    }

    void RetireReady(DestroyFunction destroy)
    {
        Retire(VK_NULL_HANDLE, 0, VK_NULL_HANDLE, std::move(destroy));
    }

    void Collect(
        VkDevice device,
        PFN_vkGetSemaphoreCounterValueKHR getSemaphoreCounterValue,
        bool deviceLost = false)
    {
        std::vector<DestroyFunction> ready;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            auto entry = Entries.begin();
            while (entry != Entries.end())
            {
                bool complete = deviceLost;
                if (!complete
                    && entry->TimelineSemaphore != VK_NULL_HANDLE
                    && entry->TimelineValue != 0
                    && device != VK_NULL_HANDLE
                    && getSemaphoreCounterValue != nullptr)
                {
                    u64 completedValue = 0;
                    const VkResult result = getSemaphoreCounterValue(
                        device,
                        entry->TimelineSemaphore,
                        &completedValue);
                    complete = result == VK_SUCCESS
                        ? completedValue >= entry->TimelineValue
                        : result == VK_ERROR_DEVICE_LOST;
                }
                else if (!complete
                    && entry->Fence != VK_NULL_HANDLE
                    && device != VK_NULL_HANDLE)
                {
                    const VkResult result = vkGetFenceStatus(device, entry->Fence);
                    complete = result == VK_SUCCESS
                        || result == VK_ERROR_DEVICE_LOST;
                }
                else if (!complete
                    && entry->TimelineSemaphore == VK_NULL_HANDLE
                    && entry->Fence == VK_NULL_HANDLE)
                {
                    complete = true;
                }

                if (!complete)
                {
                    ++entry;
                    continue;
                }

                ready.emplace_back(std::move(entry->Destroy));
                entry = Entries.erase(entry);
            }
        }

        for (DestroyFunction& destroy : ready)
            destroy();
    }

    void Drain(
        VkDevice device,
        PFN_vkWaitSemaphoresKHR waitSemaphores,
        bool deviceLost = false)
    {
        std::deque<Entry> entries;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            entries.swap(Entries);
        }

        for (Entry& entry : entries)
        {
            bool complete = deviceLost
                || entry.TimelineSemaphore == VK_NULL_HANDLE
                    && entry.Fence == VK_NULL_HANDLE;

            if (!complete && device != VK_NULL_HANDLE)
            {
                VkResult waitResult = VK_NOT_READY;
                if (entry.TimelineSemaphore != VK_NULL_HANDLE
                    && entry.TimelineValue != 0
                    && waitSemaphores != nullptr)
                {
                    VkSemaphoreWaitInfo waitInfo{};
                    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                    waitInfo.semaphoreCount = 1;
                    waitInfo.pSemaphores = &entry.TimelineSemaphore;
                    waitInfo.pValues = &entry.TimelineValue;
                    waitResult = waitSemaphores(
                        device,
                        &waitInfo,
                        UINT64_MAX);
                }
                else if (entry.Fence != VK_NULL_HANDLE)
                {
                    waitResult = vkWaitForFences(
                        device,
                        1,
                        &entry.Fence,
                        VK_TRUE,
                        UINT64_MAX);
                }

                complete = waitResult == VK_SUCCESS
                    || waitResult == VK_ERROR_DEVICE_LOST;
            }

            // If a completion primitive unexpectedly failed, leaking the
            // individual handles until vkDestroyDevice is safer than destroying
            // a resource that may still be in flight.
            if (complete && entry.Destroy)
                entry.Destroy();
        }
    }

    [[nodiscard]] bool Empty() const
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return Entries.empty();
    }

private:
    struct Entry
    {
        VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
        u64 TimelineValue = 0;
        VkFence Fence = VK_NULL_HANDLE;
        DestroyFunction Destroy;
    };

    mutable std::mutex Mutex;
    std::deque<Entry> Entries;
};

namespace VulkanR24Barrier
{

inline void HostWriteToShaderRead(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize size,
    VkPipelineStageFlags destinationStages =
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
        | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
{
    if (commandBuffer == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || size == 0)
        return;

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = 0;
    if ((destinationStages
            & (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)) != 0)
    {
        barrier.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ((destinationStages & VK_PIPELINE_STAGE_VERTEX_INPUT_BIT) != 0)
        barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        destinationStages,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
}

inline void HostWriteToTransferRead(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize size)
{
    if (commandBuffer == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || size == 0)
        return;

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
}

inline void TransferWriteToShaderRead(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_GENERAL)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

inline void ColorAttachmentToSampled(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_GENERAL)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

inline void StorageWriteToSampled(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

inline void ShaderWriteToIndirectRead(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize size)
{
    if (commandBuffer == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || size == 0)
        return;

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
}

inline void CompositionWriteToPresenterRead(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_TRANSFER_BIT
            | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

inline void PresentToRender(
    VkCommandBuffer commandBuffer,
    VkImage image,
    bool initialized,
    u32 graphicsQueueFamily,
    u32 presentQueueFamily)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    const bool separateFamilies =
        graphicsQueueFamily != presentQueueFamily
        && graphicsQueueFamily != UINT32_MAX
        && presentQueueFamily != UINT32_MAX;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
        | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = initialized
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex =
        separateFamilies ? presentQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex =
        separateFamilies ? graphicsQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        initialized
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

inline void RenderToPresent(
    VkCommandBuffer commandBuffer,
    VkImage image,
    u32 graphicsQueueFamily,
    u32 presentQueueFamily)
{
    if (commandBuffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return;

    const bool separateFamilies =
        graphicsQueueFamily != presentQueueFamily
        && graphicsQueueFamily != UINT32_MAX
        && presentQueueFamily != UINT32_MAX;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex =
        separateFamilies ? graphicsQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex =
        separateFamilies ? presentQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

} // namespace VulkanR24Barrier
} // namespace melonDS
