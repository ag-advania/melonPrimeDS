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

#include "GPU3D_TexcacheVulkan.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstring>
#include <new>

#include "VulkanMemory.h"
#include "VulkanPerf.h"

namespace melonDS
{

namespace
{

// The decoded texel format. outputFmt_RGB6A5 writes one byte per channel, so
// the image is an 8-bit-per-channel *integer* image, not UNORM: the shader
// reads the 0..63 / 0..31 values verbatim.
constexpr VkFormat TexcacheFormat = VK_FORMAT_R8G8B8A8_UINT;

constexpr VkDeviceSize TexelBytes = 4;
constexpr u32 kInitialPendingUploadCapacity = 64;
constexpr u32 kInitialPendingCreateCapacity = 64;

TextureMaterializeFailureReason ClassifyVulkanTextureCreationFailure(
    VkResult result) noexcept
{
    if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY
        || result == VK_ERROR_OUT_OF_HOST_MEMORY)
    {
        return TextureMaterializeFailureReason::OutOfMemory;
    }
    if (result == VK_ERROR_DEVICE_LOST)
        return TextureMaterializeFailureReason::DeviceLost;
    return TextureMaterializeFailureReason::Other;
}

} // namespace


// ---------------------------------------------------------------------------
// VulkanSamplerCache
// ---------------------------------------------------------------------------

VulkanSamplerCache::~VulkanSamplerCache()
{
    Destroy();
}

bool VulkanSamplerCache::Create(const VulkanDevice& device)
{
    Destroy();

    if (!device.IsValid())
        return false;

    Device = &device;
    const Vk::DeviceDispatch& fns = device.Fns();

    // Index layout matches the OpenGL renderer's Samplers[i + j*3], with i the
    // S axis and j the T axis, so the selector arithmetic in the renderer is
    // unchanged from GPU3D_Compute.cpp.
    static constexpr VkSamplerAddressMode TranslateWrapMode[WrapModeCount] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    };

    for (u32 t = 0; t < WrapModeCount; t++)
    {
        for (u32 s = 0; s < WrapModeCount; s++)
        {
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            // NEAREST everywhere, including the mip mode: the DS has no mip
            // levels and the sampled values are integers.
            info.magFilter = VK_FILTER_NEAREST;
            info.minFilter = VK_FILTER_NEAREST;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = TranslateWrapMode[s];
            info.addressModeV = TranslateWrapMode[t];
            // W is never sampled (the array layer is an index, not a
            // coordinate), so it only has to be a legal value.
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.mipLodBias = 0.0f;
            info.anisotropyEnable = VK_FALSE;
            info.maxAnisotropy = 1.0f;
            info.compareEnable = VK_FALSE;
            info.compareOp = VK_COMPARE_OP_NEVER;
            info.minLod = 0.0f;
            info.maxLod = 0.0f;
            info.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
            // Normalized coordinates: Rasterise.comp computes uv in [0,1) from
            // the fixed-point texcoords, exactly like the GL original.
            info.unnormalizedCoordinates = VK_FALSE;

            const u32 index = s + t * WrapModeCount;
            if (!MELONPRIME_VK_CHECK("vkCreateSampler",
                    fns.CreateSampler(device.GetHandle(), &info, nullptr, &Samplers[index])))
            {
                Destroy();
                return false;
            }

            device.SetDebugName(VK_OBJECT_TYPE_SAMPLER, Samplers[index], "MelonPrime DS texture sampler");
        }
    }

    return true;
}

void VulkanSamplerCache::Destroy()
{
    if (Device && Device->IsValid())
    {
        const Vk::DeviceDispatch& fns = Device->Fns();
        for (VkSampler& sampler : Samplers)
        {
            if (sampler != VK_NULL_HANDLE)
            {
                fns.DestroySampler(Device->GetHandle(), sampler, nullptr);
                sampler = VK_NULL_HANDLE;
            }
        }
    }
    else
    {
        Samplers.fill(VK_NULL_HANDLE);
    }

    Device = nullptr;
}

