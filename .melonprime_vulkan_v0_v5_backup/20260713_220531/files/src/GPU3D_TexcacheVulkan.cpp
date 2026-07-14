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



void VulkanTextureAsyncRetirementState::Reset(
    bool timelineSemaphoreAvailable) noexcept
{
    Backend = timelineSemaphoreAvailable
        ? VulkanTextureRetirementBackend::TimelineSemaphore
        : VulkanTextureRetirementBackend::FenceSerial;
    InFlight.clear();
    NextSerial = 1;
    LastQueuedSignalValue = 0;
    ObservedSignalValue = 0;
    LastRetiredSerial = 0;
    SubmissionCount = 0;
    RetirementCount = 0;
}

bool QueueVulkanTextureAsyncSubmission(
    VulkanTextureAsyncRetirementState& state,
    std::uint64_t signalValue,
    VulkanTextureAsyncSubmission& submission,
    std::string* failureReason) noexcept
{
    if (signalValue == 0u)
        signalValue = state.LastQueuedSignalValue + 1u;
    if (signalValue <= state.LastQueuedSignalValue)
    {
        if (failureReason)
            *failureReason = "texture completion signal must be strictly monotonic";
        return false;
    }
    submission = {};
    submission.Serial = state.NextSerial++;
    submission.SignalValue = signalValue;
    state.LastQueuedSignalValue = signalValue;
    state.InFlight.push_back(submission);
    ++state.SubmissionCount;
    return true;
}

std::uint64_t ObserveVulkanTextureCompletion(
    VulkanTextureAsyncRetirementState& state,
    std::uint64_t completedSignalValue) noexcept
{
    state.ObservedSignalValue = std::max(
        state.ObservedSignalValue, completedSignalValue);
    std::uint64_t retiredSerial = state.LastRetiredSerial;
    std::size_t completedPrefix = 0;
    for (auto& submission : state.InFlight)
    {
        submission.Completed = submission.SignalValue <= state.ObservedSignalValue;
        if (!submission.Completed)
            break;
        retiredSerial = submission.Serial;
        ++completedPrefix;
    }
    if (completedPrefix != 0u)
    {
        state.InFlight.erase(
            state.InFlight.begin(),
            state.InFlight.begin() + static_cast<std::ptrdiff_t>(completedPrefix));
        state.RetirementCount += completedPrefix;
        state.LastRetiredSerial = retiredSerial;
    }
    return state.LastRetiredSerial;
}

void RetireVulkanTextureUploadsFromAsync(
    VulkanTextureUploadRingState& ring,
    const VulkanTextureAsyncRetirementState& retirement) noexcept
{
    RetireVulkanTextureUploads(ring, retirement.LastRetiredSerial);
}

