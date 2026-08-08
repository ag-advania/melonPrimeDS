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

#include "VulkanDescriptors.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

namespace melonDS::Vk
{

namespace
{

// Builds the VkDescriptorSetLayoutBinding array for one of the two contracts.
// Every binding is a single descriptor visible to the compute stage only:
// nothing in this backend samples or writes these resources from a graphics
// stage, and narrowing stageFlags lets the driver skip pushing them to stages
// that never read them.
template <size_t N>
void BuildBindings(
    const std::array<VkDescriptorType, N>& types,
    std::array<VkDescriptorSetLayoutBinding, N>& out) noexcept
{
    for (size_t i = 0; i < N; i++)
    {
        out[i] = VkDescriptorSetLayoutBinding{};
        out[i].binding = static_cast<u32>(i);
        out[i].descriptorType = types[i];
        out[i].descriptorCount = 1;
        out[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        // pImmutableSamplers stays null: the DS texture sampler changes with
        // the polygon's wrap mode, so the sampler must remain part of the
        // per-binding write rather than being baked into the layout.
        out[i].pImmutableSamplers = nullptr;
    }
}

// Appends a pool size entry when the count is non-zero. A VkDescriptorPoolSize
// with descriptorCount == 0 is invalid per the spec, so empty categories are
// dropped rather than passed through.
void AppendPoolSize(std::vector<VkDescriptorPoolSize>& sizes, VkDescriptorType type, u32 count)
{
    if (count == 0)
        return;

    VkDescriptorPoolSize entry{};
    entry.type = type;
    entry.descriptorCount = count;
    sizes.push_back(entry);
}

} // namespace


// ---------------------------------------------------------------------------
// DescriptorLayouts
// ---------------------------------------------------------------------------

DescriptorLayouts::~DescriptorLayouts()
{
    Destroy();
}

bool DescriptorLayouts::Create(const DeviceDispatch& fns, VkDevice device)
{
    Destroy();

    if (device == VK_NULL_HANDLE)
        return false;

    Fns = &fns;
    Device = device;

    std::array<VkDescriptorSetLayoutBinding, RasterizerBindingCount> rasterizerBindings{};
    BuildBindings(RasterizerBindingTypes, rasterizerBindings);

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<u32>(rasterizerBindings.size());
    info.pBindings = rasterizerBindings.data();

    VkResult res = fns.CreateDescriptorSetLayout(device, &info, nullptr, &RasterizerLayout);
    if (!MELONPRIME_VK_CHECK("vkCreateDescriptorSetLayout (rasterizer set)", res))
    {
        RasterizerLayout = VK_NULL_HANDLE;
        Destroy();
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, TextureBindingCount> textureBindings{};
    BuildBindings(TextureBindingTypes, textureBindings);

    info.bindingCount = static_cast<u32>(textureBindings.size());
    info.pBindings = textureBindings.data();

    res = fns.CreateDescriptorSetLayout(device, &info, nullptr, &TextureLayout);
    if (!MELONPRIME_VK_CHECK("vkCreateDescriptorSetLayout (texture set)", res))
    {
        TextureLayout = VK_NULL_HANDLE;
        Destroy();
        return false;
    }

    // Set order in the pipeline layout is the set index; it must match the
    // `layout(set = N)` decorations in the GLSL sources.
    const VkDescriptorSetLayout setLayouts[DescriptorSetCount] = {
        RasterizerLayout,
        TextureLayout,
    };
    static_assert(RasterizerSetIndex == 0 && TextureSetIndex == 1,
        "pipeline layout set order must match the descriptor set indices");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = PushConstantSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = DescriptorSetCount;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    res = fns.CreatePipelineLayout(device, &layoutInfo, nullptr, &PipelineLayout);
    if (!MELONPRIME_VK_CHECK("vkCreatePipelineLayout", res))
    {
        PipelineLayout = VK_NULL_HANDLE;
        Destroy();
        return false;
    }

    return true;
}

void DescriptorLayouts::Destroy()
{
    if (!Fns || Device == VK_NULL_HANDLE)
    {
        RasterizerLayout = VK_NULL_HANDLE;
        TextureLayout = VK_NULL_HANDLE;
        PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    // Reverse creation order: the pipeline layout references both set layouts.
    // Vulkan does not actually require this ordering (set layouts may be
    // destroyed while a pipeline layout still references them), but keeping it
    // means the destruction sequence reads the same as the creation sequence.
    if (PipelineLayout != VK_NULL_HANDLE)
    {
        Fns->DestroyPipelineLayout(Device, PipelineLayout, nullptr);
        PipelineLayout = VK_NULL_HANDLE;
    }
    if (TextureLayout != VK_NULL_HANDLE)
    {
        Fns->DestroyDescriptorSetLayout(Device, TextureLayout, nullptr);
        TextureLayout = VK_NULL_HANDLE;
    }
    if (RasterizerLayout != VK_NULL_HANDLE)
    {
        Fns->DestroyDescriptorSetLayout(Device, RasterizerLayout, nullptr);
        RasterizerLayout = VK_NULL_HANDLE;
    }

    Fns = nullptr;
    Device = VK_NULL_HANDLE;
}


// ---------------------------------------------------------------------------
// DescriptorPool
// ---------------------------------------------------------------------------

DescriptorPool::~DescriptorPool()
{
    Destroy();
}

bool DescriptorPool::Create(
    const DeviceDispatch& fns, VkDevice device,
    const DescriptorLayouts& layouts, const DescriptorPoolSizing& sizing)
{
    Destroy();

    if (device == VK_NULL_HANDLE || !layouts.IsValid())
        return false;

    if (!sizing.IsValid())
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] descriptor pool sizing is incomplete "
            "(framesInFlight=%u rasterizerSets=%u textureSets=%u)\n",
            sizing.FramesInFlight, sizing.RasterizerSetsPerFrame, sizing.TextureSetsPerFrame);
        return false;
    }

