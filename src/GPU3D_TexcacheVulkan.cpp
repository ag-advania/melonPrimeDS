#include "GPU3D_TexcacheVulkan.h"

#include <algorithm>
#include <array>
#include <limits>

namespace melonDS::Vulkan
{
namespace
{

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint8_t ReadWrapped8(const std::uint8_t* data, std::size_t size,
                          std::uint64_t address) noexcept
{
    return size == 0 || data == nullptr ? 0 : data[address % size];
}

std::uint16_t ReadWrapped16(const std::uint8_t* data, std::size_t size,
                            std::uint64_t address) noexcept
{
    return static_cast<std::uint16_t>(ReadWrapped8(data, size, address)) |
        static_cast<std::uint16_t>(ReadWrapped8(data, size, address + 1u) << 8u);
}

std::uint32_t ReadWrapped32(const std::uint8_t* data, std::size_t size,
                            std::uint64_t address) noexcept
{
    return static_cast<std::uint32_t>(ReadWrapped16(data, size, address)) |
        (static_cast<std::uint32_t>(ReadWrapped16(data, size, address + 2u)) << 16u);
}

std::uint32_t ConvertRgb5ToRgb6(std::uint16_t value) noexcept
{
    std::uint32_t r = (value & 0x001Fu) << 1u;
    std::uint32_t g = (value & 0x03E0u) >> 4u;
    std::uint32_t b = (value & 0x7C00u) >> 9u;
    if (r != 0) ++r;
    if (g != 0) ++g;
    if (b != 0) ++b;
    return r | (g << 8u) | (b << 16u);
}

std::uint16_t AverageRgb5(std::uint16_t left, std::uint16_t right) noexcept
{
    const std::uint32_t r = ((left & 0x001Fu) + (right & 0x001Fu)) >> 1u;
    const std::uint32_t g = (((left & 0x03E0u) + (right & 0x03E0u)) >> 1u) & 0x03E0u;
    const std::uint32_t b = (((left & 0x7C00u) + (right & 0x7C00u)) >> 1u) & 0x7C00u;
    return static_cast<std::uint16_t>(r | g | b);
}

std::uint16_t WeightedRgb5(std::uint16_t left, std::uint16_t right,
                           std::uint32_t leftWeight) noexcept
{
    const std::uint32_t rightWeight = 8u - leftWeight;
    const std::uint32_t r = ((left & 0x001Fu) * leftWeight +
        (right & 0x001Fu) * rightWeight) >> 3u;
    const std::uint32_t g = (((left & 0x03E0u) * leftWeight +
        (right & 0x03E0u) * rightWeight) >> 3u) & 0x03E0u;
    const std::uint32_t b = (((left & 0x7C00u) * leftWeight +
        (right & 0x7C00u) * rightWeight) >> 3u) & 0x7C00u;
    return static_cast<std::uint16_t>(r | g | b);
}

std::uint64_t HashWrappedRange(const std::uint8_t* data, std::size_t size,
                               VulkanTextureMemorySpan span) noexcept
{
    std::uint64_t hash = kFnvOffset;
    for (std::uint64_t i = 0; i < span.Size; ++i)
    {
        hash ^= ReadWrapped8(data, size, static_cast<std::uint64_t>(span.Address) + i);
        hash *= kFnvPrime;
    }
    hash ^= span.Address;
    hash *= kFnvPrime;
    hash ^= span.Size;
    hash *= kFnvPrime;
    return hash;
}

std::uint64_t CombineHash(std::uint64_t hash, std::uint64_t value) noexcept
{
    for (std::uint32_t shift = 0; shift < 64; shift += 8)
    {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= kFnvPrime;
    }
    return hash;
}

bool SpanTouches(const VulkanTextureMemorySpan& span,
                 const std::vector<std::uint64_t>& words,
                 std::size_t memorySize) noexcept
{
    if (span.Size == 0 || memorySize == 0 || words.empty()) return false;
    const std::size_t pageCount =
        (memorySize + kTextureDirtyPageSize - 1u) / kTextureDirtyPageSize;
    if (pageCount == 0) return false;
    std::uint64_t current = span.Address % memorySize;
    std::uint64_t remaining = span.Size;
    while (remaining != 0)
    {
        const std::size_t page = static_cast<std::size_t>(current / kTextureDirtyPageSize);
        const std::size_t word = page >> 6u;
        const std::uint32_t bit = static_cast<std::uint32_t>(page & 63u);
        if (word < words.size() && (words[word] & (1ull << bit)) != 0) return true;
        const std::uint64_t pageEnd =
            std::min<std::uint64_t>(((current / kTextureDirtyPageSize) + 1u) *
                kTextureDirtyPageSize, memorySize);
        const std::uint64_t chunk = std::min<std::uint64_t>(remaining, pageEnd - current);
        remaining -= chunk;
        current = remaining == 0 ? current + chunk : (current + chunk) % memorySize;
    }
    return false;
}

bool DecodeIndexed(const VulkanTextureDecodeFootprint& footprint,
                   const VulkanTextureMemoryView& memory,
                   std::vector<std::uint32_t>& output,
                   std::uint32_t colorBits) noexcept
{
    const std::uint32_t width = footprint.Key.Width;
    const std::uint32_t height = footprint.Key.Height;
    const std::uint32_t pixelsPerWord = 16u / colorBits;
    const std::uint32_t groupsPerRow = width / pixelsPerWord;
    const std::uint32_t mask = (1u << colorBits) - 1u;
    const bool transparentZero = footprint.Key.Color0Transparent != 0;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t group = 0; group < groupsPerRow; ++group)
        {
            std::uint16_t packed = ReadWrapped16(memory.Texture, memory.TextureSize,
                footprint.TextureSpans[0].Address + 2u * (group + y * groupsPerRow));
            for (std::uint32_t i = 0; i < pixelsPerWord; ++i)
            {
                const std::uint32_t index = packed & mask;
                packed = static_cast<std::uint16_t>(packed >> colorBits);
                const std::uint16_t color = ReadWrapped16(memory.Palette, memory.PaletteSize,
                    footprint.PaletteSpan.Address + index * 2u);
                const std::uint32_t alpha = transparentZero && index == 0 ? 0u : 31u;
                output[group * pixelsPerWord + i + y * width] =
                    ConvertRgb5ToRgb6(color) | (alpha << 24u);
            }
        }
    }
    return true;
}

} // namespace

