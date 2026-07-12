#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace melonDS::Vulkan
{

// MELONPRIME_VULKAN_TEXCACHE_RESIDENCY_CONTRACT_V1
inline constexpr std::uint32_t kTextureCacheResidencyContractVersion = 1;
inline constexpr std::uint32_t kTextureCacheSamplerCount = 9;

enum class VulkanTextureSamplerAxisMode : std::uint32_t
{
    Clamp = 0,
    Repeat = 1,
    Mirror = 2,
};

struct VulkanTextureCacheKey
{
    std::uint32_t TexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Format = 0;
    std::uint32_t TextureAddress = 0;
    std::uint32_t PaletteAddress = 0;
    std::uint32_t Color0Transparent = 0;
};

static_assert(sizeof(VulkanTextureCacheKey) == 32);
static_assert(offsetof(VulkanTextureCacheKey, TextureAddress) == 20);

inline bool operator==(const VulkanTextureCacheKey& left,
                       const VulkanTextureCacheKey& right) noexcept
{
    return left.TexParam == right.TexParam &&
        left.TexPalette == right.TexPalette &&
        left.Width == right.Width &&
        left.Height == right.Height &&
        left.Format == right.Format &&
        left.TextureAddress == right.TextureAddress &&
        left.PaletteAddress == right.PaletteAddress &&
        left.Color0Transparent == right.Color0Transparent;
}

struct VulkanTextureCacheRequest
{
    std::uint32_t TexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint64_t ContentGeneration = 0;
    std::uint32_t SourceOrder = 0;
};

struct VulkanTextureCacheEntry
{
    VulkanTextureCacheKey Key{};
    std::uint64_t ContentGeneration = 0;
    std::uint64_t LastUseSerial = 0;
    std::uint32_t UploadCount = 0;
    std::uint32_t ImageSlot = 0;
    bool Resident = false;
};

struct VulkanTextureDescriptorSlot
{
    std::uint32_t EntryIndex = 0;
    std::uint32_t SamplerIndex = 0;
};

struct VulkanTextureCacheDecision
{
    std::uint32_t SourceOrder = 0;
    std::uint32_t EntryIndex = 0;
    std::uint32_t DescriptorSlot = 0;
    std::uint32_t SamplerIndex = 0;
    bool CacheHit = false;
    bool UploadRequired = false;
    bool Invalidated = false;
};

struct VulkanTextureCachePlan
{
    std::vector<VulkanTextureCacheEntry> Entries;
    std::vector<VulkanTextureDescriptorSlot> DescriptorSlots;
    std::vector<VulkanTextureCacheDecision> Decisions;
    std::uint32_t CacheHitCount = 0;
    std::uint32_t CacheMissCount = 0;
    std::uint32_t UploadCount = 0;
    std::uint32_t InvalidationCount = 0;
    bool SourceOrderPreserved = false;
    bool NonAdjacentReuseObserved = false;
    bool DescriptorReuseObserved = false;
    bool UnchangedTextureNotReuploaded = false;

    void Clear() noexcept
    {
        Entries.clear();
        DescriptorSlots.clear();
        Decisions.clear();
        CacheHitCount = 0;
        CacheMissCount = 0;
        UploadCount = 0;
        InvalidationCount = 0;
        SourceOrderPreserved = false;
        NonAdjacentReuseObserved = false;
        DescriptorReuseObserved = false;
        UnchangedTextureNotReuploaded = false;
    }
};

inline std::uint32_t TextureCacheWidth(std::uint32_t texParam) noexcept
{
    return 8u << ((texParam >> 20) & 0x7u);
}

inline std::uint32_t TextureCacheHeight(std::uint32_t texParam) noexcept
{
    return 8u << ((texParam >> 23) & 0x7u);
}

inline VulkanTextureSamplerAxisMode DecodeVulkanTextureSamplerAxis(
    std::uint32_t texParam, bool vertical) noexcept
{
    const std::uint32_t repeatBit = vertical ? 17u : 16u;
    const std::uint32_t mirrorBit = vertical ? 19u : 18u;
    if ((texParam & (1u << repeatBit)) == 0)
        return VulkanTextureSamplerAxisMode::Clamp;
    return (texParam & (1u << mirrorBit)) != 0
        ? VulkanTextureSamplerAxisMode::Mirror
        : VulkanTextureSamplerAxisMode::Repeat;
}

inline std::uint32_t VulkanTextureSamplerTableIndex(
    VulkanTextureSamplerAxisMode s,
    VulkanTextureSamplerAxisMode t) noexcept
{
    return static_cast<std::uint32_t>(s) * 3u +
        static_cast<std::uint32_t>(t);
}

inline VulkanTextureCacheKey BuildVulkanTextureCacheKey(
    std::uint32_t texParam, std::uint32_t texPalette) noexcept
{
    VulkanTextureCacheKey key;
    key.TexParam = texParam & ~0xC00F0000u;
    key.Format = (key.TexParam >> 26) & 0x7u;
    if (key.Format == 5u)
        key.TexParam &= ~(1u << 29);
    key.TexPalette = key.Format == 7u ? 0u : texPalette;
    key.Width = TextureCacheWidth(key.TexParam);
    key.Height = TextureCacheHeight(key.TexParam);
    key.TextureAddress = (key.TexParam & 0xFFFFu) * 8u;
    if (key.Format == 7u)
    {
        key.PaletteAddress = 0;
    }
    else
    {
        std::uint32_t paletteAddress = texPalette * 16u;
        if (key.Format == 2u)
            paletteAddress >>= 1;
        key.PaletteAddress = paletteAddress & 0x1FFFFu;
    }
    key.Color0Transparent =
        key.Format >= 2u && key.Format <= 4u &&
        (key.TexParam & (1u << 29)) != 0 ? 1u : 0u;
    return key;
}

inline bool BuildVulkanTextureCachePlan(
    const std::vector<VulkanTextureCacheRequest>& requests,
    VulkanTextureCachePlan& plan,
    std::string* failureReason = nullptr)
{
    plan.Clear();
    std::vector<std::uint32_t> previousDecisionForEntry;
    std::vector<std::uint32_t> previousDecisionForDescriptor;
    std::uint64_t serial = 1;

    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
    {
        const auto& request = requests[requestIndex];
        const VulkanTextureCacheKey key = BuildVulkanTextureCacheKey(
            request.TexParam, request.TexPalette);
        if (key.Format == 0u)
        {
            if (failureReason)
                *failureReason = "texture format zero is not cacheable";
            plan.Clear();
            return false;
        }

        std::uint32_t entryIndex = static_cast<std::uint32_t>(plan.Entries.size());
        for (std::uint32_t i = 0; i < plan.Entries.size(); ++i)
        {
            if (plan.Entries[i].Key == key)
            {
                entryIndex = i;
                break;
            }
        }

        bool cacheHit = false;
        bool uploadRequired = false;
        bool invalidated = false;
        if (entryIndex == plan.Entries.size())
        {
            VulkanTextureCacheEntry entry;
            entry.Key = key;
            entry.ContentGeneration = request.ContentGeneration;
            entry.LastUseSerial = serial;
            entry.UploadCount = 1;
            entry.ImageSlot = entryIndex;
            entry.Resident = true;
            plan.Entries.push_back(entry);
            previousDecisionForEntry.push_back(
                static_cast<std::uint32_t>(requestIndex));
            uploadRequired = true;
            ++plan.CacheMissCount;
            ++plan.UploadCount;
        }
        else
        {
            auto& entry = plan.Entries[entryIndex];
            const std::uint32_t previous = previousDecisionForEntry[entryIndex];
            if (entry.ContentGeneration != request.ContentGeneration)
            {
                entry.ContentGeneration = request.ContentGeneration;
                ++entry.UploadCount;
                uploadRequired = true;
                invalidated = true;
                ++plan.CacheMissCount;
                ++plan.UploadCount;
                ++plan.InvalidationCount;
            }
            else
            {
                cacheHit = true;
                ++plan.CacheHitCount;
                plan.UnchangedTextureNotReuploaded = true;
                if (requestIndex > static_cast<std::size_t>(previous + 1u))
                    plan.NonAdjacentReuseObserved = true;
            }
            entry.LastUseSerial = serial;
            previousDecisionForEntry[entryIndex] =
                static_cast<std::uint32_t>(requestIndex);
        }

        const auto s = DecodeVulkanTextureSamplerAxis(request.TexParam, false);
        const auto t = DecodeVulkanTextureSamplerAxis(request.TexParam, true);
        const std::uint32_t samplerIndex = VulkanTextureSamplerTableIndex(s, t);
        std::uint32_t descriptorSlot =
            static_cast<std::uint32_t>(plan.DescriptorSlots.size());
        for (std::uint32_t i = 0; i < plan.DescriptorSlots.size(); ++i)
        {
            const auto& slot = plan.DescriptorSlots[i];
            if (slot.EntryIndex == entryIndex && slot.SamplerIndex == samplerIndex)
            {
                descriptorSlot = i;
                if (previousDecisionForDescriptor[i] + 1u != requestIndex)
                    plan.DescriptorReuseObserved = true;
                previousDecisionForDescriptor[i] =
                    static_cast<std::uint32_t>(requestIndex);
                break;
            }
        }
        if (descriptorSlot == plan.DescriptorSlots.size())
        {
            plan.DescriptorSlots.push_back({entryIndex, samplerIndex});
            previousDecisionForDescriptor.push_back(
                static_cast<std::uint32_t>(requestIndex));
        }

        plan.Decisions.push_back({
            request.SourceOrder,
            entryIndex,
            descriptorSlot,
            samplerIndex,
            cacheHit,
            uploadRequired,
            invalidated,
        });
        ++serial;
    }

    plan.SourceOrderPreserved = plan.Decisions.size() == requests.size();
    for (std::size_t i = 0; i < requests.size() && plan.SourceOrderPreserved; ++i)
        plan.SourceOrderPreserved =
            plan.Decisions[i].SourceOrder == requests[i].SourceOrder;
    return true;
}

} // namespace melonDS::Vulkan
