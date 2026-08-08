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

#ifndef VULKAN_MEMORY_H
#define VULKAN_MEMORY_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstddef>

#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanLoader.h"

namespace melonDS::Vk
{

// ---------------------------------------------------------------------------
// Memory, buffers and images.
//
// There is no third-party allocator here on purpose. The backend makes a small
// number of very large, long-lived allocations (the tile buffers alone are
// three quarters of a gigabyte at 16x) plus a handful of persistently mapped
// staging rings. That is the workload sub-allocators are worst at justifying,
// and a hand-written path keeps the failure reporting precise: when a 768 MiB
// allocation fails, the log has to say which buffer, how large, and out of
// which memory type.
//
// Every size is VkDeviceSize. At 16x several of these values exceed 2^31, so a
// stray u32 or int would silently truncate on a 32-bit build.
// ---------------------------------------------------------------------------

inline constexpr u32 InvalidMemoryType = ~0u;

// Picks a memory type index out of the device's real memory type table.
//
// Memory type indices are device- and driver-specific: index 0 is device-local
// on one GPU and host-visible on the next, so they are never hardcoded. The
// search is two-pass -- first for a type carrying `required | preferred`, then
// for `required` alone -- so, for example, a readback buffer gets HOST_CACHED
// when the device offers it and still works when it does not.
//
// Returns InvalidMemoryType when nothing satisfies `required`.
[[nodiscard]] u32 FindMemoryType(
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    u32 memoryTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred) noexcept;

// Rounds a mapped-range [offset, offset+size) out to nonCoherentAtomSize
// boundaries, clamped to `allocationSize`.
//
// vkFlushMappedMemoryRanges / vkInvalidateMappedMemoryRanges require offset to
// be a multiple of nonCoherentAtomSize and size to be either VK_WHOLE_SIZE, a
// multiple of nonCoherentAtomSize, or the remainder to the end of the
// allocation. Rounding *outward* is safe: flushing or invalidating more than
// was written is always correct, flushing less is not.
void AlignMappedRange(
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize,
    VkDeviceSize& offset,
    VkDeviceSize& size) noexcept;


// One vkAllocateMemory result plus everything needed to use it correctly.
struct DeviceAllocation
{
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    u32 TypeIndex = InvalidMemoryType;
    VkMemoryPropertyFlags Properties = 0;
    void* Mapped = nullptr;

