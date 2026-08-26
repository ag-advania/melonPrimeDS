/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanCaptureBridge.h"

#include <algorithm>
#include <cstring>

#include "GPU.h"
#include "VulkanDevice.h"

namespace melonDS
{

namespace
{

// One DS capture block. The emulated hardware addresses capture in these
// units, so both the copy and the destination layout are expressed in them.
constexpr VkDeviceSize kCaptureBlockBytes = CapturePhysicalBlockBytes;
constexpr VkDeviceSize kCaptureBankBytes =
    kCaptureBlockBytes * CapturePhysicalBlocksPerBank;
// A capture read waits on its own submission, not on the queue. One second is
// a hang, not a slow frame.
constexpr u64 kReadbackFenceTimeoutNs = 1000000000ull;

void BufferBarrier(
    const Vk::DeviceDispatch& fns,
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkPipelineStageFlags srcStage,
    VkAccessFlags srcAccess,
    VkPipelineStageFlags dstStage,
    VkAccessFlags dstAccess)
{
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    fns.CmdPipelineBarrier(
        cmd, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

} // namespace

bool VulkanCaptureBridge::CreateReadback(
    const VulkanDevice& device, VkDeviceSize bytes)
{
    return Readback.Create(
        device, bytes, "MelonPrime Vulkan native capture readback");
}

bool VulkanCaptureBridge::CreateSidecar(
    const VulkanDevice& device, VkDeviceSize bytes)
{
    return Sidecar.Create(
        device,
        bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0,
        "MelonPrime Vulkan high-resolution capture sidecar");
}

bool VulkanCaptureBridge::ReadBlocks(
    const VulkanDevice& device,
    Vk::FrameRing& frames,
    VkBuffer source,
    VkDeviceSize sourceBase,
    u32 bank,
    u32 start,
    u32 len,
    u8* destination)
{
    if (!Readback.IsValid() || source == VK_NULL_HANDLE || !destination)
        return false;

    const u32 blockCount = len == 0u ? 1u : std::min<u32>(len, 3u);
    const VkDeviceSize totalBytes =
        static_cast<VkDeviceSize>(blockCount) * kCaptureBlockBytes;

    const Vk::DeviceDispatch& fns = device.Fns();
    Vk::FrameContext* frame = frames.BeginFrame();
    if (!frame)
        return false;

    VkCommandBuffer cmd = frame->CommandBuffer;

    // The compositor writes the source from a compute shader, so the copy has
    // to be ordered after those writes, then the source handed back for the
    // next frame's writes.
    BufferBarrier(fns, cmd, source,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    for (u32 i = 0; i < blockCount; ++i)
    {
        VkBufferCopy copy{};
        copy.srcOffset = sourceBase
            + static_cast<VkDeviceSize>(bank) * kCaptureBankBytes
            + static_cast<VkDeviceSize>(
                  (start + i) & (CapturePhysicalBlocksPerBank - 1u))
                * kCaptureBlockBytes;
        copy.dstOffset = static_cast<VkDeviceSize>(i) * kCaptureBlockBytes;
        copy.size = kCaptureBlockBytes;
        fns.CmdCopyBuffer(cmd, source, Readback.GetHandle(), 1, &copy);
    }

    BufferBarrier(fns, cmd, Readback.GetHandle(),
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    BufferBarrier(fns, cmd, source,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    if (!frames.SubmitFrame(device.GetMainQueue()))
        return false;
    const VkResult waitResult = fns.WaitForFences(
        device.GetHandle(), 1, &frame->InFlightFence, VK_TRUE,
        kReadbackFenceTimeoutNs);
    if (waitResult != VK_SUCCESS)
        return false;
    if (!Readback.Invalidate(0, totalBytes))
        return false;
    const u8* readbackSource = Readback.GetData();
    if (!readbackSource)
        return false;

    for (u32 i = 0; i < blockCount; ++i)
    {
        // The copy packed the blocks contiguously; the destination wants them
        // at their DS block positions, which wrap within the bank.
        const u32 block = (start + i) & (CapturePhysicalBlocksPerBank - 1u);
        std::memcpy(
            destination + static_cast<std::size_t>(block) * kCaptureBlockBytes,
            readbackSource + static_cast<std::size_t>(i) * kCaptureBlockBytes,
            static_cast<std::size_t>(kCaptureBlockBytes));
    }
    return true;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
