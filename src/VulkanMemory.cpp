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

#include "VulkanMemory.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstring>
#include <utility>

namespace melonDS::Vk
{

namespace
{

// Frees an allocation, unmapping first when it is still mapped. vkFreeMemory
// implicitly unmaps, but doing it explicitly keeps the tracked pointer honest
// and makes a use-after-free crash land on a null pointer instead of on memory
// the driver has recycled.
void FreeAllocation(
    const VulkanDevice* owner,
    const DeviceDispatch& fns,
    VkDevice device,
    DeviceAllocation& allocation) noexcept
{
    if (allocation.Memory == VK_NULL_HANDLE)
        return;

    if (allocation.Mapped)
    {
        fns.UnmapMemory(device, allocation.Memory);
        allocation.Mapped = nullptr;
    }

    fns.FreeMemory(device, allocation.Memory, nullptr);
    if (owner)
        owner->ReleaseMemoryAllocation(allocation.TypeIndex, allocation.Size);
    allocation = DeviceAllocation{};
}

// Shared allocation path for buffers and images. `requirements` comes from
// vkGetBufferMemoryRequirements / vkGetImageMemoryRequirements, whose
// memoryTypeBits mask is the authoritative list of types the resource may be
// bound to.
bool AllocateFor(
    const VulkanDevice& device,
    const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const char* debugName,
    DeviceAllocation& out)
{
    const VkPhysicalDeviceMemoryProperties& memProps = device.GetMemoryProperties();

    const u32 typeIndex = FindMemoryType(memProps, requirements.memoryTypeBits, required, preferred);
    if (typeIndex == InvalidMemoryType)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: no memory type satisfies properties 0x%08X among the %u types "
            "allowed by the resource (mask 0x%08X)\n",
            debugName ? debugName : "<unnamed allocation>",
            static_cast<unsigned>(required),
            memProps.memoryTypeCount,
            static_cast<unsigned>(requirements.memoryTypeBits));
        return false;
    }

    if (!device.ReserveMemoryAllocation(typeIndex, requirements.size, debugName))
        return false;

    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = typeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult res = device.Fns().AllocateMemory(device.GetHandle(), &info, nullptr, &memory);
    if (res != VK_SUCCESS)
    {
        device.ReleaseMemoryAllocation(typeIndex, requirements.size);
        const u32 heapIndex = memProps.memoryTypes[typeIndex].heapIndex;
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkAllocateMemory failed for %.1f MiB from memory type %u "
            "(heap %u, %.1f MiB total): %s\n",
            debugName ? debugName : "<unnamed allocation>",
            static_cast<double>(requirements.size) / (1024.0 * 1024.0),
            typeIndex,
            heapIndex,
            static_cast<double>(memProps.memoryHeaps[heapIndex].size) / (1024.0 * 1024.0),
            FormatResult(res).c_str());
        return false;
    }

    out.Memory = memory;
    out.Size = requirements.size;
    out.TypeIndex = typeIndex;
    out.Properties = memProps.memoryTypes[typeIndex].propertyFlags;
    out.Mapped = nullptr;
    return true;
}

} // namespace


u32 FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    u32 memoryTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred) noexcept
{
    // Pass 1: exact match on required + preferred.
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((memoryTypeBits & (1u << i)) == 0)
            continue;

        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        if ((flags & (required | preferred)) == (required | preferred))
            return i;
    }

    // Pass 2: required only. The preferred flags are genuinely optional, so
    // dropping them is a documented degradation, not a silent fallback: the
    // caller keeps working with, say, uncached host-visible memory.
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((memoryTypeBits & (1u << i)) == 0)
            continue;

        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        if ((flags & required) == required)
            return i;
    }

    return InvalidMemoryType;
}


void AlignMappedRange(
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize,
    VkDeviceSize& offset,
    VkDeviceSize& size) noexcept
{
    if (nonCoherentAtomSize <= 1)
        return;

    if (size == VK_WHOLE_SIZE)
    {
        offset = AlignDown(offset, nonCoherentAtomSize);
        return;
    }

    const VkDeviceSize end = offset + size;
    offset = AlignDown(offset, nonCoherentAtomSize);

    VkDeviceSize alignedEnd = AlignUp(end, nonCoherentAtomSize);
    if (alignedEnd > allocationSize)
    {
        // The spec explicitly permits the final range to run to the end of the
        // allocation even when the allocation size is not an atom multiple.
        alignedEnd = allocationSize;
    }

    size = (alignedEnd > offset) ? (alignedEnd - offset) : 0;
}


// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

Buffer::~Buffer()
{
    Destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
{
    MoveFrom(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

void Buffer::MoveFrom(Buffer& other) noexcept
{
    Device = other.Device;
    Handle = other.Handle;
    View = other.View;
    Size = other.Size;
    Allocation = other.Allocation;

    other.Device = nullptr;
    other.Handle = VK_NULL_HANDLE;
    other.View = VK_NULL_HANDLE;
    other.Size = 0;
    other.Allocation = DeviceAllocation{};
}

bool Buffer::Create(
    const VulkanDevice& device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredProperties,
    VkMemoryPropertyFlags preferredProperties,
    const char* debugName)
{
    Destroy();

    if (!device.IsValid() || size == 0)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: refusing to create a zero-sized buffer or one on an invalid device\n",
            debugName ? debugName : "<unnamed buffer>");
        return false;
    }

    Device = &device;
    const DeviceDispatch& fns = device.Fns();

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    // EXCLUSIVE, not CONCURRENT: the backend uses one queue family for all
    // rasterizer work. CONCURRENT would disable driver compression and
    // fast-clear paths on several implementations in exchange for a sharing
    // guarantee nothing here needs; the split-family case uses explicit
    // ownership transfer barriers instead.
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = fns.CreateBuffer(device.GetHandle(), &info, nullptr, &Handle);
    if (res != VK_SUCCESS)
    {
        Handle = VK_NULL_HANDLE;
        Device = nullptr;
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkCreateBuffer failed for %.1f MiB (usage 0x%08X): %s\n",
            debugName ? debugName : "<unnamed buffer>",
            static_cast<double>(size) / (1024.0 * 1024.0),
            static_cast<unsigned>(usage),
            FormatResult(res).c_str());
        return false;
    }

    VkMemoryRequirements requirements{};
    fns.GetBufferMemoryRequirements(device.GetHandle(), Handle, &requirements);

    if (!AllocateFor(device, requirements, requiredProperties, preferredProperties, debugName, Allocation))
    {
        Destroy();
        return false;
    }

    res = fns.BindBufferMemory(device.GetHandle(), Handle, Allocation.Memory, 0);
    if (res != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkBindBufferMemory failed: %s\n",
            debugName ? debugName : "<unnamed buffer>", FormatResult(res).c_str());
        Destroy();
        return false;
    }

    Size = size;

    if (debugName)
    {
        device.SetDebugName(VK_OBJECT_TYPE_BUFFER, Handle, debugName);
        device.SetDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, Allocation.Memory, debugName);
    }

    return true;
}

void Buffer::Destroy()
{
    if (!Device)
    {
        Handle = VK_NULL_HANDLE;
        View = VK_NULL_HANDLE;
        Size = 0;
        Allocation = DeviceAllocation{};
        return;
    }

    const DeviceDispatch& fns = Device->Fns();
    VkDevice device = Device->GetHandle();

    // Views are children of the buffer and must die first.
    if (View != VK_NULL_HANDLE)
    {
        fns.DestroyBufferView(device, View, nullptr);
        View = VK_NULL_HANDLE;
    }

    if (Handle != VK_NULL_HANDLE)
    {
        fns.DestroyBuffer(device, Handle, nullptr);
        Handle = VK_NULL_HANDLE;
    }

    FreeAllocation(Device, fns, device, Allocation);

    Size = 0;
    Device = nullptr;
}

bool Buffer::CreateView(VkFormat format, VkDeviceSize offset, VkDeviceSize range, const char* debugName)
{
    if (!Device || Handle == VK_NULL_HANDLE)
        return false;

    if (View != VK_NULL_HANDLE)
    {
        Device->Fns().DestroyBufferView(Device->GetHandle(), View, nullptr);
        View = VK_NULL_HANDLE;
    }

    VkBufferViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    info.buffer = Handle;
    info.format = format;
    info.offset = offset;
    info.range = range;

    const VkResult res = Device->Fns().CreateBufferView(Device->GetHandle(), &info, nullptr, &View);
    if (res != VK_SUCCESS)
    {
        View = VK_NULL_HANDLE;
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkCreateBufferView failed: %s\n",
            debugName ? debugName : "<unnamed buffer view>", FormatResult(res).c_str());
        return false;
    }

    if (debugName)
        Device->SetDebugName(VK_OBJECT_TYPE_BUFFER_VIEW, View, debugName);

    return true;
}