VkSampler VulkanSamplerCache::Get(u32 wrapS, u32 wrapT) const noexcept
{
    if (wrapS >= WrapModeCount) wrapS = 0;
    if (wrapT >= WrapModeCount) wrapT = 0;
    return Samplers[wrapS + wrapT * WrapModeCount];
}


// ---------------------------------------------------------------------------
// VulkanTextureHeap
// ---------------------------------------------------------------------------

VulkanTextureHeap::~VulkanTextureHeap()
{
    Shutdown();
}

void VulkanTextureHeap::Init(const VulkanDevice* device, Vk::FrameRing* frames) noexcept
{
    Device = device;
    Frames = frames;
    CreationFailed = false;
    UploadFailed = false;
    RetryableCreationFailure = false;
    MaterializeRetryAttempted = false;
    PendingUploadCount = 0;
    PendingCreateSlots.reserve(kInitialPendingCreateCapacity);
    PendingUploads.reserve(kInitialPendingUploadCapacity);
}

void VulkanTextureHeap::Shutdown()
{
    if (Device && Device->IsValid())
    {
        const Vk::DeviceDispatch& fns = Device->Fns();
        VkDevice handle = Device->GetHandle();

        // Teardown only: the caller has already waited for the device, so the
        // deferred queue is unnecessary and would outlive the frame ring.
        for (Entry& entry : Entries)
        {
            if (entry.View != VK_NULL_HANDLE)
                fns.DestroyImageView(handle, entry.View, nullptr);
            if (entry.Image != VK_NULL_HANDLE)
                fns.DestroyImage(handle, entry.Image, nullptr);
            if (entry.Memory != VK_NULL_HANDLE)
                fns.FreeMemory(handle, entry.Memory, nullptr);
            entry = Entry{};
        }
    }

    Entries.clear();
    FreeSlots.clear();
    PendingCreateSlots.clear();
    PendingBarriers.clear();
    PendingUploads.clear();
    PendingUploadCount = 0;
    FrameCommandBuffer = VK_NULL_HANDLE;
    FrameStaging = nullptr;
    Device = nullptr;
    Frames = nullptr;
    CreationFailed = false;
    UploadFailed = false;
    RetryableCreationFailure = false;
    MaterializeRetryAttempted = false;
}

void VulkanTextureHeap::BeginFrame(VkCommandBuffer cmd, Vk::StagingRing* staging) noexcept
{
    FrameCommandBuffer = cmd;
    FrameStaging = staging;
    PendingBarriers.clear();
}

u32 VulkanTextureHeap::Reserve(u32 width, u32 height, u32 layers)
{
    if (!Device || !Device->IsValid() || width == 0 || height == 0 || layers == 0)
    {
        CreationFailed = true;
        return 0;
    }

    Entry entry;
    entry.Width = width;
    entry.Height = height;
    entry.Layers = layers;
    entry.InUse = true;
    entry.PendingCreate = true;

    u32 slot;
    if (!FreeSlots.empty())
    {
        slot = FreeSlots.back();
        FreeSlots.pop_back();
        Entries[slot] = entry;
    }
    else
    {
        slot = static_cast<u32>(Entries.size());
        Entries.push_back(entry);
    }

    PendingCreateSlots.push_back(slot);

    return slot + 1;
}

TextureMaterializeResult VulkanTextureHeap::HandleMaterializeFailure(
    TextureMaterializeFailureReason reason) noexcept
{
    VulkanPerf::SetCounter(
        VulkanPerf::Counter::TextureMaterializeFailureReason,
        static_cast<u64>(reason));
    if (!MaterializeRetryAttempted)
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::TextureMaterializePreFenceFailCount);
        if (reason == TextureMaterializeFailureReason::OutOfMemory)
        {
            RetryableCreationFailure = true;
            return TextureMaterializeResult::RetryAfterRetire;
        }
    }

    CreationFailed = true;
    return TextureMaterializeResult::Fatal;
}

