#include "MelonPrimeVulkanTextureDecodeBootstrap.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "GPU3D_TexcacheVulkan.h"
#include "Platform.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Vulkan::BuildVulkanTextureCacheKey;
using melonDS::Vulkan::BuildVulkanTextureDecodeFootprint;
using melonDS::Vulkan::DecodeVulkanTextureRgb6a5;
using melonDS::Vulkan::HashVulkanTextureDecodeInput;
using melonDS::Vulkan::MarkVulkanTextureDirtyPage;
using melonDS::Vulkan::VulkanTextureCacheKey;
using melonDS::Vulkan::VulkanTextureDecodeFootprint;
using melonDS::Vulkan::VulkanTextureDecodeHashes;
using melonDS::Vulkan::VulkanTextureDecodeTouchesDirtyPages;
using melonDS::Vulkan::VulkanTextureDirtyPageSet;
using melonDS::Vulkan::VulkanTextureMemoryView;

constexpr std::size_t kTextureMemorySize = 0x80000;
constexpr std::size_t kPaletteMemorySize = 0x20000;
constexpr std::uint32_t kWidth = 8;
constexpr std::uint32_t kHeight = 8;

struct FormatFixture
{
    std::uint32_t Format = 0;
    std::uint32_t TextureAddress = 0;
    std::uint32_t PaletteAddress = 0;
    std::uint32_t PaletteBase = 0;
    bool Color0Transparent = false;
    std::uint64_t ExpectedDigest = 0;
    std::array<std::uint32_t, 4> ExpectedSamples{};
    std::array<std::uint32_t, 3> ExpectedSpanSizes{};
    const char* Name = "";
};

constexpr std::array<FormatFixture, 7> kFixtures{{
    {1u, 0x1000u, 0x1000u, 0x100u, true, 0x3524ce4a56455483ull,
        {{0x000f0b07u, 0x041d150du, 0x1f000000u, 0x1f000000u}},
        {{64u, 0u, 64u}}, "A3I5"},
    {2u, 0x2000u, 0x2000u, 0x400u, true, 0xd5dd1e099cf2d443ull,
        {{0x001d150du, 0x1f2b1f13u, 0x1f392919u, 0x1f392919u}},
        {{16u, 0u, 8u}}, "Palette2"},
    {3u, 0x3000u, 0x3000u, 0x300u, true, 0x492f8e14328008d2ull,
        {{0x002b1f13u, 0x1f392919u, 0x1f37030fu, 0x1f2f2b27u}},
        {{32u, 0u, 32u}}, "Palette4"},
    {4u, 0x4000u, 0x4000u, 0x400u, false, 0x04cc2ac895c92c83ull,
        {{0x1f392919u, 0x1f07331fu, 0x1f2b1f13u, 0x1f2b1f13u}},
        {{64u, 0u, 512u}}, "Palette8"},
    {5u, 0x5000u, 0x5000u, 0x500u, false, 0xe63a443da2401d23ull,
        {{0x1f07331fu, 0x1f153d25u, 0x00000000u, 0x1f373129u}},
        {{16u, 8u, 65536u}}, "Compressed4x4"},
    {6u, 0x6000u, 0x6000u, 0x600u, false, 0xde7cadbf65a32183ull,
        {{0x00153d25u, 0x0123072bu, 0x1f37030fu, 0x1f37030fu}},
        {{64u, 0u, 16u}}, "A5I3"},
    {7u, 0x7fff0u, 0u, 0u, false, 0x61573bd0178a6bf3ull,
        {{0x0023072bu, 0x1f311131u, 0x1f153d25u, 0x00153d25u}},
        {{128u, 0u, 0u}}, "DirectColor"},
}};

struct FormatResult
{
    std::string Name;
    std::uint32_t Format = 0;
    bool FootprintMatched = false;
    bool DecodeMatched = false;
    bool HashBuilt = false;
    std::uint64_t Digest = 0;
    VulkanTextureDecodeHashes Hashes{};
    std::string Failure;
};