bool Buffer::Map()
{
    if (!Device || !Allocation.IsValid())
        return false;
    if (Allocation.Mapped)
        return true;

    if (!Allocation.IsHostVisible())
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] attempted to map a buffer in non-host-visible memory (type %u)\n",
            Allocation.TypeIndex);
        return false;
    }

    const VkResult res = Device->Fns().MapMemory(
        Device->GetHandle(), Allocation.Memory, 0, VK_WHOLE_SIZE, 0, &Allocation.Mapped);
    if (res != VK_SUCCESS)
    {
        Allocation.Mapped = nullptr;
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] vkMapMemory failed for %.1f MiB: %s\n",
            static_cast<double>(Allocation.Size) / (1024.0 * 1024.0), FormatResult(res).c_str());
        return false;
    }
    return true;
}

void Buffer::Unmap()
{
    if (!Device || !Allocation.Mapped)
        return;

    Device->Fns().UnmapMemory(Device->GetHandle(), Allocation.Memory);
    Allocation.Mapped = nullptr;
}

bool Buffer::FlushRange(VkDeviceSize offset, VkDeviceSize size)
{
    if (!Device || !Allocation.IsValid())
        return false;

    // Coherent memory is kept in sync by the implementation; issuing a flush
    // anyway is legal but costs a driver call per frame for nothing.
    if (Allocation.IsCoherent())
        return true;

    AlignMappedRange(Allocation.Size, Device->GetLimits().nonCoherentAtomSize, offset, size);
    if (size == 0)
        return true;

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = Allocation.Memory;
    range.offset = offset;
    range.size = size;

    const VkResult res = Device->Fns().FlushMappedMemoryRanges(Device->GetHandle(), 1, &range);
    return MELONPRIME_VK_CHECK("vkFlushMappedMemoryRanges", res);
}

bool Buffer::InvalidateRange(VkDeviceSize offset, VkDeviceSize size)
{
    if (!Device || !Allocation.IsValid())
        return false;

    if (Allocation.IsCoherent())
        return true;

    AlignMappedRange(Allocation.Size, Device->GetLimits().nonCoherentAtomSize, offset, size);
    if (size == 0)
        return true;

    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = Allocation.Memory;
    range.offset = offset;
    range.size = size;

    const VkResult res = Device->Fns().InvalidateMappedMemoryRanges(Device->GetHandle(), 1, &range);
    return MELONPRIME_VK_CHECK("vkInvalidateMappedMemoryRanges", res);
}

bool Buffer::WriteMapped(VkDeviceSize offset, const void* src, VkDeviceSize bytes)
{
    if (!Allocation.Mapped || !src)
        return false;
    if (bytes == 0)
        return true;
    if (offset > Size || bytes > Size - offset)
        return false;

    std::memcpy(static_cast<u8*>(Allocation.Mapped) + offset, src, static_cast<size_t>(bytes));
    return FlushRange(offset, bytes);
}


// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

Image::~Image()
{
    Destroy();
}