void VulkanTextureRuntimeState::Reset(
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept
{
    Entries.clear();
    DescriptorSlots.clear();
    UploadRing.Reset(ringConfig);
    Retirement.Reset(timelineSemaphoreAvailable);
    NextImageGeneration = 1;
    UseSerial = 0;
    CacheHitCount = 0;
    CacheMissCount = 0;
    UploadCount = 0;
    UploadBytes = 0;
    InvalidationCount = 0;
    DescriptorRewriteCount = 0;
    FirstFrameFullUpload = true;
}

bool AcquireVulkanTextureRuntime(
    VulkanTextureRuntimeState& state,
    const VulkanTextureCacheRequest& request,
    const VulkanTextureMemoryView& memory,
    const VulkanTextureDirtyPageSet& dirty,
    std::uint64_t completionSignalValue,
    VulkanTextureRuntimeAcquireResult& result,
    std::string* failureReason)
{
    result = {};
    const VulkanTextureCacheKey key = BuildVulkanTextureCacheKey(
        request.TexParam, request.TexPalette);
    if (key.Format == 0u)
    {
        if (failureReason) *failureReason = "texture format zero is not cacheable";
        return false;
    }

    VulkanTextureDecodeFootprint footprint;
    if (!BuildVulkanTextureDecodeFootprint(key, footprint, failureReason))
        return false;
    const VulkanTextureDecodeHashes hashes =
        HashVulkanTextureDecodeInput(footprint, memory);

    std::uint32_t entryIndex = static_cast<std::uint32_t>(state.Entries.size());
    for (std::uint32_t index = 0; index < state.Entries.size(); ++index)
    {
        if (state.Entries[index].Key == key)
        {
            entryIndex = index;
            break;
        }
    }

    bool newEntry = entryIndex == state.Entries.size();
    bool dirtyTouched = newEntry || state.FirstFrameFullUpload ||
        VulkanTextureDecodeTouchesDirtyPages(footprint, memory, dirty);
    bool hashChanged = newEntry;
    if (!newEntry)
        hashChanged = state.Entries[entryIndex].Hashes.Combined != hashes.Combined;
    const bool uploadRequired = newEntry ||
        state.FirstFrameFullUpload || (dirtyTouched && hashChanged);

    if (newEntry)
    {
        VulkanTextureRuntimeEntry entry;
        entry.Key = key;
        entry.Footprint = footprint;
        entry.Hashes = hashes;
        entry.ContentGeneration = request.ContentGeneration;
        entry.ImageGeneration = state.NextImageGeneration++;
        entry.Resident = true;
        state.Entries.push_back(entry);
        ++state.CacheMissCount;
    }
    else if (uploadRequired)
    {
        ++state.CacheMissCount;
        ++state.InvalidationCount;
    }
    else
    {
        ++state.CacheHitCount;
        result.CacheHit = true;
    }

    auto& entry = state.Entries[entryIndex];
    entry.LastUseSerial = ++state.UseSerial;
    result.EntryIndex = entryIndex;
    result.DirtyPagesTouched = dirtyTouched;
    result.HashChanged = hashChanged;
    result.UploadRequired = uploadRequired;

    if (uploadRequired)
    {
        if (!DecodeVulkanTextureRgb6a5(
                footprint, memory, result.DecodedRgb6a5, failureReason))
            return false;
        const std::uint64_t byteCount =
            static_cast<std::uint64_t>(result.DecodedRgb6a5.size()) *
            sizeof(std::uint32_t);
        if (!QueueVulkanTextureAsyncSubmission(
                state.Retirement, completionSignalValue,
                result.Submission, failureReason))
            return false;
        if (!ReserveVulkanTextureUpload(
                state.UploadRing, byteCount, result.Submission.Serial,
                result.Reservation, failureReason))
        {
            state.Retirement.InFlight.pop_back();
            --state.Retirement.SubmissionCount;
            --state.Retirement.NextSerial;
            return false;
        }
        entry.Footprint = footprint;
        entry.Hashes = hashes;
        entry.ContentGeneration = request.ContentGeneration;
        entry.ImageGeneration = state.NextImageGeneration++;
        entry.Resident = true;
        ++entry.UploadCount;
        ++state.UploadCount;
        state.UploadBytes += byteCount;
        result.DescriptorRewriteRequired = true;
        ++state.DescriptorRewriteCount;
    }

    const auto s = DecodeVulkanTextureSamplerAxis(request.TexParam, false);
    const auto t = DecodeVulkanTextureSamplerAxis(request.TexParam, true);
    result.SamplerIndex = VulkanTextureSamplerTableIndex(s, t);
    result.DescriptorSlot = static_cast<std::uint32_t>(state.DescriptorSlots.size());
    for (std::uint32_t index = 0; index < state.DescriptorSlots.size(); ++index)
    {
        const auto& slot = state.DescriptorSlots[index];
        if (slot.EntryIndex == entryIndex && slot.SamplerIndex == result.SamplerIndex)
        {
            result.DescriptorSlot = index;
            break;
        }
    }
    if (result.DescriptorSlot == state.DescriptorSlots.size())
    {
        state.DescriptorSlots.push_back({entryIndex, result.SamplerIndex});
        result.DescriptorRewriteRequired = true;
        ++state.DescriptorRewriteCount;
    }
    state.FirstFrameFullUpload = false;
    return true;
}

namespace
{

std::uint16_t CaptureRead(
    const std::vector<std::uint16_t>& pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t x,
    std::int64_t y,
    bool borderTransparent) noexcept
{
    if (width == 0u || height == 0u || pixels.empty())
        return 0;
    if (borderTransparent &&
        (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width) ||
         y >= static_cast<std::int64_t>(height)))
        return 0;
    const std::uint32_t wrappedX = static_cast<std::uint32_t>(
        (x % static_cast<std::int64_t>(width) + width) % width);
    const std::uint32_t wrappedY = static_cast<std::uint32_t>(
        (y % static_cast<std::int64_t>(height) + height) % height);
    const std::size_t index = static_cast<std::size_t>(wrappedY) * width + wrappedX;
    return index < pixels.size() ? pixels[index] : 0;
}

std::uint64_t CheckedScaleBytes(
    std::uint64_t width,
    std::uint64_t height,
    std::uint64_t layers,
    std::uint64_t bytesPerPixel) noexcept
{
    if (width == 0u || height == 0u || layers == 0u || bytesPerPixel == 0u)
        return 0u;
    if (width > std::numeric_limits<std::uint64_t>::max() / height)
        return std::numeric_limits<std::uint64_t>::max();
    std::uint64_t value = width * height;
    if (value > std::numeric_limits<std::uint64_t>::max() / layers)
        return std::numeric_limits<std::uint64_t>::max();
    value *= layers;
    if (value > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel)
        return std::numeric_limits<std::uint64_t>::max();
    return value * bytesPerPixel;
}

bool AddChecked(std::uint64_t& total, std::uint64_t value) noexcept
{
    if (value == std::numeric_limits<std::uint64_t>::max() ||
        total > std::numeric_limits<std::uint64_t>::max() - value)
    {
        total = std::numeric_limits<std::uint64_t>::max();
        return false;
    }
    total += value;
    return true;
}

} // namespace