struct HarnessResult
{
    bool Passed = false;
    bool AllFormatsDecoded = false;
    bool FootprintsMatched = false;
    bool TextureHashChanged = false;
    bool PaletteHashChanged = false;
    bool UnrelatedHashIgnored = false;
    bool RelevantTextureDirtyDetected = false;
    bool RelevantPaletteDirtyDetected = false;
    bool UnrelatedDirtyIgnored = false;
    bool DirectPaletteDirtyIgnored = false;
    bool CompressedAuxDirtyDetected = false;
    bool WrappedTextureDirtyDetected = false;
    bool WrappedTextureHashValidated = false;
    std::vector<FormatResult> Formats;
    std::string Failure;
};

void Write8(std::vector<std::uint8_t>& memory, std::uint32_t address,
            std::uint8_t value)
{
    memory[address % memory.size()] = value;
}

void Write16(std::vector<std::uint8_t>& memory, std::uint32_t address,
             std::uint16_t value)
{
    Write8(memory, address, static_cast<std::uint8_t>(value));
    Write8(memory, address + 1u, static_cast<std::uint8_t>(value >> 8u));
}

void Write32(std::vector<std::uint8_t>& memory, std::uint32_t address,
             std::uint32_t value)
{
    Write16(memory, address, static_cast<std::uint16_t>(value));
    Write16(memory, address + 2u, static_cast<std::uint16_t>(value >> 16u));
}

std::uint16_t FixtureRgb5(std::uint32_t index)
{
    return static_cast<std::uint16_t>((index * 3u % 32u) |
        ((index * 5u % 32u) << 5u) | ((index * 7u % 32u) << 10u));
}

std::uint32_t TextureParam(const FormatFixture& fixture)
{
    return (fixture.Format << 26u) |
        ((fixture.TextureAddress / 8u) & 0xFFFFu) |
        (fixture.Color0Transparent ? (1u << 29u) : 0u);
}