Image::Image(Image&& other) noexcept
{
    MoveFrom(other);
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

void Image::MoveFrom(Image& other) noexcept
{
    Device = other.Device;
    Handle = other.Handle;
    View = other.View;
    Format = other.Format;
    Width = other.Width;
    Height = other.Height;
    ArrayLayers = other.ArrayLayers;
    MipLevels = other.MipLevels;
    Aspect = other.Aspect;
    Layout = other.Layout;
    Allocation = other.Allocation;

    other.Device = nullptr;
    other.Handle = VK_NULL_HANDLE;
    other.View = VK_NULL_HANDLE;
    other.Format = VK_FORMAT_UNDEFINED;
    other.Width = 0;
    other.Height = 0;
    other.ArrayLayers = 1;
    other.MipLevels = 1;
    other.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    other.Allocation = DeviceAllocation{};
}

bool Image::Create(const VulkanDevice& device, const CreateInfo& info)
{
    Destroy();

    if (!device.IsValid() || info.Width == 0 || info.Height == 0 || info.ArrayLayers == 0)
        return false;

    Device = &device;
    const DeviceDispatch& fns = device.Fns();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = info.Format;
    imageInfo.extent = { info.Width, info.Height, 1 };
    imageInfo.mipLevels = info.MipLevels;
    imageInfo.arrayLayers = info.ArrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = info.Tiling;
    imageInfo.usage = info.Usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // UNDEFINED is the only layout vkCreateImage accepts besides PREINITIALIZED,
    // and PREINITIALIZED is only meaningful for linear host-writable images.
    // Starting UNDEFINED means the first transition may discard the contents,
    // which is what every one of these images wants.
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = fns.CreateImage(device.GetHandle(), &imageInfo, nullptr, &Handle);
    if (res != VK_SUCCESS)
    {
        Handle = VK_NULL_HANDLE;
        Device = nullptr;
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkCreateImage failed for %ux%ux%u (format %d, usage 0x%08X): %s\n",
            info.DebugName ? info.DebugName : "<unnamed image>",
            info.Width, info.Height, info.ArrayLayers,
            static_cast<int>(info.Format),
            static_cast<unsigned>(info.Usage),
            FormatResult(res).c_str());
        return false;
    }

    VkMemoryRequirements requirements{};
    fns.GetImageMemoryRequirements(device.GetHandle(), Handle, &requirements);

    if (!AllocateFor(device, requirements, info.RequiredProperties, 0, info.DebugName, Allocation))
    {
        Destroy();
        return false;
    }

    res = fns.BindImageMemory(device.GetHandle(), Handle, Allocation.Memory, 0);
    if (res != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] %s: vkBindImageMemory failed: %s\n",
            info.DebugName ? info.DebugName : "<unnamed image>", FormatResult(res).c_str());
        Destroy();
        return false;
    }

    Format = info.Format;
    Width = info.Width;
    Height = info.Height;
    ArrayLayers = info.ArrayLayers;
    MipLevels = info.MipLevels;
    Aspect = info.Aspect;
    Layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (info.ViewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = Handle;
        viewInfo.viewType = info.ViewType;
        viewInfo.format = info.Format;
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = info.Aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = info.MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = info.ArrayLayers;

        res = fns.CreateImageView(device.GetHandle(), &viewInfo, nullptr, &View);
        if (res != VK_SUCCESS)
        {
            View = VK_NULL_HANDLE;
            Platform::Log(Platform::LogLevel::Error,
                "[Vulkan] %s: vkCreateImageView failed: %s\n",
                info.DebugName ? info.DebugName : "<unnamed image>", FormatResult(res).c_str());
            Destroy();
            return false;
        }
    }

    if (info.DebugName)
    {
        device.SetDebugName(VK_OBJECT_TYPE_IMAGE, Handle, info.DebugName);
        device.SetDebugName(VK_OBJECT_TYPE_DEVICE_MEMORY, Allocation.Memory, info.DebugName);
        if (View != VK_NULL_HANDLE)
            device.SetDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, View, info.DebugName);
    }

    return true;
}

void Image::Destroy()
{
    if (!Device)
    {
        Handle = VK_NULL_HANDLE;
        View = VK_NULL_HANDLE;
        Allocation = DeviceAllocation{};
        Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return;
    }

    const DeviceDispatch& fns = Device->Fns();
    VkDevice device = Device->GetHandle();

    if (View != VK_NULL_HANDLE)
    {
        fns.DestroyImageView(device, View, nullptr);
        View = VK_NULL_HANDLE;
    }

    if (Handle != VK_NULL_HANDLE)
    {
        fns.DestroyImage(device, Handle, nullptr);
        Handle = VK_NULL_HANDLE;
    }

    FreeAllocation(Device, fns, device, Allocation);

    Format = VK_FORMAT_UNDEFINED;
    Width = 0;
    Height = 0;
    ArrayLayers = 1;
    MipLevels = 1;
    Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    Device = nullptr;
}