bool ValidateVulkanDisplayCaptureConfig(
    const VulkanDisplayCaptureConfig& config,
    std::string* failureReason) noexcept
{
    if (config.Width != 128u && config.Width != 256u)
    {
        if (failureReason) *failureReason = "capture width must be 128 or 256";
        return false;
    }
    const bool validSize =
        (config.Width == 128u && config.Height == 128u) ||
        (config.Width == 256u &&
         (config.Height == 64u || config.Height == 128u || config.Height == 192u));
    if (!validSize || config.YStart > config.YEnd || config.YEnd > config.Height)
    {
        if (failureReason) *failureReason = "capture size or line range is invalid";
        return false;
    }
    if (config.DestinationBank >= 4u || config.DestinationOffset >= 4u)
    {
        if (failureReason) *failureReason = "capture destination bank or offset is invalid";
        return false;
    }
    if (config.DestinationBufferHeight != 128u &&
        config.DestinationBufferHeight != 256u)
    {
        if (failureReason) *failureReason = "capture destination height is invalid";
        return false;
    }
    if ((config.Width == 128u && config.DestinationBufferHeight != 128u) ||
        (config.Width == 256u && config.DestinationBufferHeight != 256u))
    {
        if (failureReason) *failureReason =
            "capture width and destination buffer height do not match";
        return false;
    }
    if (config.Eva > 16u || config.Evb > 16u)
    {
        if (failureReason) *failureReason = "capture blend factors exceed DS range";
        return false;
    }
    return true;
}

std::uint16_t BlendVulkanDisplayCapturePixel(
    std::uint16_t sourceA,
    std::uint16_t sourceB,
    std::uint32_t eva,
    std::uint32_t evb) noexcept
{
    eva = std::min<std::uint32_t>(eva, 16u);
    evb = std::min<std::uint32_t>(evb, 16u);
    const std::uint32_t aa = (sourceA & 0x8000u) != 0u ? 1u : 0u;
    const std::uint32_t ab = (sourceB & 0x8000u) != 0u ? 1u : 0u;
    std::uint16_t output = 0;
    for (std::uint32_t shift : {0u, 5u, 10u})
    {
        const std::uint32_t a = (sourceA >> shift) & 0x1Fu;
        const std::uint32_t b = (sourceB >> shift) & 0x1Fu;
        const std::uint32_t value = std::min<std::uint32_t>(
            31u, ((a * aa * eva) + (b * ab * evb) + 8u) >> 4u);
        output = static_cast<std::uint16_t>(output | (value << shift));
    }
    if ((eva != 0u && aa != 0u) || (evb != 0u && ab != 0u))
        output = static_cast<std::uint16_t>(output | 0x8000u);
    return output;
}