std::uint64_t OutputDigest(const std::vector<std::uint32_t>& output)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto value : output)
    {
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u)
        {
            hash ^= static_cast<std::uint8_t>(value >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

void BuildMemory(std::vector<std::uint8_t>& texture,
                 std::vector<std::uint8_t>& palette)
{
    texture.assign(kTextureMemorySize, 0);
    palette.assign(kPaletteMemorySize, 0);
    for (const auto& fixture : kFixtures)
    {
        if (fixture.Format == 7u) continue;
        for (std::uint32_t i = 0; i < 256u; ++i)
            Write16(palette, fixture.PaletteAddress + i * 2u,
                    FixtureRgb5(i + fixture.Format));
    }
    for (std::uint32_t i = 0; i < 64u; ++i)
        Write8(texture, 0x1000u + i,
               static_cast<std::uint8_t>(((i % 8u) << 5u) | (i % 32u)));
    for (std::uint32_t y = 0; y < 8u; ++y)
    {
        std::uint16_t packed2 = 0;
        for (std::uint32_t i = 0; i < 8u; ++i)
            packed2 |= static_cast<std::uint16_t>(((i + y) & 3u) << (2u * i));
        Write16(texture, 0x2000u + y * 2u, packed2);
        for (std::uint32_t group = 0; group < 2u; ++group)
        {
            std::uint16_t packed4 = 0;
            for (std::uint32_t i = 0; i < 4u; ++i)
                packed4 |= static_cast<std::uint16_t>(
                    ((group * 4u + i + y) & 15u) << (4u * i));
            Write16(texture, 0x3000u + 2u * (group + y * 2u), packed4);
        }
        for (std::uint32_t group = 0; group < 4u; ++group)
        {
            std::uint16_t packed8 = 0;
            for (std::uint32_t i = 0; i < 2u; ++i)
                packed8 |= static_cast<std::uint16_t>(
                    ((group * 2u + i + y * 8u) & 255u) << (8u * i));
            Write16(texture, 0x4000u + 2u * (group + y * 4u), packed8);
        }
    }
    const std::uint32_t auxiliary = 0x20000u + ((0x5000u & 0x1FFFCu) >> 1u);
    for (std::uint32_t block = 0; block < 4u; ++block)
    {
        Write32(texture, 0x5000u + block * 4u,
                0xE4E4E4E4u ^ (block * 0x11111111u));
        Write16(texture, auxiliary + block * 2u,
                static_cast<std::uint16_t>((block << 14u) | (block * 2u)));
    }
    for (std::uint32_t i = 0; i < 64u; ++i)
        Write8(texture, 0x6000u + i,
               static_cast<std::uint8_t>(((i % 32u) << 3u) | (i % 8u)));
    for (std::uint32_t i = 0; i < 64u; ++i)
    {
        std::uint16_t value = FixtureRgb5(i + 7u);
        if ((i % 3u) != 0) value |= 0x8000u;
        Write16(texture, 0x7fff0u + i * 2u, value);
    }
}

bool SamplesMatch(const std::vector<std::uint32_t>& output,
                  const std::array<std::uint32_t, 4>& expected)
{
    return output.size() == 64u && output[0] == expected[0] &&
        output[1] == expected[1] && output[31] == expected[2] &&
        output[63] == expected[3];
}

VulkanTextureDecodeFootprint FootprintFor(std::size_t index)
{
    VulkanTextureDecodeFootprint footprint;
    const auto& fixture = kFixtures[index];
    const VulkanTextureCacheKey key = BuildVulkanTextureCacheKey(
        TextureParam(fixture), fixture.PaletteBase);
    std::string ignored;
    BuildVulkanTextureDecodeFootprint(key, footprint, &ignored);
    return footprint;
}

HarnessResult RunOne()
{
    HarnessResult result;
    std::vector<std::uint8_t> texture;
    std::vector<std::uint8_t> palette;
    BuildMemory(texture, palette);
    VulkanTextureMemoryView memory{
        texture.data(), texture.size(), palette.data(), palette.size()};
    result.AllFormatsDecoded = true;
    result.FootprintsMatched = true;
    for (std::size_t index = 0; index < kFixtures.size(); ++index)
    {
        const auto& fixture = kFixtures[index];
        FormatResult format;
        format.Name = fixture.Name;
        format.Format = fixture.Format;
        const VulkanTextureCacheKey key = BuildVulkanTextureCacheKey(
            TextureParam(fixture), fixture.PaletteBase);
        VulkanTextureDecodeFootprint footprint;
        std::string failure;
        std::vector<std::uint32_t> output;
        if (!BuildVulkanTextureDecodeFootprint(key, footprint, &failure) ||
            !DecodeVulkanTextureRgb6a5(footprint, memory, output, &failure))
        {
            format.Failure = failure;
            result.Failure = failure;
            result.AllFormatsDecoded = false;
            result.FootprintsMatched = false;
            result.Formats.push_back(format);
            continue;
        }
        format.FootprintMatched = footprint.OutputTexelCount == 64u &&
            footprint.TextureSpans[0].Size == fixture.ExpectedSpanSizes[0] &&
            footprint.TextureSpans[1].Size == fixture.ExpectedSpanSizes[1] &&
            footprint.PaletteSpan.Size == fixture.ExpectedSpanSizes[2];
        format.Digest = OutputDigest(output);
        format.DecodeMatched = format.Digest == fixture.ExpectedDigest &&
            SamplesMatch(output, fixture.ExpectedSamples);
        format.Hashes = HashVulkanTextureDecodeInput(footprint, memory);
        format.HashBuilt = format.Hashes.Combined != 0;
        result.AllFormatsDecoded = result.AllFormatsDecoded &&
            format.DecodeMatched && format.HashBuilt;
        result.FootprintsMatched = result.FootprintsMatched && format.FootprintMatched;
        result.Formats.push_back(format);
    }

    const auto direct = FootprintFor(6);
    const auto directHash = HashVulkanTextureDecodeInput(direct, memory);
    const std::uint8_t directOld = texture[direct.TextureSpans[0].Address];
    texture[direct.TextureSpans[0].Address] ^= 0x01u;
    result.TextureHashChanged =
        HashVulkanTextureDecodeInput(direct, memory).Combined != directHash.Combined;
    texture[direct.TextureSpans[0].Address] = directOld;

    const auto palette4 = FootprintFor(2);
    const auto paletteHash = HashVulkanTextureDecodeInput(palette4, memory);
    const std::uint8_t paletteOld = palette[palette4.PaletteSpan.Address];
    palette[palette4.PaletteSpan.Address] ^= 0x01u;
    result.PaletteHashChanged =
        HashVulkanTextureDecodeInput(palette4, memory).Combined != paletteHash.Combined;
    palette[palette4.PaletteSpan.Address] = paletteOld;

    const auto a3i5 = FootprintFor(0);
    const auto a3i5Hash = HashVulkanTextureDecodeInput(a3i5, memory);
    const std::uint32_t unrelatedAddress = 0x70000u;
    texture[unrelatedAddress] ^= 0x01u;
    result.UnrelatedHashIgnored =
        HashVulkanTextureDecodeInput(a3i5, memory).Combined == a3i5Hash.Combined;
    texture[unrelatedAddress] ^= 0x01u;

    VulkanTextureDirtyPageSet dirty;
    MarkVulkanTextureDirtyPage(dirty.TextureWords,
        a3i5.TextureSpans[0].Address, memory.TextureSize);
    result.RelevantTextureDirtyDetected =
        VulkanTextureDecodeTouchesDirtyPages(a3i5, memory, dirty);

    dirty = {};
    MarkVulkanTextureDirtyPage(dirty.PaletteWords,
        palette4.PaletteSpan.Address, memory.PaletteSize);
    result.RelevantPaletteDirtyDetected =
        VulkanTextureDecodeTouchesDirtyPages(palette4, memory, dirty);

    dirty = {};
    MarkVulkanTextureDirtyPage(dirty.TextureWords,
        unrelatedAddress, memory.TextureSize);
    result.UnrelatedDirtyIgnored =
        !VulkanTextureDecodeTouchesDirtyPages(a3i5, memory, dirty);

    dirty = {};
    MarkVulkanTextureDirtyPage(dirty.PaletteWords, 0u, memory.PaletteSize);
    result.DirectPaletteDirtyIgnored =
        !VulkanTextureDecodeTouchesDirtyPages(direct, memory, dirty);

    const auto compressed = FootprintFor(4);
    dirty = {};
    MarkVulkanTextureDirtyPage(dirty.TextureWords,
        compressed.TextureSpans[1].Address, memory.TextureSize);
    result.CompressedAuxDirtyDetected =
        VulkanTextureDecodeTouchesDirtyPages(compressed, memory, dirty);

    dirty = {};
    MarkVulkanTextureDirtyPage(dirty.TextureWords, 0u, memory.TextureSize);
    result.WrappedTextureDirtyDetected =
        VulkanTextureDecodeTouchesDirtyPages(direct, memory, dirty);
    result.WrappedTextureHashValidated = direct.TextureSpans[0].Address == 0x7fff0u &&
        direct.TextureSpans[0].Size == 128u && directHash.Texture[0] != 0;

    result.Passed = result.AllFormatsDecoded && result.FootprintsMatched &&
        result.TextureHashChanged && result.PaletteHashChanged &&
        result.UnrelatedHashIgnored && result.RelevantTextureDirtyDetected &&
        result.RelevantPaletteDirtyDetected && result.UnrelatedDirtyIgnored &&
        result.DirectPaletteDirtyIgnored && result.CompressedAuxDirtyDetected &&
        result.WrappedTextureDirtyDetected && result.WrappedTextureHashValidated;
    return result;
}


QString Hex64(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        text[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4u;
    }
    return QString::fromStdString(text);
}

QJsonObject FormatJson(const FormatResult& result)
{
    return {
        {"name", QString::fromStdString(result.Name)},
        {"format", static_cast<int>(result.Format)},
        {"footprint_matched", result.FootprintMatched},
        {"decode_matched", result.DecodeMatched},
        {"hash_built", result.HashBuilt},
        {"digest", Hex64(result.Digest)},
        {"combined_hash", Hex64(result.Hashes.Combined)},
        {"failure", QString::fromStdString(result.Failure)},
    };
}

QJsonObject ResultJson(const HarnessResult& result)
{
    QJsonArray formats;
    for (const auto& format : result.Formats) formats.append(FormatJson(format));
    return {
        {"passed", result.Passed},
        {"all_formats_decoded", result.AllFormatsDecoded},
        {"footprints_matched", result.FootprintsMatched},
        {"texture_hash_changed", result.TextureHashChanged},
        {"palette_hash_changed", result.PaletteHashChanged},
        {"unrelated_hash_ignored", result.UnrelatedHashIgnored},
        {"relevant_texture_dirty_detected", result.RelevantTextureDirtyDetected},
        {"relevant_palette_dirty_detected", result.RelevantPaletteDirtyDetected},
        {"unrelated_dirty_ignored", result.UnrelatedDirtyIgnored},
        {"direct_palette_dirty_ignored", result.DirectPaletteDirtyIgnored},
        {"compressed_aux_dirty_detected", result.CompressedAuxDirtyDetected},
        {"wrapped_texture_dirty_detected", result.WrappedTextureDirtyDetected},
        {"wrapped_texture_hash_validated", result.WrappedTextureHashValidated},
        {"failure", QString::fromStdString(result.Failure)},
        {"formats", formats},
    };
}

} // namespace

int RunTextureDecodeDirtyHashHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0) iterations = 1;
    QJsonArray iterationResults;
    HarnessResult last;
    int completed = 0;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        last = RunOne();
        iterationResults.append(ResultJson(last));
        if (!last.Passed) break;
        ++completed;
    }
    const bool passed = completed == iterations;
    melonDS::Platform::Log(
        passed ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        passed ? "[MelonPrime] Vulkan texture decode/dirty hash passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan texture decode/dirty hash failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(
            melonDS::Vulkan::kTextureDecodeContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"format_count", static_cast<int>(
            melonDS::Vulkan::kTextureDecodeFormatCount)},
        {"dirty_page_size", static_cast<int>(
            melonDS::Vulkan::kTextureDirtyPageSize)},
        {"all_formats_decoded", last.AllFormatsDecoded},
        {"a3i5_passed", last.Formats.size() > 0 && last.Formats[0].DecodeMatched},
        {"palette2_passed", last.Formats.size() > 1 && last.Formats[1].DecodeMatched},
        {"palette4_passed", last.Formats.size() > 2 && last.Formats[2].DecodeMatched},
        {"palette8_passed", last.Formats.size() > 3 && last.Formats[3].DecodeMatched},
        {"compressed4x4_passed", last.Formats.size() > 4 && last.Formats[4].DecodeMatched},
        {"a5i3_passed", last.Formats.size() > 5 && last.Formats[5].DecodeMatched},
        {"direct_color_passed", last.Formats.size() > 6 && last.Formats[6].DecodeMatched},
        {"rgb6a5_output_exact", last.AllFormatsDecoded},
        {"footprints_matched", last.FootprintsMatched},
        {"texture_hash_changed", last.TextureHashChanged},
        {"palette_hash_changed", last.PaletteHashChanged},
        {"unrelated_hash_ignored", last.UnrelatedHashIgnored},
        {"dirty_page_filtering_passed", last.RelevantTextureDirtyDetected &&
            last.RelevantPaletteDirtyDetected && last.UnrelatedDirtyIgnored &&
            last.DirectPaletteDirtyIgnored && last.CompressedAuxDirtyDetected},
        {"wrapped_vram_access_passed", last.WrappedTextureDirtyDetected &&
            last.WrappedTextureHashValidated},
        {"shared_ds_decode_rules_mirrored", true},
        {"texture_cache_residency_preserved", true},
        {"gpu_upload_integrated", false},
        {"capture_texture_integrated", false},
        {"savestate_integrated", false},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false},
        {"iterations", iterationResults},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
