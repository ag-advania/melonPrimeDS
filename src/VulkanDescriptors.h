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

#ifndef VULKAN_DESCRIPTORS_H
#define VULKAN_DESCRIPTORS_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <cstddef>
#include <vector>

#include "VulkanCommon.h"
#include "VulkanLoader.h"

namespace melonDS::Vk
{

// ---------------------------------------------------------------------------
// Descriptor binding contract
//
// This is the single source of truth for the compute rasterizer's resource
// layout. The GLSL sources in src/GPU3D_Vulkan_shaders/ declare exactly these
// set/binding numbers and types, and the feature probe derives its descriptor
// limit checks from the tables below rather than from hand-written numbers, so
// a change here propagates to both without a second edit.
//
// Two sets, split by update frequency, which is what descriptor sets are for:
//   set 0 changes at most once per frame (all the rasterizer's own buffers),
//   set 1 changes whenever the bound DS texture changes, which can be many
//   times per frame.
// Putting them in one set would force a full rewrite of the twelve set-0
// descriptors on every texture switch.
// ---------------------------------------------------------------------------

// Set 0 -- per-frame rasterizer resources.
enum class RasterizerBinding : u32
{
    MetaUniform     = 0,    // UNIFORM_BUFFER
    PolygonBuffer   = 1,    // STORAGE_BUFFER, readonly
    XSpanSetups     = 2,    // STORAGE_BUFFER
    YSpanSetups     = 3,    // STORAGE_BUFFER
    ColorTiles      = 4,    // STORAGE_BUFFER
    DepthTiles      = 5,    // STORAGE_BUFFER
    AttrTiles       = 6,    // STORAGE_BUFFER
    ResultBuffer    = 7,    // STORAGE_BUFFER
    BinResultBuffer = 8,    // STORAGE_BUFFER
    WorkDescBuffer  = 9,    // STORAGE_BUFFER
    SetupIndices    = 10,   // UNIFORM_TEXEL_BUFFER, VK_FORMAT_R16G16B16A16_UINT
    FinalFB         = 11,   // STORAGE_IMAGE, VK_FORMAT_R8G8B8A8_UNORM

    // --- presentation stages (Resolve / Compositor) ------------------------
    // Appended after the rasterizer's own bindings; nothing above ever moved.
    // Both are only read/written by the two presentation pipelines, but they
    // live in set 0 because the whole backend shares one VkPipelineLayout and a
    // second layout would mean a second copy of every rasterizer binding.
    StructuredInput = 12,   // STORAGE_BUFFER, readonly: the software renderer's
                            // structured 2D planes + per-scanline metadata
    PresentationOut = 13,   // STORAGE_BUFFER: Resolve writes the native-
                            // resolution capture image here, Compositor the
                            // two-screen internal-resolution BGRA output. One
                            // binding, two buffers, selected by which
                            // descriptor set the host binds.
    CaptureSidecar = 14,    // STORAGE_BUFFER: two high-resolution versions of
                            // each LCDC capture bank, persistent across frames
    BlendState = 15,        // STORAGE_BUFFER: stencil/shadow continuation state
                            // retained across bounded polygon batches
    ResultWinner = 16,      // STORAGE_BUFFER: polygon owning each result layer;
                            // used by native-resolution accepted-AA correction

    Count           = 17
};

// Set 1 -- texture resources, rebound when the active texture changes.
enum class TextureBinding : u32
{
    CurrentTexture    = 0,  // COMBINED_IMAGE_SAMPLER, usampler2DArray
    Capture128Texture = 1,  // COMBINED_IMAGE_SAMPLER, sampler2DArray
    Capture256Texture = 2,  // COMBINED_IMAGE_SAMPLER, sampler2DArray
    ClearBitmapColor  = 3,  // COMBINED_IMAGE_SAMPLER, usampler2D
    ClearBitmapDepth  = 4,  // COMBINED_IMAGE_SAMPLER, usampler2D