bool BuildVulkanTextureDecodeFootprint(
    const VulkanTextureCacheKey& key,
    VulkanTextureDecodeFootprint& footprint,
    std::string* failureReason) noexcept
{
    footprint = {};
    footprint.Key = key;
    footprint.OutputTexelCount = key.Width * key.Height;
    if (key.Format == 0 || key.Format > 7 || key.Width == 0 || key.Height == 0)
    {
        if (failureReason) *failureReason = "invalid Vulkan texture decode key";
        return false;
    }
    footprint.TextureSpanCount = 1;
    footprint.TextureSpans[0].Address = key.TextureAddress;
    switch (key.Format)
    {
    case 1:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount;
        footprint.PaletteSpan = {key.PaletteAddress, 32u * 2u};
        footprint.UsesPalette = true;
        break;
    case 2:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount / 4u;
        footprint.PaletteSpan = {key.PaletteAddress, 4u * 2u};
        footprint.UsesPalette = true;
        break;
    case 3:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount / 2u;
        footprint.PaletteSpan = {key.PaletteAddress, 16u * 2u};
        footprint.UsesPalette = true;
        break;
    case 4:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount;
        footprint.PaletteSpan = {key.PaletteAddress, 256u * 2u};
        footprint.UsesPalette = true;
        break;
    case 5:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount / 4u;
        footprint.TextureSpans[1].Address = 0x20000u +
            ((key.TextureAddress & 0x1FFFCu) >> 1u);
        if (key.TextureAddress >= 0x40000u)
            footprint.TextureSpans[1].Address += 0x10000u;
        footprint.TextureSpans[1].Size = footprint.OutputTexelCount / 8u;
        footprint.TextureSpanCount = 2;
        footprint.PaletteSpan = {key.PaletteAddress, 0x10000u};
        footprint.UsesPalette = true;
        footprint.UsesAuxiliaryTexture = true;
        break;
    case 6:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount;
        footprint.PaletteSpan = {key.PaletteAddress, 8u * 2u};
        footprint.UsesPalette = true;
        break;
    case 7:
        footprint.TextureSpans[0].Size = footprint.OutputTexelCount * 2u;
        break;
    default:
        if (failureReason) *failureReason = "unsupported Vulkan texture format";
        return false;
    }
    return true;
}