    Fns = &fns;
    Device = device;
    Sizing = sizing;

    const u32 rasterizerSets = sizing.TotalRasterizerSets();
    const u32 textureSets = sizing.TotalTextureSets();

    // Every count below is (descriptors of that type in the layout) x (number
    // of sets of that layout the pool must hold). No rounded-up guesses: an
    // undersized pool fails at vkAllocateDescriptorSets, and an oversized one
    // wastes memory that at 16x is already scarce.
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(5);
    AppendPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        DescriptorDemand::UniformBuffers * rasterizerSets);
    AppendPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        DescriptorDemand::StorageBuffers * rasterizerSets);
    AppendPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        DescriptorDemand::UniformTexelBuffers * rasterizerSets);
    AppendPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        DescriptorDemand::StorageImages * rasterizerSets);
    AppendPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        DescriptorDemand::CombinedImageSamplers * textureSets);

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // No VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT: sets are never
    // individually freed, so the pool cannot fragment and vkFreeDescriptorSets
    // is never called on it.
    info.flags = 0;
    info.maxSets = sizing.TotalSets();
    info.poolSizeCount = static_cast<u32>(poolSizes.size());
    info.pPoolSizes = poolSizes.data();

    VkResult res = fns.CreateDescriptorPool(device, &info, nullptr, &Pool);
    if (!MELONPRIME_VK_CHECK("vkCreateDescriptorPool", res))
    {
        Pool = VK_NULL_HANDLE;
        Destroy();
        return false;
    }

    // Allocate every set up front. Doing it once removes the per-frame
    // allocation from the hot path entirely and turns "the pool ran out" from
    // a mid-frame failure into a startup failure.
    {
        std::vector<VkDescriptorSetLayout> layoutList(rasterizerSets, layouts.GetRasterizerLayout());
        RasterizerSets.resize(rasterizerSets);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = Pool;
        allocInfo.descriptorSetCount = rasterizerSets;
        allocInfo.pSetLayouts = layoutList.data();

        res = fns.AllocateDescriptorSets(device, &allocInfo, RasterizerSets.data());
        if (!MELONPRIME_VK_CHECK("vkAllocateDescriptorSets (rasterizer sets)", res))
        {
            Destroy();
            return false;
        }
    }

    {
        std::vector<VkDescriptorSetLayout> layoutList(textureSets, layouts.GetTextureLayout());
        TextureSets.resize(textureSets);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = Pool;
        allocInfo.descriptorSetCount = textureSets;
        allocInfo.pSetLayouts = layoutList.data();

        res = fns.AllocateDescriptorSets(device, &allocInfo, TextureSets.data());
        if (!MELONPRIME_VK_CHECK("vkAllocateDescriptorSets (texture sets)", res))
        {
            Destroy();
            return false;
        }
    }

    Platform::Log(Platform::LogLevel::Debug,
        "[Vulkan] descriptor pool: %u sets (%u rasterizer, %u texture) across %u frames in flight\n",
        sizing.TotalSets(), rasterizerSets, textureSets, sizing.FramesInFlight);

    return true;
}