    Count             = 5
};

inline constexpr u32 RasterizerSetIndex = 0;
inline constexpr u32 TextureSetIndex = 1;
inline constexpr u32 DescriptorSetCount = 2;

inline constexpr size_t RasterizerBindingCount = static_cast<size_t>(RasterizerBinding::Count);
inline constexpr size_t TextureBindingCount = static_cast<size_t>(TextureBinding::Count);

// Descriptor type per binding, indexed by the enum value. The order must match
// the enums above; the static_asserts below pin the entries that are easiest
// to get wrong in a future edit.
inline constexpr std::array<VkDescriptorType, RasterizerBindingCount> RasterizerBindingTypes = {
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          // 0  MetaUniform
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 1  PolygonBuffer
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 2  XSpanSetups
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 3  YSpanSetups
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 4  ColorTiles
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 5  DepthTiles
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 6  AttrTiles
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 7  ResultBuffer
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 8  BinResultBuffer
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 9  WorkDescBuffer
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,    // 10 SetupIndices
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,           // 11 FinalFB
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 12 StructuredInput
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 13 PresentationOut
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 14 CaptureSidecar
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 15 BlendState
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          // 16 ResultWinner
};

inline constexpr std::array<VkDescriptorType, TextureBindingCount> TextureBindingTypes = {
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
};

static_assert(RasterizerBindingTypes[static_cast<size_t>(RasterizerBinding::MetaUniform)]
    == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "MetaUniform must stay a uniform buffer");
static_assert(RasterizerBindingTypes[static_cast<size_t>(RasterizerBinding::SetupIndices)]
    == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, "SetupIndices must stay a uniform texel buffer");
static_assert(RasterizerBindingTypes[static_cast<size_t>(RasterizerBinding::FinalFB)]
    == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "FinalFB must stay a storage image");
static_assert(RasterizerBindingTypes[static_cast<size_t>(RasterizerBinding::PresentationOut)]
    == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    "PresentationOut must stay a storage buffer: Resolve and Compositor both write it directly");

// Formats fixed by the contract. The feature probe verifies both against
// VkFormatProperties before a device is declared eligible.
inline constexpr VkFormat SetupIndicesFormat = VK_FORMAT_R16G16B16A16_UINT;
inline constexpr VkFormat FinalFramebufferFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Counts a descriptor type across one set layout. constexpr so the probe and
// the pool sizing both read the same numbers with no runtime cost.
template <size_t N>
[[nodiscard]] constexpr u32 CountDescriptorType(
    const std::array<VkDescriptorType, N>& types, VkDescriptorType type) noexcept
{
    u32 count = 0;
    for (size_t i = 0; i < N; i++)
    {
        if (types[i] == type)
            count++;
    }
    return count;
}

// Per-stage descriptor demand of the compute rasterizer. Both sets are visible
// to the same (compute) stage, so per-stage demand is the sum of the two.
namespace DescriptorDemand
{
    inline constexpr u32 UniformBuffers =
        CountDescriptorType(RasterizerBindingTypes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    inline constexpr u32 StorageBuffers =
        CountDescriptorType(RasterizerBindingTypes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    inline constexpr u32 StorageImages =
        CountDescriptorType(RasterizerBindingTypes, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    inline constexpr u32 UniformTexelBuffers =
        CountDescriptorType(RasterizerBindingTypes, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    inline constexpr u32 CombinedImageSamplers =
        CountDescriptorType(TextureBindingTypes, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    // VkPhysicalDeviceLimits::maxPerStageDescriptorSampledImages counts
    // SAMPLED_IMAGE, COMBINED_IMAGE_SAMPLER *and* UNIFORM_TEXEL_BUFFER
    // descriptors together, so SetupIndices is charged here too.
    inline constexpr u32 SampledImages = CombinedImageSamplers + UniformTexelBuffers;

    // maxPerStageDescriptorSamplers counts the sampler half of every
    // COMBINED_IMAGE_SAMPLER.
    inline constexpr u32 Samplers = CombinedImageSamplers;
}

// Push constant block, compute stage, 32 bytes. Laid out to match the GLSL
// declaration exactly: all members are 4-byte scalars, so std430 and the C++
// layout agree with no padding beyond the explicit tail word.
struct RasterizerPushConstants
{
    u32 CurVariant;
    u32 TexWidth;
    u32 TexHeight;
    u32 TexWrapS;
    u32 TexWrapT;
    u32 TexIsCapture;
    s32 CaptureYOffset;
    u32 Padding;        // keeps the block at the 32-byte size the layout declares
};
static_assert(sizeof(RasterizerPushConstants) == 32,
    "Push constant block must stay 32 bytes: the pipeline layout and the shaders both declare that size");
static_assert(alignof(RasterizerPushConstants) == 4,
    "Push constant block must not gain alignment padding");

inline constexpr u32 PushConstantSize = static_cast<u32>(sizeof(RasterizerPushConstants));


// ---------------------------------------------------------------------------
// Layout + pool objects
// ---------------------------------------------------------------------------

// The two set layouts and the pipeline layout that binds them. Immutable for
// the lifetime of the renderer, so it is created once and shared by every
// compute pipeline.
class DescriptorLayouts
{
public:
    DescriptorLayouts() = default;
    ~DescriptorLayouts();

    DescriptorLayouts(const DescriptorLayouts&) = delete;
    DescriptorLayouts& operator=(const DescriptorLayouts&) = delete;

    bool Create(const DeviceDispatch& fns, VkDevice device);
    void Destroy();

    [[nodiscard]] VkDescriptorSetLayout GetRasterizerLayout() const noexcept { return RasterizerLayout; }
    [[nodiscard]] VkDescriptorSetLayout GetTextureLayout() const noexcept { return TextureLayout; }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return PipelineLayout; }
    [[nodiscard]] bool IsValid() const noexcept { return PipelineLayout != VK_NULL_HANDLE; }

private:
    const DeviceDispatch* Fns = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout RasterizerLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout TextureLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
};

// How many sets of each kind the renderer needs alive at once. Every pool size
// is computed from these plus the binding tables above -- there is no constant
// in the pool code that is not traceable to a real descriptor count.
struct DescriptorPoolSizing
{
    // Frames the CPU may be ahead of the GPU. A set written for frame N must
    // stay untouched until frame N's fence signals, so every per-frame set is
    // multiplied by this.
    u32 FramesInFlight = 0;

    // Set-0 allocations per frame. One is the normal case (set 0 is written at
    // most once per frame); the field exists so a future pass that needs a
    // second rasterizer set does not have to touch the pool math.
    u32 RasterizerSetsPerFrame = 1;

    // Set-1 allocations per frame. The rasterizer rebinds set 1 whenever the
    // active DS texture changes, and each distinct binding needs its own set
    // for the whole frame because the GPU has not consumed the earlier ones
    // yet. Sized by the caller from the renderer's worst-case texture count.
    u32 TextureSetsPerFrame = 0;

    [[nodiscard]] u32 TotalRasterizerSets() const noexcept { return FramesInFlight * RasterizerSetsPerFrame; }
    [[nodiscard]] u32 TotalTextureSets() const noexcept { return FramesInFlight * TextureSetsPerFrame; }
    [[nodiscard]] u32 TotalSets() const noexcept { return TotalRasterizerSets() + TotalTextureSets(); }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return FramesInFlight > 0 && RasterizerSetsPerFrame > 0 && TextureSetsPerFrame > 0;
    }
};

// Owns one VkDescriptorPool sized for the whole renderer, plus the sets
// pre-allocated out of it.
//
// The pool is created *without*
// VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT: individual sets are never
// freed, only the whole pool at shutdown. That removes the pool fragmentation
// failure mode (VK_ERROR_FRAGMENTED_POOL) entirely, and vkResetDescriptorPool
// is not used per frame either -- the sets are allocated once and rewritten,
// which is legal as long as no in-flight frame still references them.
class DescriptorPool
{
public:
    DescriptorPool() = default;
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    bool Create(const DeviceDispatch& fns, VkDevice device,
                const DescriptorLayouts& layouts, const DescriptorPoolSizing& sizing);
    void Destroy();

    [[nodiscard]] bool IsValid() const noexcept { return Pool != VK_NULL_HANDLE; }

    // Set 0 for `frameIndex`. Valid for frameIndex < FramesInFlight.
    [[nodiscard]] VkDescriptorSet GetRasterizerSet(u32 frameIndex, u32 slot = 0) const noexcept;

    // Set 1 number `slot` for `frameIndex`. Valid for slot < TextureSetsPerFrame.
    [[nodiscard]] VkDescriptorSet GetTextureSet(u32 frameIndex, u32 slot) const noexcept;

    [[nodiscard]] const DescriptorPoolSizing& GetSizing() const noexcept { return Sizing; }

private:
    const DeviceDispatch* Fns = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    DescriptorPoolSizing Sizing{};
    std::vector<VkDescriptorSet> RasterizerSets;
    std::vector<VkDescriptorSet> TextureSets;
};

// Accumulates VkWriteDescriptorSet entries and submits them in one
// vkUpdateDescriptorSets call.
//
// Batching is not just tidiness: the pointed-to VkDescriptorBufferInfo /
// VkDescriptorImageInfo structs must stay alive until the update executes, so
// the writer owns them in fixed-capacity storage and refuses to grow. A
// reallocation would invalidate the pointers already stored in earlier writes,
// which is exactly the kind of bug that shows up as a random wrong texture
// three frames later.
class DescriptorWriter
{
public:
    // Enough for one full set-0 write (12) plus one full set-1 write (5) in a
    // single batch, which is the largest batch the renderer ever issues.
    static constexpr size_t MaxWrites = RasterizerBindingCount + TextureBindingCount;

    void Reset() noexcept;

    // Each returns false when the writer is full; the caller must Flush() and
    // retry rather than silently dropping a binding.
    bool WriteBuffer(VkDescriptorSet set, u32 binding, VkDescriptorType type,
                     VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) noexcept;
    bool WriteTexelBuffer(VkDescriptorSet set, u32 binding, VkBufferView view) noexcept;
    bool WriteImage(VkDescriptorSet set, u32 binding, VkDescriptorType type,
                    VkSampler sampler, VkImageView view, VkImageLayout layout) noexcept;

    // Applies every queued write. No-op when empty.
    void Flush(const DeviceDispatch& fns, VkDevice device) noexcept;

    [[nodiscard]] size_t GetCount() const noexcept { return Count; }

private:
    std::array<VkWriteDescriptorSet, MaxWrites> Writes{};
    std::array<VkDescriptorBufferInfo, MaxWrites> BufferInfos{};
    std::array<VkDescriptorImageInfo, MaxWrites> ImageInfos{};
    std::array<VkBufferView, MaxWrites> TexelViews{};
    size_t Count = 0;
};

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_DESCRIPTORS_H
