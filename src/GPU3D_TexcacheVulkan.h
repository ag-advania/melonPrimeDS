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

#ifndef GPU3D_TEXCACHE_VULKAN_H
#define GPU3D_TEXCACHE_VULKAN_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <vector>

#include "GPU3D_Texcache.h"
#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanSync.h"

namespace melonDS
{

template <typename, typename>
class Texcache;

// ---------------------------------------------------------------------------
// DS texture wrap modes
//
// The compute rasterizer deliberately does NOT wrap in the shader: the OpenGL
// renderer expressed the four DS wrap modes through nine sampler objects
// (GPU3D_Compute.cpp::Init(), Samplers[9]) and this port keeps that, because
// doing the wrap in the shader would change the filtering path. Rasterise.comp
// therefore declares pc.TexWrapS / pc.TexWrapT but never reads them, and the
// host is responsible for binding the matching sampler at set 1 binding 0.
//
// The DS wrap selector per axis is:
//      0 = clamp,  1 = repeat,  2 = mirrored repeat
// which maps one-to-one onto VkSamplerAddressMode.
//
// Filtering is always VK_FILTER_NEAREST. DS texture sampling is integer-exact:
// the shader reads a usampler2DArray whose texels are the decoded 6-bit colour
// channels, and any linear filtering would both blend integer channel values
// (meaningless) and break the bit-exact match with the software renderer.
// ---------------------------------------------------------------------------
class VulkanSamplerCache
{
public:
    static constexpr u32 WrapModeCount = 3;
    static constexpr u32 SamplerCount = WrapModeCount * WrapModeCount;

    VulkanSamplerCache() = default;
    ~VulkanSamplerCache();

    VulkanSamplerCache(const VulkanSamplerCache&) = delete;
    VulkanSamplerCache& operator=(const VulkanSamplerCache&) = delete;

    bool Create(const VulkanDevice& device);
    void Destroy();

    // `wrapS` / `wrapT` are the DS selectors above. Out-of-range values clamp
    // to 0 rather than indexing out of the table.
    [[nodiscard]] VkSampler Get(u32 wrapS, u32 wrapT) const noexcept;

    // Repeat on both axes: what the clear-bitmap textures use
    // (GPU3D_Compute.cpp::Init() sets GL_REPEAT + GL_NEAREST on both).
    [[nodiscard]] VkSampler GetRepeat() const noexcept { return Get(1, 1); }

    [[nodiscard]] bool IsValid() const noexcept { return Samplers[0] != VK_NULL_HANDLE; }

private:
    const VulkanDevice* Device = nullptr;
    std::array<VkSampler, SamplerCount> Samplers{};
};


// ---------------------------------------------------------------------------
// Owns every decoded-texture array image the texcache hands out.
//
// Like the DX12 sibling, the generic Texcache<> template only ever sees an
// opaque u32 handle, which keeps its batching comparison (`variant.Texture`)
// working exactly like the OpenGL backend's GLuint. Handles are 1-based so 0
// stays the "no texture" sentinel the renderer already uses.
//
// Images are VK_FORMAT_R8G8B8A8_UINT 2D arrays: the texcache decodes into
// outputFmt_RGB6A5, i.e. one byte per channel holding the raw 0..63 colour and
// 0..31 alpha values, and Rasterise.comp reads them through a usampler2DArray
// and compares them against those integer ranges directly.
// ---------------------------------------------------------------------------
class VulkanTextureHeap
{
public:
    struct Entry
    {
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        u32 Width = 0;
        u32 Height = 0;
        u32 Layers = 0;
        // Tracked so an upload only inserts the transitions it actually needs
        // instead of moving every array through TRANSFER_DST every frame.
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool InUse = false;
    };

    VulkanTextureHeap() = default;
    ~VulkanTextureHeap();

    VulkanTextureHeap(const VulkanTextureHeap&) = delete;
    VulkanTextureHeap& operator=(const VulkanTextureHeap&) = delete;

    void Init(const VulkanDevice* device, Vk::FrameRing* frames) noexcept;

    // Retires every image immediately. The caller must already have waited for
    // the device to go idle -- this is the teardown path, not the frame path.
    void Shutdown();

    // Opens a frame: uploads recorded from here on go into `cmd` and take
    // their staging space from `staging`.
    void BeginFrame(VkCommandBuffer cmd, Vk::StagingRing* staging) noexcept;

    // Moves every array written this frame into SHADER_READ_ONLY_OPTIMAL.
    // Called once, after the texcache finished updating and before the first
    // rasterise dispatch that samples them.
    void FlushUploadBarriers();

    u32 Create(u32 width, u32 height, u32 layers);
    void Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data);
    void Destroy(u32 handle);

    [[nodiscard]] const Entry* Lookup(u32 handle) const noexcept
    {
        if (handle == 0 || handle > Entries.size())
            return nullptr;
        const Entry& entry = Entries[handle - 1];
        return entry.InUse ? &entry : nullptr;
    }

private:
    // Records a temporary host-visible buffer for one oversized upload. The
    // ring cannot serve it, and dropping the texture would render garbage, so
    // a dedicated buffer is created and handed to the deferred destroy queue.
    bool CreateScratchUpload(VkDeviceSize size, VkBuffer& outBuffer, VkDeviceMemory& outMemory, void*& outMapped);

    void RetireEntry(Entry& entry);

    const VulkanDevice* Device = nullptr;
    Vk::FrameRing* Frames = nullptr;

    VkCommandBuffer FrameCommandBuffer = VK_NULL_HANDLE;
    Vk::StagingRing* FrameStaging = nullptr;

    std::vector<Entry> Entries;
    std::vector<u32> FreeSlots;
    std::vector<u32> PendingBarriers;
};


// Adapter the generic Texcache<> template drives. Identical in shape to
// TexcacheDX12Loader so both backends share the same base overloads
// (Update(u8&) and GetTexture(texParam, palBase, handle, layer, helper)).
class TexcacheVulkanLoader
{
public:
    explicit TexcacheVulkanLoader(VulkanTextureHeap* heap = nullptr) : Heap(heap) {}

    void SetHeap(VulkanTextureHeap* heap) noexcept { Heap = heap; }

    u32 GenerateTexture(u32 width, u32 height, u32 layers)
    {
        return Heap ? Heap->Create(width, height, layers) : 0;
    }

    void UploadTexture(u32 handle, u32 width, u32 height, u32 layer, void* data)
    {
        if (Heap) Heap->Upload(handle, width, height, layer, data);
    }

    void DeleteTexture(u32 handle)
    {
        if (Heap) Heap->Destroy(handle);
    }

private:
    VulkanTextureHeap* Heap;
};

using TexcacheVulkan = Texcache<TexcacheVulkanLoader, u32>;

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // GPU3D_TEXCACHE_VULKAN_H