    [[nodiscard]] bool IsValid() const noexcept { return Memory != VK_NULL_HANDLE; }
    [[nodiscard]] bool IsHostVisible() const noexcept
    {
        return (Properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }
    // Coherent memory needs no explicit flush/invalidate; the wrappers below
    // skip both calls in that case rather than issuing no-op work every frame.
    [[nodiscard]] bool IsCoherent() const noexcept
    {
        return (Properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
};


// A VkBuffer with its own dedicated allocation.
//
// Dedicated rather than sub-allocated: the backend's buffers are few, huge and
// created once per resolution change, so the memory-per-buffer overhead is
// irrelevant and the lifetime rules become trivial.
class Buffer
{
public:
    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    // `preferredProperties` is advisory; `requiredProperties` is not. Returns
    // false after logging the requested size, usage and the memory types that
    // were considered.
    bool Create(
        const VulkanDevice& device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredProperties,
        VkMemoryPropertyFlags preferredProperties,
        const char* debugName);

    // Immediate destruction. Anything that may still be referenced by an
    // in-flight frame must go through DeferredDestroyQueue instead.
    void Destroy();

    // Texel buffer view, needed for the SetupIndices binding.
    bool CreateView(VkFormat format, VkDeviceSize offset, VkDeviceSize range, const char* debugName);

    [[nodiscard]] bool IsValid() const noexcept { return Handle != VK_NULL_HANDLE; }
    [[nodiscard]] VkBuffer GetHandle() const noexcept { return Handle; }
    [[nodiscard]] VkBufferView GetView() const noexcept { return View; }
    [[nodiscard]] VkDeviceSize GetSize() const noexcept { return Size; }
    [[nodiscard]] const DeviceAllocation& GetAllocation() const noexcept { return Allocation; }

    // Persistent mapping. Vulkan places no cost on keeping host-visible memory
    // mapped for the object's whole lifetime, and repeated map/unmap cycles are
    // measurably slower on several drivers, so mapping happens once.
    bool Map();
    void Unmap();
    [[nodiscard]] void* GetMappedPointer() const noexcept { return Allocation.Mapped; }

    // CPU wrote [offset, size): make it visible to the device. No-op on
    // coherent memory. Returns false if the flush itself failed.
    bool FlushRange(VkDeviceSize offset, VkDeviceSize size);

    // Device wrote [offset, size): make it visible to the CPU. No-op on
    // coherent memory.
    bool InvalidateRange(VkDeviceSize offset, VkDeviceSize size);

    // Copies `bytes` from `src` into the mapping at `offset` and flushes.
    // Fails (without partial writes) when the buffer is not mapped or the range
    // does not fit.
    bool WriteMapped(VkDeviceSize offset, const void* src, VkDeviceSize bytes);

private:
    void MoveFrom(Buffer& other) noexcept;

    const VulkanDevice* Device = nullptr;
    VkBuffer Handle = VK_NULL_HANDLE;
    VkBufferView View = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    DeviceAllocation Allocation;
};


// A VkImage with its own allocation and (optionally) a default view.
class Image
{
public:
    struct CreateInfo
    {
        VkFormat Format = VK_FORMAT_UNDEFINED;
        u32 Width = 0;
        u32 Height = 0;
        u32 ArrayLayers = 1;
        u32 MipLevels = 1;
        VkImageUsageFlags Usage = 0;
        VkImageTiling Tiling = VK_IMAGE_TILING_OPTIMAL;
        VkMemoryPropertyFlags RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        // VK_IMAGE_VIEW_TYPE_MAX_ENUM means "do not create a view".
        VkImageViewType ViewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        const char* DebugName = nullptr;
    };

    Image() = default;
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    bool Create(const VulkanDevice& device, const CreateInfo& info);
    void Destroy();

    [[nodiscard]] bool IsValid() const noexcept { return Handle != VK_NULL_HANDLE; }
    [[nodiscard]] VkImage GetHandle() const noexcept { return Handle; }
    [[nodiscard]] VkImageView GetView() const noexcept { return View; }
    [[nodiscard]] VkFormat GetFormat() const noexcept { return Format; }
    [[nodiscard]] u32 GetWidth() const noexcept { return Width; }
    [[nodiscard]] u32 GetHeight() const noexcept { return Height; }
    [[nodiscard]] u32 GetArrayLayers() const noexcept { return ArrayLayers; }
    [[nodiscard]] u32 GetMipLevels() const noexcept { return MipLevels; }
    [[nodiscard]] VkImageLayout GetLayout() const noexcept { return Layout; }
    [[nodiscard]] const DeviceAllocation& GetAllocation() const noexcept { return Allocation; }

    // Records a layout transition and updates the tracked layout.
    //
    // The stage and access masks are the caller's responsibility on purpose:
    // only the caller knows which stage produced the previous contents and
    // which will consume them. Passing VK_PIPELINE_STAGE_ALL_COMMANDS_BIT here
    // would serialize the frame, so the parameters are required rather than
    // defaulted.
    void RecordLayoutTransition(
        VkCommandBuffer cmd,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStageMask,
        VkAccessFlags srcAccessMask,
        VkPipelineStageFlags dstStageMask,
        VkAccessFlags dstAccessMask,
        u32 srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
        u32 dstQueueFamily = VK_QUEUE_FAMILY_IGNORED);

    // For images whose layout changed through a path this object did not
    // record (a render pass final layout, or a swapchain acquire).
    void OverrideTrackedLayout(VkImageLayout layout) noexcept { Layout = layout; }

private:
    void MoveFrom(Image& other) noexcept;

    const VulkanDevice* Device = nullptr;
    VkImage Handle = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    u32 Width = 0;
    u32 Height = 0;
    u32 ArrayLayers = 1;
    u32 MipLevels = 1;
    VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    DeviceAllocation Allocation;
};


// Persistently mapped host-visible upload buffer with bump allocation.
//
// Texture uploads, uniform updates and the per-frame span/polygon data all come
// through here. The ring is reset once per frame *after* that frame's fence has
// signalled, which is what makes overwriting the previous contents safe -- there
// is no per-allocation lifetime tracking and there does not need to be.
class StagingRing
{
public:
    StagingRing() = default;
    ~StagingRing();

    StagingRing(const StagingRing&) = delete;
    StagingRing& operator=(const StagingRing&) = delete;

    bool Create(const VulkanDevice& device, VkDeviceSize capacity, const char* debugName);
    void Destroy();

    // FlushedUpTo has to move back with Head: it is a watermark into the same
    // address space, so leaving it behind would make FlushWritten() consider
    // the next frame's writes already flushed and skip them entirely on
    // non-coherent memory.
    void Reset() noexcept { Head = 0; FlushedUpTo = 0; }

    // Reserves `size` bytes aligned to `alignment` and returns the mapped
    // pointer plus the buffer offset to use in a VkBufferCopy /
    // VkBufferImageCopy. Returns nullptr when the remaining space is
    // insufficient; the caller must then submit, wait for the frame and Reset()
    // -- growing implicitly would strand the offsets already recorded into the
    // open command buffer.
    [[nodiscard]] void* Allocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& outOffset) noexcept;

    // Convenience: reserve, memcpy and return the offset.
    [[nodiscard]] bool Upload(const void* src, VkDeviceSize size, VkDeviceSize alignment,
                              VkDeviceSize& outOffset) noexcept;

    // Flushes everything written since the last Reset(). Must be called before
    // the submission that reads the ring. No-op on coherent memory.
    bool FlushWritten();

    [[nodiscard]] VkBuffer GetHandle() const noexcept { return Storage.GetHandle(); }
    [[nodiscard]] VkDeviceSize GetCapacity() const noexcept { return Storage.GetSize(); }
    [[nodiscard]] VkDeviceSize GetUsed() const noexcept { return Head; }
    [[nodiscard]] bool IsValid() const noexcept { return Storage.IsValid(); }

private:
    Buffer Storage;
    u8* Mapped = nullptr;
    VkDeviceSize Head = 0;
    VkDeviceSize FlushedUpTo = 0;
};


// Host-visible destination for vkCmdCopyImageToBuffer / vkCmdCopyBuffer
// readback.
//
// Allocated HOST_CACHED when the device offers it: readback is CPU-read-heavy
// (GetLine() walks every scanline), and uncached write-combined memory makes
// that read path roughly an order of magnitude slower. HOST_CACHED memory is
// usually non-coherent, which is exactly why Invalidate() exists.
class ReadbackBuffer
{
public:
    ReadbackBuffer() = default;
    ~ReadbackBuffer();

    ReadbackBuffer(const ReadbackBuffer&) = delete;
    ReadbackBuffer& operator=(const ReadbackBuffer&) = delete;

    bool Create(const VulkanDevice& device, VkDeviceSize size, const char* debugName);
    void Destroy();

    // Call after the fence for the copy has signalled and before reading
    // GetData(). No-op on coherent memory.
    bool Invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    [[nodiscard]] const u8* GetData() const noexcept { return Mapped; }
    [[nodiscard]] VkBuffer GetHandle() const noexcept { return Storage.GetHandle(); }
    [[nodiscard]] VkDeviceSize GetSize() const noexcept { return Storage.GetSize(); }
    [[nodiscard]] bool IsValid() const noexcept { return Storage.IsValid(); }

private:
    Buffer Storage;
    u8* Mapped = nullptr;
};

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_MEMORY_H