bool DecodeVulkanTextureRgb6a5(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory,
    std::vector<std::uint32_t>& output,
    std::string* failureReason)
{
    if (memory.Texture == nullptr || memory.TextureSize == 0 ||
        (footprint.UsesPalette && (memory.Palette == nullptr || memory.PaletteSize == 0)))
    {
        if (failureReason) *failureReason = "texture decode memory view is incomplete";
        output.clear();
        return false;
    }
    output.assign(footprint.OutputTexelCount, 0u);
    const std::uint32_t width = footprint.Key.Width;
    const std::uint32_t height = footprint.Key.Height;
    switch (footprint.Key.Format)
    {
    case 1:
        for (std::uint32_t i = 0; i < footprint.OutputTexelCount; ++i)
        {
            const std::uint8_t value = ReadWrapped8(memory.Texture, memory.TextureSize,
                footprint.TextureSpans[0].Address + i);
            const std::uint32_t index = value & 0x1Fu;
            const std::uint32_t alpha3 = value >> 5u;
            const std::uint32_t alpha5 = alpha3 * 4u + alpha3 / 2u;
            const std::uint16_t color = ReadWrapped16(memory.Palette, memory.PaletteSize,
                footprint.PaletteSpan.Address + index * 2u);
            output[i] = ConvertRgb5ToRgb6(color) | (alpha5 << 24u);
        }
        return true;
    case 2:
        return DecodeIndexed(footprint, memory, output, 2u);
    case 3:
        return DecodeIndexed(footprint, memory, output, 4u);
    case 4:
        return DecodeIndexed(footprint, memory, output, 8u);
    case 5:
        for (std::uint32_t blockY = 0; blockY < height / 4u; ++blockY)
        {
            for (std::uint32_t blockX = 0; blockX < width / 4u; ++blockX)
            {
                const std::uint32_t blockIndex = blockX + blockY * (width / 4u);
                const std::uint32_t data = ReadWrapped32(memory.Texture, memory.TextureSize,
                    footprint.TextureSpans[0].Address + blockIndex * 4u);
                const std::uint16_t aux = ReadWrapped16(memory.Texture, memory.TextureSize,
                    footprint.TextureSpans[1].Address + blockIndex * 2u);
                const std::uint32_t paletteOffset = footprint.PaletteSpan.Address +
                    (aux & 0x3FFFu) * 4u;
                std::array<std::uint16_t, 4> colors{{
                    static_cast<std::uint16_t>(ReadWrapped16(memory.Palette,
                        memory.PaletteSize, paletteOffset) | 0x8000u),
                    static_cast<std::uint16_t>(ReadWrapped16(memory.Palette,
                        memory.PaletteSize, paletteOffset + 2u) | 0x8000u),
                    static_cast<std::uint16_t>(ReadWrapped16(memory.Palette,
                        memory.PaletteSize, paletteOffset + 4u) | 0x8000u),
                    static_cast<std::uint16_t>(ReadWrapped16(memory.Palette,
                        memory.PaletteSize, paletteOffset + 6u) | 0x8000u),
                }};
                switch ((aux >> 14u) & 0x3u)
                {
                case 0:
                    colors[3] = 0;
                    break;
                case 1:
                    colors[2] = static_cast<std::uint16_t>(
                        AverageRgb5(colors[0], colors[1]) | 0x8000u);
                    colors[3] = 0;
                    break;
                case 2:
                    break;
                case 3:
                    colors[2] = static_cast<std::uint16_t>(
                        WeightedRgb5(colors[0], colors[1], 5u) | 0x8000u);
                    colors[3] = static_cast<std::uint16_t>(
                        WeightedRgb5(colors[0], colors[1], 3u) | 0x8000u);
                    break;
                }
                for (std::uint32_t y = 0; y < 4u; ++y)
                {
                    for (std::uint32_t x = 0; x < 4u; ++x)
                    {
                        const std::uint32_t index =
                            (data >> (2u * (x + y * 4u))) & 0x3u;
                        const std::uint16_t color = colors[index];
                        output[blockX * 4u + x + (blockY * 4u + y) * width] =
                            ConvertRgb5ToRgb6(color) |
                            ((color & 0x8000u) != 0 ? 0x1F000000u : 0u);
                    }
                }
            }
        }
        return true;
    case 6:
        for (std::uint32_t i = 0; i < footprint.OutputTexelCount; ++i)
        {
            const std::uint8_t value = ReadWrapped8(memory.Texture, memory.TextureSize,
                footprint.TextureSpans[0].Address + i);
            const std::uint32_t index = value & 0x7u;
            const std::uint32_t alpha = value >> 3u;
            const std::uint16_t color = ReadWrapped16(memory.Palette, memory.PaletteSize,
                footprint.PaletteSpan.Address + index * 2u);
            output[i] = ConvertRgb5ToRgb6(color) | (alpha << 24u);
        }
        return true;
    case 7:
        for (std::uint32_t i = 0; i < footprint.OutputTexelCount; ++i)
        {
            const std::uint16_t value = ReadWrapped16(memory.Texture, memory.TextureSize,
                footprint.TextureSpans[0].Address + i * 2u);
            output[i] = ConvertRgb5ToRgb6(value) |
                ((value & 0x8000u) != 0 ? 0x1F000000u : 0u);
        }
        return true;
    default:
        if (failureReason) *failureReason = "unsupported Vulkan texture decode format";
        output.clear();
        return false;
    }
}