void DescriptorPool::Destroy()
{
    if (Fns && Device != VK_NULL_HANDLE && Pool != VK_NULL_HANDLE)
    {
        // Destroying the pool implicitly frees every set allocated from it, so
        // the set vectors only need clearing.
        Fns->DestroyDescriptorPool(Device, Pool, nullptr);
    }

    Pool = VK_NULL_HANDLE;
    RasterizerSets.clear();
    TextureSets.clear();
    Sizing = DescriptorPoolSizing{};
    Fns = nullptr;
    Device = VK_NULL_HANDLE;
}

VkDescriptorSet DescriptorPool::GetRasterizerSet(u32 frameIndex, u32 slot) const noexcept
{
    if (frameIndex >= Sizing.FramesInFlight || slot >= Sizing.RasterizerSetsPerFrame)
        return VK_NULL_HANDLE;

    const size_t index = static_cast<size_t>(frameIndex) * Sizing.RasterizerSetsPerFrame + slot;
    if (index >= RasterizerSets.size())
        return VK_NULL_HANDLE;

    return RasterizerSets[index];
}

VkDescriptorSet DescriptorPool::GetTextureSet(u32 frameIndex, u32 slot) const noexcept
{
    if (frameIndex >= Sizing.FramesInFlight || slot >= Sizing.TextureSetsPerFrame)
        return VK_NULL_HANDLE;

    const size_t index = static_cast<size_t>(frameIndex) * Sizing.TextureSetsPerFrame + slot;
    if (index >= TextureSets.size())
        return VK_NULL_HANDLE;

    return TextureSets[index];
}


// ---------------------------------------------------------------------------
// DescriptorWriter
// ---------------------------------------------------------------------------

void DescriptorWriter::Reset() noexcept
{
    Count = 0;
}

bool DescriptorWriter::WriteBuffer(
    VkDescriptorSet set, u32 binding, VkDescriptorType type,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) noexcept
{
    if (Count >= MaxWrites || set == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE)
        return false;

    const size_t index = Count;

    BufferInfos[index] = VkDescriptorBufferInfo{};
    BufferInfos[index].buffer = buffer;
    BufferInfos[index].offset = offset;
    BufferInfos[index].range = range;

    Writes[index] = VkWriteDescriptorSet{};
    Writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[index].dstSet = set;
    Writes[index].dstBinding = binding;
    Writes[index].dstArrayElement = 0;
    Writes[index].descriptorCount = 1;
    Writes[index].descriptorType = type;
    // Points into this object's own storage, which is why the writer refuses to
    // grow: a reallocation would leave these pointers dangling.
    Writes[index].pBufferInfo = &BufferInfos[index];

    Count++;
    return true;
}

bool DescriptorWriter::WriteTexelBuffer(VkDescriptorSet set, u32 binding, VkBufferView view) noexcept
{
    if (Count >= MaxWrites || set == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
        return false;

    const size_t index = Count;

    TexelViews[index] = view;

    Writes[index] = VkWriteDescriptorSet{};
    Writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[index].dstSet = set;
    Writes[index].dstBinding = binding;
    Writes[index].dstArrayElement = 0;
    Writes[index].descriptorCount = 1;
    Writes[index].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    Writes[index].pTexelBufferView = &TexelViews[index];

    Count++;
    return true;
}

bool DescriptorWriter::WriteImage(
    VkDescriptorSet set, u32 binding, VkDescriptorType type,
    VkSampler sampler, VkImageView view, VkImageLayout layout) noexcept
{
    if (Count >= MaxWrites || set == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
        return false;

    // A combined image sampler without a sampler is a validation error rather
    // than a degraded binding, so it is rejected here where the caller can
    // still react.
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && sampler == VK_NULL_HANDLE)
        return false;

    const size_t index = Count;

    ImageInfos[index] = VkDescriptorImageInfo{};
    ImageInfos[index].sampler = sampler;
    ImageInfos[index].imageView = view;
    ImageInfos[index].imageLayout = layout;

    Writes[index] = VkWriteDescriptorSet{};
    Writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[index].dstSet = set;
    Writes[index].dstBinding = binding;
    Writes[index].dstArrayElement = 0;
    Writes[index].descriptorCount = 1;
    Writes[index].descriptorType = type;
    Writes[index].pImageInfo = &ImageInfos[index];

    Count++;
    return true;
}

void DescriptorWriter::Flush(const DeviceDispatch& fns, VkDevice device) noexcept
{
    if (Count == 0 || device == VK_NULL_HANDLE)
    {
        Count = 0;
        return;
    }

    fns.UpdateDescriptorSets(device, static_cast<u32>(Count), Writes.data(), 0, nullptr);
    Count = 0;
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