void Image::RecordLayoutTransition(
    VkCommandBuffer cmd,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkAccessFlags srcAccessMask,
    VkPipelineStageFlags dstStageMask,
    VkAccessFlags dstAccessMask,
    u32 srcQueueFamily,
    u32 dstQueueFamily)
{
    if (!Device || Handle == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE)
        return;

    // A no-op transition is still a memory dependency when the queue family
    // changes, so the early-out only applies to the plain same-family case.
    if (Layout == newLayout
        && srcQueueFamily == VK_QUEUE_FAMILY_IGNORED
        && dstQueueFamily == VK_QUEUE_FAMILY_IGNORED
        && srcAccessMask == 0 && dstAccessMask == 0)
    {
        return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = Layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    barrier.image = Handle;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.subresourceRange.aspectMask = Aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = MipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = ArrayLayers;

    Device->Fns().CmdPipelineBarrier(
        cmd,
        srcStageMask, dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    Layout = newLayout;
}


// ---------------------------------------------------------------------------
// StagingRing
// ---------------------------------------------------------------------------

StagingRing::~StagingRing()
{
    Destroy();
}

bool StagingRing::Create(const VulkanDevice& device, VkDeviceSize capacity, const char* debugName)
{
    Destroy();

    // TRANSFER_SRC only: the ring is never read by a shader, so no storage or
    // uniform usage is requested and the driver is free to place it in plain
    // write-combined system memory.
    //
    // HOST_COHERENT is *preferred* rather than required. When it is available
    // the per-upload flush disappears; when it is not, FlushWritten() covers
    // correctness. Requiring it would exclude devices whose only host-visible
    // type is uncached.
    if (!Storage.Create(device, capacity,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            debugName))
    {
        return false;
    }

    if (!Storage.Map())
    {
        Storage.Destroy();
        return false;
    }

    Mapped = static_cast<u8*>(Storage.GetMappedPointer());
    Head = 0;
    FlushedUpTo = 0;
    return true;
}

void StagingRing::Destroy()
{
    Storage.Destroy();
    Mapped = nullptr;
    Head = 0;
    FlushedUpTo = 0;
}

void* StagingRing::Allocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& outOffset) noexcept
{
    outOffset = 0;

    if (!Mapped || size == 0)
        return nullptr;

    const VkDeviceSize offset = AlignUp(Head, alignment == 0 ? 1 : alignment);
    if (offset > Storage.GetSize() || size > Storage.GetSize() - offset)
        return nullptr;

    Head = offset + size;
    outOffset = offset;
    return Mapped + offset;
}

bool StagingRing::Upload(const void* src, VkDeviceSize size, VkDeviceSize alignment,
                         VkDeviceSize& outOffset) noexcept
{
    if (!src)
        return false;

    void* dst = Allocate(size, alignment, outOffset);
    if (!dst)
        return false;

    std::memcpy(dst, src, static_cast<size_t>(size));
    return true;
}

bool StagingRing::FlushWritten()
{
    if (!Mapped || Head <= FlushedUpTo)
        return true;

    const VkDeviceSize offset = FlushedUpTo;
    const VkDeviceSize size = Head - FlushedUpTo;
    FlushedUpTo = Head;

    // Buffer::FlushRange handles both the coherent early-out and the
    // nonCoherentAtomSize rounding.
    return Storage.FlushRange(offset, size);
}


// ---------------------------------------------------------------------------
// ReadbackBuffer
// ---------------------------------------------------------------------------

ReadbackBuffer::~ReadbackBuffer()
{
    Destroy();
}

bool ReadbackBuffer::Create(const VulkanDevice& device, VkDeviceSize size, const char* debugName)
{
    Destroy();

    if (!Storage.Create(device, size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            debugName))
    {
        return false;
    }

    if (!Storage.Map())
    {
        Storage.Destroy();
        return false;
    }

    Mapped = static_cast<u8*>(Storage.GetMappedPointer());
    return true;
}

void ReadbackBuffer::Destroy()
{
    Storage.Destroy();
    Mapped = nullptr;
}

bool ReadbackBuffer::Invalidate(VkDeviceSize offset, VkDeviceSize size)
{
    return Storage.InvalidateRange(offset, size);
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