bool ExecuteVulkanDisplayCaptureReference(
    const VulkanDisplayCaptureConfig& config,
    const std::vector<std::uint16_t>& sourceA,
    const std::vector<std::uint16_t>& sourceB,
    std::vector<std::uint16_t>& destination,
    std::string* failureReason)
{
    if (!ValidateVulkanDisplayCaptureConfig(config, failureReason))
        return false;
    constexpr std::uint32_t sourceAWidth = 256u;
    constexpr std::uint32_t sourceAHeight = 192u;
    constexpr std::uint32_t sourceBWidth = 256u;
    constexpr std::uint32_t sourceBHeight = 256u;
    if (sourceA.size() < sourceAWidth * sourceAHeight ||
        sourceB.size() < sourceBWidth * sourceBHeight)
    {
        if (failureReason) *failureReason = "capture source buffers are incomplete";
        return false;
    }
    destination.resize(
        static_cast<std::size_t>(config.Width) * config.DestinationBufferHeight, 0u);
    for (std::uint32_t line = config.YStart; line < config.YEnd; ++line)
    {
        const std::uint32_t destinationLine =
            (config.DestinationOffset * 64u + line) % config.DestinationBufferHeight;
        const std::uint32_t sourceBLine =
            (config.SourceBOffset * 64u + line) % sourceBHeight;
        const std::int32_t sourceAX = config.SourceA ==
            VulkanDisplayCaptureSourceA::Engine3D && line < config.SourceAXOffset.size()
            ? config.SourceAXOffset[line]
            : 0;
        for (std::uint32_t x = 0; x < config.Width; ++x)
        {
            const std::uint16_t a = CaptureRead(
                sourceA, sourceAWidth, sourceAHeight,
                static_cast<std::int64_t>(x) + sourceAX, line, true);
            const std::uint16_t b = CaptureRead(
                sourceB, sourceBWidth, sourceBHeight, x, sourceBLine, false);
            std::uint16_t output = 0;
            switch (config.Mode)
            {
            case VulkanDisplayCaptureMode::SourceA:
                output = a;
                break;
            case VulkanDisplayCaptureMode::SourceB:
                output = b;
                break;
            case VulkanDisplayCaptureMode::Blend:
                output = BlendVulkanDisplayCapturePixel(a, b, config.Eva, config.Evb);
                break;
            }
            destination[static_cast<std::size_t>(destinationLine) * config.Width + x] = output;
        }
    }
    return true;
}

void VulkanCaptureRegistry::Reset() noexcept
{
    Slots.clear();
    NextGeneration = 1;
    CaptureCount = 0;
    GpuReuseCount = 0;
    CpuSyncCount = 0;
}

VulkanCaptureSlot& StoreVulkanCapture(
    VulkanCaptureRegistry& registry,
    const VulkanDisplayCaptureConfig& config,
    std::vector<std::uint16_t> pixels,
    bool complete)
{
    for (auto& slot : registry.Slots)
    {
        if (slot.Bank == config.DestinationBank &&
            slot.Offset == config.DestinationOffset &&
            slot.Width == config.Width)
        {
            slot.Height = config.Height;
            slot.BufferHeight = config.DestinationBufferHeight;
            slot.Generation = registry.NextGeneration++;
            slot.Complete = complete;
            slot.GpuResident = true;
            slot.CpuSynchronized = false;
            slot.Pixels = std::move(pixels);
            ++registry.CaptureCount;
            return slot;
        }
    }
    VulkanCaptureSlot slot;
    slot.Bank = config.DestinationBank;
    slot.Offset = config.DestinationOffset;
    slot.Width = config.Width;
    slot.Height = config.Height;
    slot.BufferHeight = config.DestinationBufferHeight;
    slot.Generation = registry.NextGeneration++;
    slot.Complete = complete;
    slot.GpuResident = true;
    slot.CpuSynchronized = false;
    slot.Pixels = std::move(pixels);
    registry.Slots.push_back(std::move(slot));
    ++registry.CaptureCount;
    return registry.Slots.back();
}

const VulkanCaptureSlot* ResolveVulkanCaptureTexture(
    VulkanCaptureRegistry& registry,
    std::uint32_t bank,
    std::uint32_t offset,
    std::uint32_t width) noexcept
{
    for (auto& slot : registry.Slots)
    {
        if (slot.Bank == bank && slot.Offset == offset && slot.Width == width &&
            slot.Complete && slot.GpuResident)
        {
            ++registry.GpuReuseCount;
            return &slot;
        }
    }
    return nullptr;
}