TextureMaterializeResult VulkanTextureHeap::MaterializePendingCreates()
{
    if (CreationFailed)
        return TextureMaterializeResult::Fatal;
    if (RetryableCreationFailure)
        return TextureMaterializeResult::RetryAfterRetire;
    if (!Device || !Device->IsValid())
        return HandleMaterializeFailure(TextureMaterializeFailureReason::Other);

    const Vk::DeviceDispatch& fns = Device->Fns();
    VkDevice handle = Device->GetHandle();

    for (u32 slot : PendingCreateSlots)
    {
        if (slot >= Entries.size())
            continue;

        Entry& entry = Entries[slot];
        if (!entry.InUse || !entry.PendingCreate)
            continue;

        VulkanPerf::ScopedCpuTimer createTimer(
            VulkanPerf::CpuMetric::TextureResourceCreate);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = TexcacheFormat;
        imageInfo.extent = { entry.Width, entry.Height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = entry.Layers;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        const VkResult createImageResult = fns.CreateImage(
            handle, &imageInfo, nullptr, &entry.Image);
        if (!MELONPRIME_VK_CHECK("vkCreateImage (texcache array)", createImageResult))
        {
            return HandleMaterializeFailure(
                ClassifyVulkanTextureCreationFailure(createImageResult));
        }

        VkMemoryRequirements requirements{};
        fns.GetImageMemoryRequirements(handle, entry.Image, &requirements);

        const u32 typeIndex = Vk::FindMemoryType(
            Device->GetMemoryProperties(),
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0);
        if (typeIndex == Vk::InvalidMemoryType)
        {
            Platform::Log(Platform::LogLevel::Error,
                "[Vulkan] texcache: no device-local memory type for a %ux%ux%u array\n",
                entry.Width, entry.Height, entry.Layers);
            fns.DestroyImage(handle, entry.Image, nullptr);
            entry.Image = VK_NULL_HANDLE;
            return HandleMaterializeFailure(
                TextureMaterializeFailureReason::InvalidMemoryType);
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = typeIndex;

        const VkResult allocateResult = fns.AllocateMemory(
            handle, &allocInfo, nullptr, &entry.Memory);
        if (!MELONPRIME_VK_CHECK("vkAllocateMemory (texcache array)", allocateResult))
        {
            fns.DestroyImage(handle, entry.Image, nullptr);
            entry.Image = VK_NULL_HANDLE;
            return HandleMaterializeFailure(
                ClassifyVulkanTextureCreationFailure(allocateResult));
        }

        const VkResult bindResult = fns.BindImageMemory(
            handle, entry.Image, entry.Memory, 0);
        if (!MELONPRIME_VK_CHECK("vkBindImageMemory (texcache array)", bindResult))
        {
            fns.FreeMemory(handle, entry.Memory, nullptr);
            fns.DestroyImage(handle, entry.Image, nullptr);
            entry.Memory = VK_NULL_HANDLE;
            entry.Image = VK_NULL_HANDLE;
            return HandleMaterializeFailure(
                ClassifyVulkanTextureCreationFailure(bindResult));
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = entry.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = TexcacheFormat;
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = entry.Layers;

        const VkResult createViewResult = fns.CreateImageView(
            handle, &viewInfo, nullptr, &entry.View);
        if (!MELONPRIME_VK_CHECK("vkCreateImageView (texcache array)", createViewResult))
        {
            fns.FreeMemory(handle, entry.Memory, nullptr);
            fns.DestroyImage(handle, entry.Image, nullptr);
            entry.Memory = VK_NULL_HANDLE;
            entry.Image = VK_NULL_HANDLE;
            return HandleMaterializeFailure(
                ClassifyVulkanTextureCreationFailure(createViewResult));
        }

        entry.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        entry.PhysicalReady = true;
        entry.PendingCreate = false;

        Device->SetDebugName(VK_OBJECT_TYPE_IMAGE, entry.Image, "MelonPrime texcache array");
        Device->SetDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, entry.View, "MelonPrime texcache array");
        VulkanPerf::AddCounter(VulkanPerf::Counter::TextureMaterializeCount);
    }

    PendingCreateSlots.clear();

    return TextureMaterializeResult::Ready;
}

bool VulkanTextureHeap::CreateScratchUpload(
    VkDeviceSize size, VkBuffer& outBuffer, VkDeviceMemory& outMemory, void*& outMapped)
{
    outBuffer = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;
    outMapped = nullptr;

    const Vk::DeviceDispatch& fns = Device->Fns();
    VkDevice handle = Device->GetHandle();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!MELONPRIME_VK_CHECK("vkCreateBuffer (texcache scratch upload)",
            fns.CreateBuffer(handle, &bufferInfo, nullptr, &outBuffer)))
        return false;

    VkMemoryRequirements requirements{};
    fns.GetBufferMemoryRequirements(handle, outBuffer, &requirements);

    const u32 typeIndex = Vk::FindMemoryType(
        Device->GetMemoryProperties(),
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (typeIndex == Vk::InvalidMemoryType)
    {
        fns.DestroyBuffer(handle, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;

    // This is the bounded per-upload spill path used only when the frame
    // staging ring is full. It intentionally bypasses the persistent
    // reservation/diagnostic counters; vkAllocateMemory is the final memory
    // pressure check, and the caller drops this upload safely when creation,
    // binding, or mapping fails. Destruction is deferred with the upload's
    // frame so the GPU never observes a recycled scratch allocation.
    if (!MELONPRIME_VK_CHECK("vkAllocateMemory (texcache scratch upload)",
            fns.AllocateMemory(handle, &allocInfo, nullptr, &outMemory)))
    {
        fns.DestroyBuffer(handle, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    if (!MELONPRIME_VK_CHECK("vkBindBufferMemory (texcache scratch upload)",
            fns.BindBufferMemory(handle, outBuffer, outMemory, 0))
        || !MELONPRIME_VK_CHECK("vkMapMemory (texcache scratch upload)",
            fns.MapMemory(handle, outMemory, 0, VK_WHOLE_SIZE, 0, &outMapped)))
    {
        fns.FreeMemory(handle, outMemory, nullptr);
        fns.DestroyBuffer(handle, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        outMapped = nullptr;
        return false;
    }

    // The memory type search above required HOST_VISIBLE and only preferred
    // HOST_COHERENT, so a non-coherent type is possible. vkFlushMappedMemory-
    // Ranges is issued by the caller before the copy is recorded.
    return true;
}

bool VulkanTextureHeap::EnsurePendingStorage(
    PendingUpload& pending, std::size_t words) noexcept
{
    if (pending.CapacityWords >= words)
        return true;

    // Scalar array default-initialization leaves the words uninitialized. The
    // decoder overwrites the complete target before it is committed.
    std::unique_ptr<u32[]> replacement(new (std::nothrow) u32[words]);
    if (!replacement)
        return false;

    pending.Data = std::move(replacement);
    pending.CapacityWords = words;
    return true;
}

VulkanTextureHeap::PendingUpload* VulkanTextureHeap::AcquirePendingUpload(
    u32 handle, u32 width, u32 height, u32 layer, std::size_t words) noexcept
{
    if (PendingUploadCount == PendingUploads.size())
    {
        try
        {
            PendingUploads.emplace_back();
        }
        catch (...)
        {
            return nullptr;
        }
    }

    PendingUpload& pending = PendingUploads[PendingUploadCount];
    pending.Handle = handle;
    pending.Width = width;
    pending.Height = height;
    pending.Layer = layer;
    const bool storageGrows = pending.CapacityWords < words;
    bool storageReady = false;
    {
        VulkanPerf::ScopedCpuTimer growTimer(
            VulkanPerf::CpuMetric::TexturePendingStorageGrow, storageGrows);
        storageReady = EnsurePendingStorage(pending, words);
    }
    if (!storageReady)
        return nullptr;
    if (storageGrows)
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::TexturePendingStorageGrowCount);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::TexturePendingStorageGrowBytes,
            pending.CapacityWords * sizeof(u32));
    }
    pending.UsedWords = words;
    pending.Committed = false;
    PendingUploadCount++;
    return &pending;
}

TextureDecodeTarget VulkanTextureHeap::BeginTextureUpload(
    u32 handle, u32 width, u32 height, u32 layer)
{
    if (handle == 0 || handle > Entries.size())
    {
        UploadFailed = true;
        return {};
    }

    const Entry& entry = Entries[handle - 1];
    if (!entry.InUse || layer >= entry.Layers || width != entry.Width || height != entry.Height)
    {
        UploadFailed = true;
        return {};
    }

    const std::size_t words = static_cast<std::size_t>(width) * height;
    PendingUpload* pending = AcquirePendingUpload(handle, width, height, layer, words);
    if (!pending)
    {
        UploadFailed = true;
        return {};
    }
    return {
        pending->Data.get(), pending->UsedWords * sizeof(u32), PendingUploadCount};
}

void VulkanTextureHeap::CommitTextureUpload(u32 token) noexcept
{
    if (token == 0 || token > PendingUploadCount)
    {
        UploadFailed = true;
        return;
    }

    PendingUpload& pending = PendingUploads[token - 1];
    if (pending.Committed)
    {
        UploadFailed = true;
        return;
    }
    pending.Committed = true;
    VulkanPerf::AddCounter(
        VulkanPerf::Counter::TexturePendingUploadBytes,
        pending.UsedWords * sizeof(u32));
    VulkanPerf::AddCounter(VulkanPerf::Counter::TexturePendingUploadCount);
}

void VulkanTextureHeap::CancelTextureUpload(u32 token) noexcept
{
    if (token == 0 || token > PendingUploadCount)
        return;

    const u32 index = token - 1;
    const u32 last = PendingUploadCount - 1;
    if (index != last)
        std::swap(PendingUploads[index], PendingUploads[last]);
    PendingUploads[last].Committed = false;
    PendingUploads[last].UsedWords = 0;
    PendingUploadCount = last;
}

void VulkanTextureHeap::Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data)
{
    if (UploadFailed)
        return;
    if (!Device || !Device->IsValid() || !Frames || !data)
    {
        UploadFailed = true;
        return;
    }
    if (handle == 0 || handle > Entries.size())
    {
        UploadFailed = true;
        return;
    }

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse || layer >= entry.Layers || width != entry.Width || height != entry.Height)
    {
        UploadFailed = true;
        return;
    }

    const Vk::FrameContext* frame = Frames->GetCurrentFrame();
    if (FrameCommandBuffer == VK_NULL_HANDLE || !FrameStaging
        || !frame || !frame->Recording)
    {
        const std::size_t words = static_cast<std::size_t>(width) * height;
        PendingUpload* pending = AcquirePendingUpload(handle, width, height, layer, words);
        if (!pending)
        {
            UploadFailed = true;
            return;
        }
        VulkanPerf::ScopedCpuTimer copyTimer(VulkanPerf::CpuMetric::TexturePendingCpuCopy);
        const std::size_t bytes = pending->UsedWords * sizeof(u32);
        std::memcpy(pending->Data.get(), data, bytes);
        pending->Committed = true;
        VulkanPerf::AddCounter(VulkanPerf::Counter::TexturePendingUploadBytes, bytes);
        VulkanPerf::AddCounter(VulkanPerf::Counter::TexturePendingUploadCount);
        return;
    }

    if (!RecordUpload(handle, width, height, layer, data))
        UploadFailed = true;
}

void VulkanTextureHeap::RecordPendingUploads()
{
    const Vk::FrameContext* frame = Frames ? Frames->GetCurrentFrame() : nullptr;
    if (PendingUploadCount == 0 || FrameCommandBuffer == VK_NULL_HANDLE || !FrameStaging
        || !frame || !frame->Recording)
        return;

    for (u32 i = 0; i < PendingUploadCount; i++)
    {
        PendingUpload& pending = PendingUploads[i];
        if (pending.Committed && !RecordUpload(
            pending.Handle, pending.Width, pending.Height, pending.Layer,
            pending.Data.get()))
        {
            UploadFailed = true;
        }
        pending.Committed = false;
        pending.UsedWords = 0;
    }
    PendingUploadCount = 0;
}

bool VulkanTextureHeap::RecordUpload(
    u32 handle, u32 width, u32 height, u32 layer, const void* data)
{
    const Vk::FrameContext* frame = Frames ? Frames->GetCurrentFrame() : nullptr;
    if (!Device || !Device->IsValid() || !Frames || FrameCommandBuffer == VK_NULL_HANDLE
        || !frame || !frame->Recording || !data)
        return false;
    if (handle == 0 || handle > Entries.size())
        return false;

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse || !entry.PhysicalReady || entry.Image == VK_NULL_HANDLE
        || layer >= entry.Layers || width != entry.Width || height != entry.Height)
        return false;

    const Vk::DeviceDispatch& fns = Device->Fns();
    VkDevice device = Device->GetHandle();

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * TexelBytes;
    VulkanPerf::AddCounter(VulkanPerf::Counter::TextureUploadBytes, bytes);

    // vkCmdCopyBufferToImage requires bufferOffset to be a multiple of 4 and of
    // the texel block size (also 4 here). optimalBufferCopyOffsetAlignment is
    // advisory but free to honour.
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
        TexelBytes, Device->GetLimits().optimalBufferCopyOffsetAlignment);

    VkBuffer srcBuffer = VK_NULL_HANDLE;
    VkDeviceSize srcOffset = 0;
    bool usedScratch = false;
    VkDeviceMemory scratchMemory = VK_NULL_HANDLE;

    void* mapped = FrameStaging ? FrameStaging->Allocate(bytes, alignment, srcOffset) : nullptr;
    if (mapped)
    {
        srcBuffer = FrameStaging->GetHandle();
    }
    else
    {
        // The per-frame ring is full. Dropping the upload would leave the
        // rasterizer sampling undefined texels, so a dedicated buffer carries
        // this one texture; it is destroyed once this frame retires.
        if (!CreateScratchUpload(bytes, srcBuffer, scratchMemory, mapped))
        {
            Platform::Log(Platform::LogLevel::Error,
                "[Vulkan] texcache: could not stage a %ux%u texture upload (%llu bytes)\n",
                width, height, static_cast<unsigned long long>(bytes));
            return false;
        }
        usedScratch = true;
        srcOffset = 0;
        VulkanPerf::AddCounter(VulkanPerf::Counter::ScratchUploadCount);
        VulkanPerf::AddCounter(VulkanPerf::Counter::ScratchUploadBytes, bytes);
    }

    std::memcpy(mapped, data, static_cast<size_t>(bytes));

    if (usedScratch)
    {
        // The scratch allocation only *preferred* HOST_COHERENT, so it may be
        // non-coherent and the host write has to be made available to the
        // device explicitly. VK_WHOLE_SIZE is always a legal mapped-range size
        // and needs no nonCoherentAtomSize rounding; on coherent memory the
        // call is a driver-side no-op rather than an error.
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = scratchMemory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        const u64 recordingFrame = Frames->GetCurrentRecordingFrameNumber();
        Frames->GetDestroyQueue().Enqueue(
            Vk::DeferredObject::Buffer, srcBuffer, recordingFrame);
        Frames->GetDestroyQueue().Enqueue(
            Vk::DeferredObject::DeviceMemory, scratchMemory, recordingFrame);

        if (!MELONPRIME_VK_CHECK("vkFlushMappedMemoryRanges (texcache scratch upload)",
                fns.FlushMappedMemoryRanges(device, 1, &range)))
            return false;
    }

    // Move the whole array into TRANSFER_DST. A layout transition preserves the
    // contents of every subresource unless oldLayout is UNDEFINED, so the
    // already-uploaded layers of this array survive.
    if (entry.Layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = entry.Layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = entry.Layers;

        VkPipelineStageFlags srcStage;
        if (entry.Layout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            // Nothing has read or written the image yet, so there is no prior
            // access to make available.
            barrier.srcAccessMask = 0;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        else
        {
            // The previous frame's rasterise dispatches sampled it.
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        fns.CmdPipelineBarrier(
            FrameCommandBuffer,
            srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        entry.Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkBufferImageCopy copy{};
    copy.bufferOffset = srcOffset;
    // 0/0 means "tightly packed to the image extent", which is how the
    // texcache's decoding buffer is laid out.
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = layer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0, 0, 0 };
    copy.imageExtent = { width, height, 1 };

    fns.CmdCopyBufferToImage(
        FrameCommandBuffer, srcBuffer, entry.Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    const u32 slot = handle - 1;
    if (std::find(PendingBarriers.begin(), PendingBarriers.end(), slot) == PendingBarriers.end())
        PendingBarriers.push_back(slot);
    return true;
}

void VulkanTextureHeap::FlushUploadBarriers()
{
    if (PendingBarriers.empty())
        return;

    if (!Device || !Device->IsValid() || FrameCommandBuffer == VK_NULL_HANDLE)
    {
        PendingBarriers.clear();
        return;
    }

    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(PendingBarriers.size());

    for (u32 slot : PendingBarriers)
    {
        if (slot >= Entries.size())
            continue;
        Entry& entry = Entries[slot];
        if (!entry.InUse || entry.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            continue;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // The copies above wrote the image; the rasterise dispatches sample it.
        // This is the only dependency between the two, so it is stated exactly:
        // transfer writes become available and visible to compute reads.
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = entry.Layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = entry.Layers;

        barriers.push_back(barrier);
        entry.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    if (!barriers.empty())
    {
        Device->Fns().CmdPipelineBarrier(
            FrameCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<u32>(barriers.size()), barriers.data());
    }

    PendingBarriers.clear();
}

void VulkanTextureHeap::RetireEntry(Entry& entry)
{
    if (!entry.InUse)
        return;

    if (!entry.PhysicalReady)
    {
        // A reservation that never reached the materialization boundary has no
        // driver object and must not enter the deferred-destroy queue.
        entry = Entry{};
        return;
    }

    if (Frames)
    {
        // A cached image can be invalidated while a submitted or currently
        // recording command buffer still references it. Tag the last frame
        // that may legally reference it; the scheduler's next-frame counter
        // would retain CPU-prep invalidations one frame too long.
        const u64 frame = Frames->GetResourceRetireFrame();
        Vk::DeferredDestroyQueue& queue = Frames->GetDestroyQueue();
        queue.Enqueue(Vk::DeferredObject::ImageView, entry.View, frame);
        queue.Enqueue(Vk::DeferredObject::Image, entry.Image, frame);
        queue.Enqueue(Vk::DeferredObject::DeviceMemory, entry.Memory, frame);
    }
    else if (Device && Device->IsValid())
    {
        const Vk::DeviceDispatch& fns = Device->Fns();
        VkDevice handle = Device->GetHandle();
        if (entry.View != VK_NULL_HANDLE) fns.DestroyImageView(handle, entry.View, nullptr);
        if (entry.Image != VK_NULL_HANDLE) fns.DestroyImage(handle, entry.Image, nullptr);
        if (entry.Memory != VK_NULL_HANDLE) fns.FreeMemory(handle, entry.Memory, nullptr);
    }

    entry = Entry{};
}

void VulkanTextureHeap::Destroy(u32 handle)
{
    if (handle == 0 || handle > Entries.size())
        return;

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse)
        return;

    RetireEntry(entry);
    FreeSlots.push_back(handle - 1);

    PendingBarriers.erase(
        std::remove(PendingBarriers.begin(), PendingBarriers.end(), handle - 1),
        PendingBarriers.end());
    PendingCreateSlots.erase(
        std::remove(PendingCreateSlots.begin(), PendingCreateSlots.end(), handle - 1),
        PendingCreateSlots.end());
    u32 i = 0;
    while (i < PendingUploadCount)
    {
        if (PendingUploads[i].Handle != handle)
        {
            i++;
            continue;
        }

        PendingUploadCount--;
        if (i != PendingUploadCount)
            std::swap(PendingUploads[i], PendingUploads[PendingUploadCount]);
        PendingUploads[PendingUploadCount].Committed = false;
        PendingUploads[PendingUploadCount].UsedWords = 0;
    }
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