VulkanTextureDecodeHashes HashVulkanTextureDecodeInput(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory) noexcept
{
    VulkanTextureDecodeHashes hashes;
    for (std::uint32_t i = 0; i < footprint.TextureSpanCount; ++i)
        hashes.Texture[i] = HashWrappedRange(memory.Texture, memory.TextureSize,
            footprint.TextureSpans[i]);
    if (footprint.UsesPalette)
        hashes.Palette = HashWrappedRange(memory.Palette, memory.PaletteSize,
            footprint.PaletteSpan);
    hashes.Combined = kFnvOffset;
    hashes.Combined = CombineHash(hashes.Combined, footprint.Key.Format);
    hashes.Combined = CombineHash(hashes.Combined, footprint.Key.Width);
    hashes.Combined = CombineHash(hashes.Combined, footprint.Key.Height);
    hashes.Combined = CombineHash(hashes.Combined, hashes.Texture[0]);
    hashes.Combined = CombineHash(hashes.Combined, hashes.Texture[1]);
    hashes.Combined = CombineHash(hashes.Combined, hashes.Palette);
    return hashes;
}

bool VulkanTextureDecodeTouchesDirtyPages(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory,
    const VulkanTextureDirtyPageSet& dirty) noexcept
{
    for (std::uint32_t i = 0; i < footprint.TextureSpanCount; ++i)
    {
        if (SpanTouches(footprint.TextureSpans[i], dirty.TextureWords,
                        memory.TextureSize))
            return true;
    }
    return footprint.UsesPalette &&
        SpanTouches(footprint.PaletteSpan, dirty.PaletteWords, memory.PaletteSize);
}

void MarkVulkanTextureDirtyPage(
    std::vector<std::uint64_t>& words,
    std::uint32_t byteAddress,
    std::size_t memorySize) noexcept
{
    if (memorySize == 0) return;
    const std::size_t pageCount =
        (memorySize + kTextureDirtyPageSize - 1u) / kTextureDirtyPageSize;
    const std::size_t page = (byteAddress % memorySize) / kTextureDirtyPageSize;
    const std::size_t wordCount = (pageCount + 63u) / 64u;
    if (words.size() < wordCount) words.resize(wordCount, 0);
    words[page >> 6u] |= 1ull << (page & 63u);
}


std::uint64_t AlignVulkanTextureUploadOffset(
    std::uint64_t value,
    std::uint64_t alignment) noexcept
{
    if (alignment <= 1u) return value;
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0u) return value;
    const std::uint64_t delta = alignment - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - delta)
        return std::numeric_limits<std::uint64_t>::max();
    return value + delta;
}

void VulkanTextureUploadRingState::Reset(
    const VulkanTextureUploadRingConfig& config) noexcept
{
    Config = config;
    InFlight.clear();
    Head = 0;
    LastRetiredSerial = 0;
    ReservationCount = 0;
    WrapCount = 0;
    ReuseCount = 0;
    RejectedOverlapCount = 0;
    PersistentMappingRequired = true;
    NonCoherentFlushRequired = true;
}

namespace
{

bool UploadRangesOverlap(
    std::uint64_t leftOffset,
    std::uint64_t leftSize,
    std::uint64_t rightOffset,
    std::uint64_t rightSize) noexcept
{
    if (leftSize == 0u || rightSize == 0u) return false;
    return leftOffset < rightOffset + rightSize &&
        rightOffset < leftOffset + leftSize;
}

bool UploadRangeAvailable(
    const VulkanTextureUploadRingState& state,
    std::uint64_t offset,
    std::uint64_t size) noexcept
{
    for (const auto& active : state.InFlight)
    {
        if (UploadRangesOverlap(offset, size, active.Offset, active.PaddedSize))
            return false;
    }
    return true;
}

} // namespace