bool SyncVulkanCaptureToCpuVram(
    VulkanCaptureRegistry& registry,
    std::uint32_t bank,
    std::uint32_t offset,
    std::uint32_t width,
    std::vector<std::uint8_t>& vram,
    std::vector<std::uint64_t>& dirtyWords,
    std::string* failureReason)
{
    if (vram.empty())
    {
        if (failureReason) *failureReason = "capture VRAM bank is empty";
        return false;
    }
    for (auto& slot : registry.Slots)
    {
        if (slot.Bank != bank || slot.Offset != offset || slot.Width != width)
            continue;
        if (!slot.Complete)
        {
            if (failureReason) *failureReason = "capture is still in flight";
            return false;
        }
        const std::uint64_t start = static_cast<std::uint64_t>(offset) * 64u * 512u;
        const std::uint64_t byteCount =
            static_cast<std::uint64_t>(slot.Pixels.size()) * sizeof(std::uint16_t);
        for (std::uint64_t byte = 0; byte < byteCount; ++byte)
        {
            const std::uint16_t pixel = slot.Pixels[byte / 2u];
            const std::uint8_t value = (byte & 1u) == 0u
                ? static_cast<std::uint8_t>(pixel & 0xFFu)
                : static_cast<std::uint8_t>(pixel >> 8u);
            vram[(start + byte) % vram.size()] = value;
        }
        const std::uint64_t pageCount =
            (byteCount + kTextureDirtyPageSize - 1u) / kTextureDirtyPageSize;
        for (std::uint64_t page = 0; page < pageCount; ++page)
        {
            MarkVulkanTextureDirtyPage(
                dirtyWords,
                static_cast<std::uint32_t>((start + page * kTextureDirtyPageSize) % vram.size()),
                vram.size());
        }
        slot.CpuSynchronized = true;
        ++registry.CpuSyncCount;
        return true;
    }
    if (failureReason) *failureReason = "capture slot was not found";
    return false;
}

std::uint64_t EstimateVulkanPhase8ScaleBytes(std::uint32_t scale) noexcept
{
    if (scale < kPhase8MinimumScale || scale > kPhase8MaximumScale)
        return std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t s = scale;
    std::uint64_t total = 0;
    // Two double-buffered final outputs, each with top and bottom layers.
    if (!AddChecked(total, CheckedScaleBytes(256u * s, 192u * s, 4u, 4u)))
        return total;
    // Four 256-wide capture banks and sixteen 128-wide bank/offset slots.
    if (!AddChecked(total, CheckedScaleBytes(256u * s, 256u * s, 4u, 4u)))
        return total;
    if (!AddChecked(total, CheckedScaleBytes(128u * s, 128u * s, 16u, 4u)))
        return total;
    // Same-bank capture snapshot, CPU synchronization target and aux inputs.
    if (!AddChecked(total, CheckedScaleBytes(256u * s, 256u * s, 1u, 4u)))
        return total;
    if (!AddChecked(total, CheckedScaleBytes(256u, 256u, 1u, 4u)))
        return total;
    if (!AddChecked(total, CheckedScaleBytes(256u, 256u, 2u, 2u)))
        return total;
    return total;
}

VulkanScaleResourcePlan BuildVulkanScaleResourcePlan(
    std::uint32_t oldScale,
    std::uint32_t newScale,
    std::uint64_t oldGeneration,
    std::uint64_t memoryBudgetBytes,
    bool presenterLeaseOutstanding) noexcept
{
    VulkanScaleResourcePlan plan;
    plan.OldScale = oldScale;
    plan.NewScale = newScale;
    plan.OldGeneration = oldGeneration;
    plan.NewGeneration = oldGeneration + 1u;
    plan.MemoryBudgetBytes = memoryBudgetBytes;
    plan.PresenterLeaseOutstanding = presenterLeaseOutstanding;
    plan.DeferOldResourceDestruction = presenterLeaseOutstanding;
    plan.RequiredBytes = EstimateVulkanPhase8ScaleBytes(newScale);
    if (oldScale < kPhase8MinimumScale || oldScale > kPhase8MaximumScale ||
        newScale < kPhase8MinimumScale || newScale > kPhase8MaximumScale)
    {
        plan.FailureReason = "Vulkan scale must be in the range 1 through 16";
        return plan;
    }
    if (plan.RequiredBytes == std::numeric_limits<std::uint64_t>::max())
    {
        plan.FailureReason = "Vulkan scale resource size overflow";
        return plan;
    }
    if (plan.RequiredBytes > memoryBudgetBytes)
    {
        plan.FailureReason = "insufficient Vulkan memory budget for requested scale";
        return plan;
    }
    plan.Valid = true;
    return plan;
}