bool ReserveVulkanTextureUpload(
    VulkanTextureUploadRingState& state,
    std::uint64_t size,
    std::uint64_t submissionSerial,
    VulkanTextureUploadReservation& reservation,
    std::string* failureReason) noexcept
{
    reservation = {};
    const std::uint64_t copyAlignment = std::max<std::uint64_t>(
        1u, state.Config.CopyAlignment);
    const std::uint64_t atomSize = std::max<std::uint64_t>(
        1u, state.Config.NonCoherentAtomSize);
    if (state.Config.Capacity == 0u || size == 0u)
    {
        if (failureReason) *failureReason = "upload ring capacity and size must be nonzero";
        return false;
    }
    if (submissionSerial == 0u || submissionSerial <= state.LastRetiredSerial)
    {
        if (failureReason) *failureReason = "upload submission serial is already retired";
        return false;
    }
    const std::uint64_t paddedSize = AlignVulkanTextureUploadOffset(size, copyAlignment);
    if (paddedSize == std::numeric_limits<std::uint64_t>::max() ||
        paddedSize > state.Config.Capacity)
    {
        if (failureReason) *failureReason = "upload is larger than the staging ring";
        return false;
    }

    std::uint64_t offset = AlignVulkanTextureUploadOffset(state.Head, copyAlignment);
    bool wrapped = false;
    if (offset == std::numeric_limits<std::uint64_t>::max() ||
        offset + paddedSize > state.Config.Capacity ||
        !UploadRangeAvailable(state, offset, paddedSize))
    {
        offset = 0u;
        wrapped = true;
        if (!UploadRangeAvailable(state, offset, paddedSize))
        {
            ++state.RejectedOverlapCount;
            if (failureReason) *failureReason = "upload ring overlaps an unretired submission";
            return false;
        }
    }

    reservation.Offset = offset;
    reservation.Size = size;
    reservation.PaddedSize = paddedSize;
    reservation.SubmissionSerial = submissionSerial;
    reservation.Wrapped = wrapped;
    reservation.ReusedRetiredSpace = wrapped && state.LastRetiredSerial != 0u;
    reservation.FlushOffset = (offset / atomSize) * atomSize;
    const std::uint64_t flushEnd = std::min<std::uint64_t>(
        state.Config.Capacity,
        AlignVulkanTextureUploadOffset(offset + size, atomSize));
    reservation.FlushSize = flushEnd - reservation.FlushOffset;

    state.InFlight.push_back(reservation);
    state.Head = offset + paddedSize;
    ++state.ReservationCount;
    if (wrapped) ++state.WrapCount;
    if (reservation.ReusedRetiredSpace) ++state.ReuseCount;
    return true;
}

void RetireVulkanTextureUploads(
    VulkanTextureUploadRingState& state,
    std::uint64_t completedSerial) noexcept
{
    if (completedSerial <= state.LastRetiredSerial) return;
    state.LastRetiredSerial = completedSerial;
    state.InFlight.erase(
        std::remove_if(state.InFlight.begin(), state.InFlight.end(),
            [completedSerial](const VulkanTextureUploadReservation& reservation)
            {
                return reservation.SubmissionSerial <= completedSerial;
            }),
        state.InFlight.end());
}

bool ValidateVulkanTextureUploadFlushRange(
    const VulkanTextureUploadRingState& state,
    const VulkanTextureUploadReservation& reservation) noexcept
{
    const std::uint64_t atomSize = std::max<std::uint64_t>(
        1u, state.Config.NonCoherentAtomSize);
    if (reservation.Size == 0u || reservation.FlushSize == 0u)
        return false;
    if (reservation.FlushOffset % atomSize != 0u)
        return false;
    if (reservation.FlushOffset > reservation.Offset)
        return false;
    if (reservation.FlushOffset + reservation.FlushSize <
        reservation.Offset + reservation.Size)
        return false;
    if (reservation.FlushOffset + reservation.FlushSize > state.Config.Capacity)
        return false;
    return reservation.FlushSize % atomSize == 0u ||
        reservation.FlushOffset + reservation.FlushSize == state.Config.Capacity;
}

} // namespace melonDS::Vulkan