void VulkanPhase8LifecycleState::Initialize(
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept
{
    TextureRuntime.Reset(ringConfig, timelineSemaphoreAvailable);
    CaptureRegistry.Reset();
    Scale = 1;
    OutputGeneration = 1;
    DeferredResourceBytes = 0;
    ResetCount = 0;
    SavestateCount = 0;
    RendererSwitchCount = 0;
    ScaleChangeCount = 0;
    GpuWorkInFlight = false;
    PreSavestateSynchronized = false;
    FirstFrameFullUpload = true;
}

bool PreSavestateVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    std::array<std::vector<std::uint8_t>, 4>& vramBanks,
    std::array<std::vector<std::uint64_t>, 4>& dirtyWords,
    std::string* failureReason)
{
    if (state.GpuWorkInFlight || !state.TextureRuntime.UploadRing.InFlight.empty() ||
        !state.TextureRuntime.Retirement.InFlight.empty())
    {
        if (failureReason) *failureReason = "Vulkan Phase 8 work is still in flight";
        return false;
    }
    for (auto& slot : state.CaptureRegistry.Slots)
    {
        if (!slot.CpuSynchronized)
        {
            if (!SyncVulkanCaptureToCpuVram(
                    state.CaptureRegistry, slot.Bank, slot.Offset, slot.Width,
                    vramBanks[slot.Bank], dirtyWords[slot.Bank], failureReason))
                return false;
        }
    }
    state.PreSavestateSynchronized = true;
    ++state.SavestateCount;
    return true;
}

void PostSavestateVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept
{
    state.TextureRuntime.Reset(ringConfig, timelineSemaphoreAvailable);
    state.CaptureRegistry.Reset();
    ++state.OutputGeneration;
    state.GpuWorkInFlight = false;
    state.PreSavestateSynchronized = false;
    state.FirstFrameFullUpload = true;
}

void ResetVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept
{
    const std::uint32_t scale = state.Scale;
    const std::uint64_t generation = state.OutputGeneration + 1u;
    const std::uint64_t resetCount = state.ResetCount + 1u;
    state.TextureRuntime.Reset(ringConfig, timelineSemaphoreAvailable);
    state.CaptureRegistry.Reset();
    state.Scale = scale;
    state.OutputGeneration = generation;
    state.GpuWorkInFlight = false;
    state.PreSavestateSynchronized = false;
    state.FirstFrameFullUpload = true;
    state.ResetCount = resetCount;
}

bool FlushVulkanPhase8ForRendererSwitch(
    VulkanPhase8LifecycleState& state,
    std::array<std::vector<std::uint8_t>, 4>& vramBanks,
    std::array<std::vector<std::uint64_t>, 4>& dirtyWords,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable,
    std::string* failureReason)
{
    if (!PreSavestateVulkanPhase8(state, vramBanks, dirtyWords, failureReason))
        return false;
    const std::uint64_t switches = state.RendererSwitchCount + 1u;
    PostSavestateVulkanPhase8(state, ringConfig, timelineSemaphoreAvailable);
    state.RendererSwitchCount = switches;
    return true;
}

bool ApplyVulkanPhase8ScaleChange(
    VulkanPhase8LifecycleState& state,
    const VulkanScaleResourcePlan& plan) noexcept
{
    if (!plan.Valid || plan.OldScale != state.Scale ||
        plan.OldGeneration != state.OutputGeneration)
        return false;
    if (plan.DeferOldResourceDestruction)
        state.DeferredResourceBytes += EstimateVulkanPhase8ScaleBytes(state.Scale);
    state.Scale = plan.NewScale;
    state.OutputGeneration = plan.NewGeneration;
    state.FirstFrameFullUpload = true;
    state.TextureRuntime.FirstFrameFullUpload = true;
    ++state.ScaleChangeCount;
    return true;
}

bool VulkanPhase8ExitAudit::Passed() const noexcept
{
    return AllTextureFormats && RepeatMirrorClamp && ClearBitmap &&
        DisplayCapture && CaptureTextureReuse && Savestate && Reset &&
        RendererSwitch && ScaleLiveChange && AsyncRetirement &&
        CpuReadbackSynchronization && MemoryBudget && ResourceLifetime;
}

} // namespace melonDS::Vulkan
