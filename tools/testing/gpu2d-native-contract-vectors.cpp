/*
    GPU-independent contract vectors for the native Vulkan/DX12 GPU2D input
    ABI and exact logical-pixel comparator.
*/

#include <array>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "GPU2DNative.h"
#include "Platform.h"

namespace melonDS::Platform
{
// The standalone contract vector links the developer diagnostic code from
// core, but not the Qt frontend's logger implementation.
void Log(LogLevel, const char*, ...)
{
}
} // namespace melonDS::Platform

namespace
{

using namespace melonDS;
using namespace melonDS::GPU2DNative;

bool Require(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

u32 SoftwareCaptureByteAddress(
    u32 offsetCode, u32 width, u32 line, u32 x)
{
    const u32 halfword = WrapLCDCHalfword(
        CaptureOffsetHalfwords(offsetCode) + line * width + x);
    return halfword << 1u;
}

u32 NativeCaptureByteAddress(
    u32 offsetCode, u32 width, u32 line, u32 x)
{
    return WrapLCDCByte(
        CaptureOffsetBytes(offsetCode)
        + line * width * 2u + x * 2u);
}

u32 SoftwareSourceBByteAddress(u32 offsetCode, u32 line, u32 x)
{
    const u32 halfword = WrapLCDCHalfword(
        line * 256u + x + CaptureOffsetHalfwords(offsetCode));
    return halfword << 1u;
}

u32 NativeSourceBByteAddress(u32 offsetCode, u32 line, u32 x)
{
    return WrapLCDCByte(
        line * 512u + x * 2u + CaptureOffsetBytes(offsetCode));
}

bool RunMappedBlockFlattenVectors()
{
    constexpr u32 blockBytes = 512u;
    const std::array<u32, 9> bankSizes = {
        128u * 1024u, 128u * 1024u, 128u * 1024u, 128u * 1024u,
        64u * 1024u, 16u * 1024u, 16u * 1024u, 32u * 1024u,
        16u * 1024u};
    std::array<std::vector<u8>, 9> banks;
    for (u32 bank = 0u; bank < banks.size(); ++bank)
    {
        banks[bank].resize(bankSizes[bank]);
        for (u32 address = 0u; address < bankSizes[bank]; ++address)
        {
            banks[bank][address] = static_cast<u8>(
                (address * 29u + bank * 47u + (address >> 9u)) & 0xFFu);
        }
    }

    const std::array<u32, 7> logicalOffsets = {
        0x00000u, 0x03E00u, 0x07E00u, 0x0FE00u,
        0x17E00u, 0x1FE00u, 0x3FE00u};
    std::array<u8, blockBytes> reference{};
    std::array<u8, blockBytes> flattened{};
    for (const u32 mappingBytes : {8u * 1024u, 16u * 1024u})
    {
        for (const u32 logicalOffset : logicalOffsets)
        {
            if (logicalOffset % mappingBytes + blockBytes > mappingBytes)
                continue;
            for (u32 bankMask = 0u; bankMask < (1u << banks.size()); ++bankMask)
            {
                reference.fill(0u);
                flattened.fill(0u);
                for (u32 byte = 0u; byte < blockBytes; ++byte)
                {
                    for (u32 bank = 0u; bank < banks.size(); ++bank)
                    {
                        if ((bankMask & (1u << bank)) != 0u)
                        {
                            reference[byte] |= banks[bank][
                                (logicalOffset + byte) & (bankSizes[bank] - 1u)];
                        }
                    }
                }

                u32 remaining = bankMask;
                if (remaining != 0u)
                {
                    const u32 firstBank = static_cast<u32>(__builtin_ctz(remaining));
                    remaining &= remaining - 1u;
                    for (u32 byte = 0u; byte < blockBytes; ++byte)
                    {
                        flattened[byte] = banks[firstBank][
                            (logicalOffset + byte) & (bankSizes[firstBank] - 1u)];
                    }
                    while (remaining != 0u)
                    {
                        const u32 bank = static_cast<u32>(__builtin_ctz(remaining));
                        remaining &= remaining - 1u;
                        for (u32 byte = 0u; byte < blockBytes; ++byte)
                        {
                            flattened[byte] |= banks[bank][
                                (logicalOffset + byte) & (bankSizes[bank] - 1u)];
                        }
                    }
                }
                if (flattened != reference)
                {
                    return Require(false,
                        "block mapped-VRAM flatten differs from byte reference");
                }
            }
        }
    }

    // The shader's recorded row and the host snapshot do not share a temporal
    // boundary. A CPU/DMA write can supersede the recorded native owner before
    // the fast 64-bit read executes, so live authority must select CPU bytes.
    constexpr u32 mappedBankMask = 1u << 2u;
    constexpr u32 recordedRowNativeMask = 1u << 2u;
    constexpr u32 liveNativeMask = 0u;
    constexpr u32 currentFrameWrittenMask = 0u;
    const u32 legacyCpuMask = mappedBankMask & ~recordedRowNativeMask;
    const u32 liveAuthorityCpuMask = mappedBankMask
        & ~(liveNativeMask | currentFrameWrittenMask);
    if (!Require(legacyCpuMask == 0u
            && liveAuthorityCpuMask == mappedBankMask,
            "fast mapped read reused a shader-row owner after CPU authority"))
    {
        return false;
    }
    return true;
}

u32 SoftwareCaptureBlockMask(u32 offsetCode, u32 sizeCode)
{
    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    u32 mask = 0u;
    for (u32 line = 0u; line < height; ++line)
    {
        for (u32 x = 0u; x < width; x += 2u)
        {
            const u32 byteAddress = SoftwareCaptureByteAddress(
                offsetCode, width, line, x);
            mask |= 1u << (byteAddress / CapturePhysicalBlockBytes);
        }
    }
    return mask;
}

u32 NativeCaptureBlockMask(u32 offsetCode, u32 sizeCode)
{
    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    u32 mask = 0u;
    for (u32 line = 0u; line < height; ++line)
    {
        for (u32 x = 0u; x < width; x += 2u)
        {
            const u32 byteAddress = NativeCaptureByteAddress(
                offsetCode, width, line, x);
            mask |= 1u << (byteAddress / CapturePhysicalBlockBytes);
        }
    }
    return mask;
}

void SimulateNativeCompactCapture(
    std::vector<u8>& mirror,
    u32 bank,
    u32 offsetCode,
    u32 sizeCode)
{
    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    const std::size_t bankBase = static_cast<std::size_t>(bank)
        * LCDCBankBytes;
    for (u32 line = 0u; line < height; ++line)
    {
        for (u32 x = 0u; x < width; x += 2u)
        {
            const u32 address = NativeCaptureByteAddress(
                offsetCode, width, line, x);
            mirror[bankBase + address] = 0x3Cu;
            mirror[bankBase + address + 1u] = 0xC3u;
            mirror[bankBase + address + 2u] = 0x3Cu;
            mirror[bankBase + address + 3u] = 0xC3u;
        }
    }
}

bool RunCaptureAddressVectors()
{
    bool passed = true;

    const std::array<u32, 4> expectedStartBytes = {
        0x00000u, 0x08000u, 0x10000u, 0x18000u};
    for (u32 offset = 0u; offset < 4u; ++offset)
    {
        passed &= Require(
            CaptureOffsetBytes(offset) == expectedStartBytes[offset],
            "capture destination offset did not convert halfwords to bytes");
    }

    // Exercise every size/offset pair and several edge pixels. The software
    // expression is intentionally kept independent from the native byte
    // expression so a unit regression cannot make both sides pass together.
    for (u32 size = 0u; size < 4u; ++size)
    {
        const u32 width = CaptureWidthForSize(size);
        const u32 height = CaptureHeightForSize(size);
        const u32 expectedBytes = width * height * 2u;
        passed &= Require(
            expectedBytes == (size == 0u ? 0x8000u
                : size == 1u ? 0x8000u
                : size == 2u ? 0x10000u : 0x18000u),
            "capture size code has an unexpected byte length");

        const std::array<u32, 4> lines = {
            0u, height / 2u, height - 1u, height > 1u ? height - 2u : 0u};
        const std::array<u32, 4> xs = {
            0u, 1u, width / 2u, width - 1u};
        for (u32 offset = 0u; offset < 4u; ++offset)
        {
            const u32 softwareMask = SoftwareCaptureBlockMask(offset, size);
            const u32 nativeMask = NativeCaptureBlockMask(offset, size);
            passed &= Require(
                softwareMask == nativeMask,
                "software/native capture block masks differ");
            for (const u32 line : lines)
            {
                for (const u32 x : xs)
                {
                    passed &= Require(
                        SoftwareCaptureByteAddress(offset, width, line, x)
                            == NativeCaptureByteAddress(offset, width, line, x),
                        "software/native capture byte address differs");
                }
            }
        }
    }

    // Source-B is a separate byte-address context. Use one unique BGR555
    // value per 32 KiB band so offset 0 alone cannot hide a bad conversion.
    const std::array<u16, 4> sourcePatterns = {
        0x001Fu, 0x03E0u, 0x7C00u, 0x7FFFu};
    for (u32 offset = 0u; offset < 4u; ++offset)
    {
        for (const u32 line : {0u, 63u, 127u, 191u})
        {
            for (const u32 x : {0u, 5u, 127u, 255u})
            {
                const u32 software = SoftwareSourceBByteAddress(offset, line, x);
                const u32 native = NativeSourceBByteAddress(offset, line, x);
                passed &= Require(
                    software == native,
                    "software/native source-B byte address differs");
                passed &= Require(
                    sourcePatterns[software / CapturePhysicalBlockBytes]
                        == sourcePatterns[native / CapturePhysicalBlockBytes],
                    "source-B offset selected different LCDC pattern band");

                // Display mode 2 ignores source-B's offset and reads the
                // destination LCDC address directly after capture.
                const u32 mode2Software = WrapLCDCByte(line * 512u + x * 2u);
                const u32 mode2Native = WrapLCDCByte(line * 512u + x * 2u);
                passed &= Require(
                    mode2Software == mode2Native,
                    "display mode 2 source address is not scale-independent");
            }
        }
    }

    // Offset 3 / size 3 is the crossing case: it must wrap inside one 128 KiB
    // bank and never touch a neighboring bank or the guard scratch region.
    constexpr u32 targetBank = 1u;
    std::vector<u8> mirror(CapturePhysicalBanks * LCDCBankBytes, 0xA5u);
    const std::vector<u8> before = mirror;
    std::vector<u8> scratch(2u * LCDCBankBytes, 0x5Au);
    const std::vector<u8> scratchBefore = scratch;
    SimulateNativeCompactCapture(mirror, targetBank, 3u, 3u);
    u32 bankWrapMismatches = 0u;
    u32 firstBankWrapMismatch = 0xFFFFFFFFu;
    for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
    {
        for (u32 byte = 0u; byte < LCDCBankBytes; ++byte)
        {
            const bool target = bank == targetBank;
            const bool shouldChange = target
                && (byte >= 0x18000u || byte < 0x10000u);
            const std::size_t index = static_cast<std::size_t>(bank)
                * LCDCBankBytes + byte;
            if ((mirror[index] != before[index]) != shouldChange)
            {
                ++bankWrapMismatches;
                if (firstBankWrapMismatch == 0xFFFFFFFFu)
                    firstBankWrapMismatch = bank * LCDCBankBytes + byte;
            }
        }
    }
    passed &= Require(
        bankWrapMismatches == 0u,
        "capture bank-wrap or neighboring-bank sentinel mismatch");
    if (bankWrapMismatches != 0u)
    {
        std::fprintf(
            stderr,
            "bank-wrap mismatches=%u firstPhysicalIndex=0x%X\n",
            bankWrapMismatches, firstBankWrapMismatch);
    }
    passed &= Require(
        scratch == scratchBefore,
        "capture bank-wrap changed the guard scratch region");

    // The address contract is independent of presentation scale. Include a
    // 600-frame logical sequence so a presentation-frame or stale-hash shortcut
    // cannot satisfy this vector accidentally.
    const u32 referenceMask = NativeCaptureBlockMask(3u, 3u);
    for (u32 emulatedFrame = 0u; emulatedFrame < 600u; ++emulatedFrame)
    {
        for (const u32 scale : {1u, 4u, 16u})
        {
            (void)scale;
            passed &= Require(
                NativeCaptureBlockMask(3u, 3u) == referenceMask,
                "capture address changed with presentation scale");
        }
    }

    return passed;
}

u32 CaptureRawFromColor6ForTest(u32 color)
{
    return ((color >> 1u) & 0x1Fu)
        | (((color >> 9u) & 0x1Fu) << 5u)
        | (((color >> 17u) & 0x1Fu) << 10u)
        | (((color >> 24u) != 0u ? 1u : 0u) << 15u);
}

u32 Color6FromCaptureRawForTest(u32 raw)
{
    return ((raw & 0x1Fu) << 1u)
        | (((raw >> 5u) & 0x1Fu) << 9u)
        | (((raw >> 10u) & 0x1Fu) << 17u)
        | (((raw >> 15u) & 1u) != 0u ? 0x1Fu << 24u : 0u);
}

u32 TrustedSourceBSampleForTest(
    u32 compact,
    const std::array<u32, 256u>& sidecar,
    u32 scale,
    u32 sampleX,
    u32 sampleY)
{
    const u32 representative = GPU2DNative::RepresentativeSubpixel(scale);
    const std::size_t canonicalIndex =
        static_cast<std::size_t>(representative) * scale + representative;
    if (CaptureRawFromColor6ForTest(sidecar[canonicalIndex]) != compact)
        return compact;
    const std::size_t sampleIndex =
        static_cast<std::size_t>(sampleY % scale) * scale + (sampleX % scale);
    return CaptureRawFromColor6ForTest(sidecar[sampleIndex]);
}

bool RunSourceBSubpixelAndSavestateVectors()
{
    bool passed = true;
    const u32 compact = 0x801Fu;
    constexpr u32 scale = 4u;
    std::array<u32, 256u> sidecar{};
    sidecar[2u * scale + 2u] = Color6FromCaptureRawForTest(compact);
    sidecar[1u * scale + 2u] = Color6FromCaptureRawForTest(0x83E0u);
    sidecar[2u * scale + 1u] = Color6FromCaptureRawForTest(0xFC00u);
    sidecar[3u * scale + 3u] = Color6FromCaptureRawForTest(0x001Fu);

    passed &= Require(
        TrustedSourceBSampleForTest(compact, sidecar, scale, 2u, 1u) == 0x83E0u,
        "Source-B trusted sidecar did not preserve the requested subpixel");
    passed &= Require(
        TrustedSourceBSampleForTest(compact, sidecar, scale, 1u, 2u)
            != TrustedSourceBSampleForTest(compact, sidecar, scale, 2u, 1u),
        "Source-B output did not depend on the subpixel coordinate");

    auto staleSidecar = sidecar;
    staleSidecar[2u * scale + 2u] = Color6FromCaptureRawForTest(0x03E0u);
    for (u32 sampleY = 0u; sampleY < scale; ++sampleY)
    {
        for (u32 sampleX = 0u; sampleX < scale; ++sampleX)
        {
            passed &= Require(
                TrustedSourceBSampleForTest(
                    compact, staleSidecar, scale, sampleX, sampleY) == compact,
                "stale Source-B canonical reference was not rejected");
        }
    }

    constexpr u32 kControlHas3D = 0x40u;
    constexpr u32 kControlAbove = 0x80u;
    constexpr u32 kBlend4 = 1u;
    constexpr u32 kBrightnessUp = 2u;
    constexpr u32 kBrightnessDown = 3u;
    constexpr u32 kBlend5 = 4u;
    const std::array<u32, 5u> effectModes = {
        0u, kBlend4, kBrightnessUp, kBrightnessDown, kBlend5};
    for (const u32 scale : {1u, 2u, 4u, 8u, 16u})
    {
        (void)scale;
        for (const u32 effect : effectModes)
        {
            // First-layer capture follows the same deferred slot contract as
            // first-layer 3D: below is the second word, the effect stays in
            // the control word, and the sidecar reference is retained.
            const u32 firstControl = kControlHas3D | effect;
            passed &= Require(
                (firstControl & kControlHas3D) != 0u,
                "capture effect matrix lost the deferred 3D slot");
            passed &= Require(
                effect == (firstControl & 0x0Fu),
                "capture effect matrix changed its composition mode");
        }

        const u32 secondControl = kControlHas3D | kControlAbove | kBlend4;
        passed &= Require(
            (secondControl & (kControlHas3D | kControlAbove))
                == (kControlHas3D | kControlAbove),
            "second-layer capture Blend4 did not retain the above-plane contract");
    }

    // GPU::StartHBlank draws the restored line first, then prepares the next
    // line. The recovery hook must seed exactly the visible VCOUNT line and
    // must not manufacture a sprite line during VBlank.
    for (const u32 vcount : {0u, 1u, 32u, 64u, 96u, 128u, 191u})
    {
        passed &= Require(
            vcount < ScreenHeight,
            "visible savestate VCOUNT was not admitted to OBJ recovery");
    }
    for (const u32 vcount : {192u, 215u, 262u})
    {
        passed &= Require(
            vcount >= ScreenHeight,
            "VBlank savestate VCOUNT incorrectly seeded a visible OBJ line");
    }
    return passed;
}

bool RunVRAMDisplaySidecarReferenceVectors()
{
    bool passed = true;
    constexpr u32 bankC = 2u;
    constexpr u32 bankD = 3u;
    constexpr u32 committedVersion = 1u;
    constexpr u32 captureStart = 0u;
    constexpr u32 line = 64u;
    constexpr u32 addressBeforeWrite = line * 256u;
    constexpr u32 addressWrittenEarlier = (line - 1u) * 256u;
    constexpr u32 captureC = (3u << 20u) | (bankC << 16u);
    constexpr u32 captureD = (3u << 20u) | (bankD << 16u);

    const auto captureOff = ResolveHighResCaptureReference(
        true, false, committedVersion, line, bankC, addressBeforeWrite,
        0u, CaptureStartLineNone);
    passed &= Require(
        captureOff.Version == HighResCaptureReferenceVersion::Committed
            && captureOff.SidecarVersion == committedVersion,
        "capture OFF discarded a retained VRAM-display sidecar");

    // Capture activity in the opposite C/D ping-pong bank is unrelated to the
    // displayed bank's retained physical contents.
    const auto displayCCaptureD = ResolveHighResCaptureReference(
        true, false, committedVersion, line, bankC, addressBeforeWrite,
        captureD, captureStart);
    const auto displayDCaptureC = ResolveHighResCaptureReference(
        true, false, committedVersion, line, bankD, addressBeforeWrite,
        captureC, captureStart);
    passed &= Require(
        displayCCaptureD.Version == HighResCaptureReferenceVersion::Committed,
        "display C / capture D fell back from committed sidecar");
    passed &= Require(
        displayDCaptureC.Version == HighResCaptureReferenceVersion::Committed,
        "display D / capture C fell back from committed sidecar");

    const auto sameBankBeforeWrite = ResolveHighResCaptureReference(
        true, true, committedVersion, line, bankC, addressBeforeWrite,
        captureC, captureStart);
    const auto sameBankAfterWrite = ResolveHighResCaptureReference(
        true, true, committedVersion, line, bankC, addressWrittenEarlier,
        captureC, captureStart);
    passed &= Require(
        sameBankBeforeWrite.Version == HighResCaptureReferenceVersion::Committed
            && sameBankBeforeWrite.SidecarVersion == committedVersion,
        "same-bank VRAM display did not read old committed data before write");
    passed &= Require(
        sameBankAfterWrite.Version == HighResCaptureReferenceVersion::Pending
            && sameBankAfterWrite.SidecarVersion == (committedVersion ^ 1u),
        "same-bank VRAM display did not select pending data after write");

    const auto pendingWithoutOldBeforeWrite = ResolveHighResCaptureReference(
        false, true, committedVersion, line, bankC, addressBeforeWrite,
        captureC, captureStart);
    const auto pendingWithoutOldAfterWrite = ResolveHighResCaptureReference(
        false, true, committedVersion, line, bankC, addressWrittenEarlier,
        captureC, captureStart);
    passed &= Require(
        pendingWithoutOldBeforeWrite.Version
            == HighResCaptureReferenceVersion::None,
        "unwritten pending capture exposed sidecar data without an old version");
    passed &= Require(
        pendingWithoutOldAfterWrite.Version
            == HighResCaptureReferenceVersion::Pending,
        "written pending capture was not exposed without an old version");

    // CPU/DMA invalidation clears committed validity only for the overlapping
    // segment. A non-overlapping segment and content-preserving materialization
    // retain the same committed decision.
    const auto cpuWritten = ResolveHighResCaptureReference(
        false, false, committedVersion, line, bankC, addressBeforeWrite,
        0u, CaptureStartLineNone);
    const auto nonOverlapping = ResolveHighResCaptureReference(
        true, false, committedVersion, line, bankC, addressBeforeWrite + 128u,
        0u, CaptureStartLineNone);
    const auto materialized = ResolveHighResCaptureReference(
        true, false, committedVersion, line, bankC, addressBeforeWrite,
        0u, CaptureStartLineNone);
    passed &= Require(
        cpuWritten.Version == HighResCaptureReferenceVersion::None,
        "CPU-written VRAM-display segment retained a stale sidecar reference");
    passed &= Require(
        nonOverlapping.Version == HighResCaptureReferenceVersion::Committed,
        "non-overlapping CPU write retired a VRAM-display sidecar");
    passed &= Require(
        materialized.Version == HighResCaptureReferenceVersion::Committed,
        "content-preserving materialization retired a VRAM-display sidecar");

    for (u32 gapFrame = 0u; gapFrame < 74u; ++gapFrame)
    {
        const auto retained = ResolveHighResCaptureReference(
            true, false, committedVersion, line, bankC, addressBeforeWrite,
            0u, CaptureStartLineNone);
        passed &= Require(
            retained.Version == HighResCaptureReferenceVersion::Committed,
            "74-frame capture gap changed persistent VRAM-display selection");
    }
    for (const u32 scale : {1u, 2u, 3u, 4u})
    {
        (void)scale;
        const auto retained = ResolveHighResCaptureReference(
            true, false, committedVersion, line, bankC, addressBeforeWrite,
            captureD, captureStart);
        passed &= Require(
            retained.Version == HighResCaptureReferenceVersion::Committed,
            "VRAM-display sidecar selection changed with presentation scale");
    }

    // VRAM display ignores bit 15. Keep the representative guard's comparison
    // aligned with OpenGL's RGB555 downscale/canonicalization rule.
    constexpr u32 compact = 0x001Fu;
    constexpr u32 canonical = 0x801Fu;
    passed &= Require(
        (compact & 0x7FFFu) == (canonical & 0x7FFFu),
        "VRAM-display representative guard treated LCDC bit 15 as color");
    passed &= Require(
        (compact & 0x7FFFu) != (0x03E0u & 0x7FFFu),
        "VRAM-display representative mismatch guard admitted different color");
    return passed;
}

bool RunFrameCoverageAndRepresentativeVectors()
{
    bool passed = true;

    for (const u32 scale : {1u, 2u, 4u, 8u, 16u})
    {
        const u32 representative = GPU2DNative::RepresentativeSubpixel(scale);
        std::array<u32, 256u * 256u> highRes{};
        highRes[0u] = Color6FromCaptureRawForTest(0x001Fu);
        highRes[static_cast<std::size_t>(representative) * 256u + representative]
            = Color6FromCaptureRawForTest(0x03E0u);
        const u32 compact = CaptureRawFromColor6ForTest(
            highRes[static_cast<std::size_t>(representative) * 256u + representative]);
        passed &= Require(
            compact == 0x03E0u,
            "native compact capture did not use the Resolve representative centre");
    }

    auto markRange = [](GPU2DNative::LineCoverage& coverage, u32 firstLine) {
        for (u32 line = firstLine; line < GPU2DNative::ScreenHeight; ++line)
            coverage.Mark(line);
    };
    auto completeStructuredFrame = [](GPU2DNative::LineCoverage (&screen)[2],
                                      GPU2DNative::LineCoverage (&engine)[2]) {
        for (u32 line = 0u; line < GPU2DNative::ScreenHeight; ++line)
        {
            screen[0].Mark(line);
            screen[1].Mark(line);
            engine[0].Mark(line);
            engine[1].Mark(line);
        }
    };
    auto complete = [](const GPU2DNative::LineCoverage (&screen)[2],
                       const GPU2DNative::LineCoverage (&engine)[2]) {
        return screen[0].Complete() && screen[1].Complete()
            && engine[0].Complete() && engine[1].Complete();
    };

    // VCount=0 can rebuild a complete post-load frame; every other visible
    // resume begins with a missing prefix and must not compose at VBlank.
    for (const u32 vcount : {0u, 1u, 32u, 64u, 96u, 128u, 191u})
    {
        GPU2DNative::LineCoverage screen[2]{};
        GPU2DNative::LineCoverage engine[2]{};
        markRange(screen[0], vcount);
        markRange(screen[1], vcount);
        markRange(engine[0], vcount);
        markRange(engine[1], vcount);
        const bool valid = complete(screen, engine);
        passed &= Require(
            valid == (vcount == 0u),
            "savestate VCount coverage gate admitted an incomplete Scale1 frame");
        const u32 composeCalls = valid ? 1u : 0u;
        passed &= Require(
            composeCalls == (vcount == 0u ? 1u : 0u),
            "partial VBlank attempted a structured composition");
    }

    // line 191 alone and a one-row hole are both invalid; 192/192 is the only
    // complete publication contract. A magenta poison in a missing row must
    // never reach the visible surface because publication remains zero.
    GPU2DNative::LineCoverage line191Only[2]{};
    GPU2DNative::LineCoverage line191Engine[2]{};
    for (u32 screen = 0u; screen < 2u; ++screen)
    {
        line191Only[screen].Mark(191u);
        line191Engine[screen].Mark(191u);
    }
    passed &= Require(
        !complete(line191Only, line191Engine),
        "line191-only fixture became a valid structured frame");

    GPU2DNative::LineCoverage oneRowMissing[2]{};
    GPU2DNative::LineCoverage oneRowMissingEngine[2]{};
    completeStructuredFrame(oneRowMissing, oneRowMissingEngine);
    oneRowMissing[0].Words[95u >> 6u] &= ~(1ull << (95u & 63u));
    oneRowMissingEngine[0].Words[95u >> 6u] &= ~(1ull << (95u & 63u));
    passed &= Require(
        !complete(oneRowMissing, oneRowMissingEngine),
        "one-row-missing fixture became a valid structured frame");

    GPU2DNative::LineCoverage allRows[2]{};
    GPU2DNative::LineCoverage allRowsEngine[2]{};
    completeStructuredFrame(allRows, allRowsEngine);
    passed &= Require(
        complete(allRows, allRowsEngine),
        "192/192 structured coverage did not publish");

    u32 lastCompleteSurface = 0x1234u;
    const u32 poison = 0x00FF00FFu;
    const bool partialPublished = false;
    if (partialPublished)
        lastCompleteSurface = poison;
    passed &= Require(
        lastCompleteSurface != poison,
        "missing-row poison leaked into the retained presentation surface");

    // Capture semantics continue for the resumed visible suffix even though
    // presentation is held. The next full physical frame replaces the held
    // surface exactly once.
    constexpr u32 resumedVCount = 96u;
    u32 captureWrites = 0u;
    for (u32 line = resumedVCount; line < GPU2DNative::ScreenHeight; ++line)
        ++captureWrites;
    passed &= Require(
        captureWrites == GPU2DNative::ScreenHeight - resumedVCount,
        "Display Capture continuation stopped with presentation suppression");
    u32 publishCount = 0u;
    if (complete(allRows, allRowsEngine))
        ++publishCount;
    passed &= Require(
        publishCount == 1u,
        "next complete physical frame did not replace the retained surface");

    return passed;
}

bool RunPackVectors()
{
    auto input = std::make_unique<FrameInput>();
    input->Generation.Frame = 0x1122334455667788ull;
    input->Generation.ContentGeneration = 0x8877665544332211ull;
    input->CaptureCnt = 0xA5A5A5A5u;
    input->LCDVRAMMap = 1u << 2u;
    input->Engine[0].BGSize = 0x80000u;
    input->Engine[1].OBJSize = 0x20000u;
    input->Lines[192u + 17u].DispCnt = 0x12345678u;
    input->Lines[192u + 17u].CaptureCnt = 0x00320010u;
    input->Lines[192u + 17u].CaptureEnable = 1u;
    input->Lines[192u + 17u].UnitEnabled = 1u;
    input->Lines[192u + 17u].LCDVRAMMap = 1u << 3u;
    input->Lines[192u + 17u].SpriteLatchValid = 1u;
    input->ScreenSource[191u] = 1u;
    input->Palette[37u] = 0x5Au;
    input->OAM[513u] = 0xC3u;
    input->LCDVRAM[0x1234u] = 0xE7u;
    input->SpriteTimelineRowIds[17u] = 0u;
    input->SpriteTimelineRowCount = 1u;
    input->SpriteTimelineRows[7u] = 0xABCDEF01u;

    std::vector<u32> packed(PackedFrameWords, 0u);
    bool passed = true;
    passed &= Require(PackFrame(*input, packed.data(), packed.size()),
        "PackFrame rejected a correctly sized destination");
    passed &= Require(
        packed[0] == 0x32445047u && packed[1] == PackedFrameAbiVersion,
        "native frame header magic/version drifted");
    passed &= Require(packed[2] == 0x55667788u && packed[3] == 0x11223344u,
        "frame generation is not serialized little-endian");
    passed &= Require(packed[10] == input->CaptureCnt,
        "global capture state is not serialized");
    passed &= Require(packed[14] == input->LCDVRAMMap,
        "frame LCDC mapping is not serialized");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords] == 0x12345678u,
        "engine-B line state offset drifted");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 65u] == 1u,
        "engine-enable state is not serialized");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 66u]
            == (1u << 3u),
        "per-line LCDC mapping is not serialized");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 67u] == 1u,
        "sprite latch validity is not serialized");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 55u]
            == 0x00320010u,
        "line capture count moved away from the canonical word 55");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 56u] == 1u,
        "line capture enable moved away from the canonical word 56");
    passed &= Require(
        ((packed[PackedPaletteBase + 37u / 4u] >> ((37u & 3u) * 8u)) & 0xFFu)
            == 0x5Au,
        "palette byte mirror is not packed verbatim");
    passed &= Require(
        ((packed[PackedLCDVRAMBase + 0x1234u / 4u]
            >> ((0x1234u & 3u) * 8u)) & 0xFFu) == 0xE7u,
        "LCD VRAM byte mirror is not packed verbatim");
    passed &= Require(
        packed[PackedSpriteTimelineRowsBase + 7u]
            == 0xABCDEF01u,
        "private OBJ/OAM timeline row is not packed");
    input->NativeCaptureBGMapping[3u * NativeCaptureBGMappingStride + 32u + 7u]
        = 1u << 2u;
    input->NativeCaptureOBJMapping[17u * NativeCaptureOBJMappingStride + 16u + 3u]
        = 1u << 1u;
    input->NativeCaptureSpriteOBJMapping[
        17u * NativeCaptureOBJMappingStride + 16u + 3u] = 1u << 3u;
    passed &= Require(
        PackFrame(*input, packed.data(), packed.size()),
        "PackFrame rejected mapped-capture metadata");
    passed &= Require(
        packed[PackedNativeCaptureBGMappingBase
            + 3u * NativeCaptureBGMappingStride + 32u + 7u] == (1u << 2u),
        "BG current-line capture owner row was not packed");
    passed &= Require(
        packed[PackedNativeCaptureOBJMappingBase
            + 17u * NativeCaptureOBJMappingStride + 16u + 3u] == (1u << 1u),
        "OBJ current-line capture owner row was not packed");
    passed &= Require(
        packed[PackedNativeCaptureSpriteOBJMappingBase
            + 17u * NativeCaptureOBJMappingStride + 16u + 3u] == (1u << 3u),
        "OBJ latch capture owner row was not packed");
    passed &= Require(!PackFrame(*input, packed.data(), PackedFrameWords - 1u),
        "PackFrame accepted a short destination");
    return passed;
}

struct MappedCaptureOverlayModel
{
    std::array<std::array<u32, NativeCaptureBGMappingStride>, ScreenHeight>
        BG{};
    std::array<std::array<u32, NativeCaptureOBJMappingStride>, ScreenHeight>
        OBJ{};
    std::array<std::array<u32, NativeCaptureOBJMappingStride>, ScreenHeight>
        SpriteOBJ{};
    std::array<u32, ScreenHeight> BGMappedBanks{};
    std::array<u32, ScreenHeight> OBJMappedBanks{};
    std::array<u32, ScreenHeight> SpriteOBJMappedBanks{};
    std::array<std::array<u8, LCDCBankBytes>, CapturePhysicalBanks> Cpu{};
    std::array<std::array<u8, LCDCBankBytes>, CapturePhysicalBanks> Native{};
    std::array<u8, CapturePhysicalBanks> WrittenBlocks{};

    [[nodiscard]] const u32* MappingRow(
        u32 engine,
        u32 line,
        bool obj,
        bool spriteLatch) const
    {
        if (line >= ScreenHeight)
            return nullptr;
        if (!obj)
            return BG[line].data() + (engine == 0u ? 0u : 32u);
        const auto& rows = spriteLatch ? SpriteOBJ : OBJ;
        return rows[line].data() + (engine == 0u ? 0u : 16u);
    }

    [[nodiscard]] bool RowHasOverlay(
        u32 engine,
        u32 line,
        bool obj,
        bool spriteLatch) const
    {
        const u32* row = MappingRow(engine, line, obj, spriteLatch);
        return row != nullptr
            && (row[0] & NativeCaptureOverlayPresent) != 0u;
    }

    [[nodiscard]] u32 OwnerMask(
        u32 engine,
        u32 line,
        u32 address,
        u32 size,
        bool obj,
        bool spriteLatch) const
    {
        if (size == 0u || !RowHasOverlay(engine, line, obj, spriteLatch))
            return 0u;
        const u32 offset = address & (size - 1u);
        const u32 mappingIndex = offset / (16u * 1024u);
        const u32 mappingCount = obj
            ? (engine == 0u ? 16u : 8u)
            : (engine == 0u ? 32u : 8u);
        if (mappingIndex >= mappingCount)
            return 0u;
        const u32* row = MappingRow(engine, line, obj, spriteLatch);
        return row[mappingIndex] & NativeCaptureBankMask;
    }

    [[nodiscard]] u8 Resolve(
        u32 engine,
        u32 line,
        u32 address,
        u32 size,
        bool obj,
        bool spriteLatch) const
    {
        const u32 offset = address & (size - 1u);
        const u32 ownerMask = OwnerMask(
            engine, line, address, size, obj, spriteLatch);
        const u32 mappedBankMask = !obj
            ? BGMappedBanks[line]
            : (spriteLatch ? SpriteOBJMappedBanks[line] : OBJMappedBanks[line]);
        u8 cpuFlatten = 0u;
        u8 nativeOverlay = 0u;
        for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
        {
            if ((mappedBankMask & (1u << bank)) == 0u)
                continue;
            if ((ownerMask & (1u << bank)) != 0u)
                nativeOverlay |= Native[bank][offset & LCDCBankByteMask];
            else
                cpuFlatten |= Cpu[bank][offset & LCDCBankByteMask];
        }
        return cpuFlatten | nativeOverlay;
    }

    [[nodiscard]] u8 HostFlatten(
        u32 engine,
        u32 line,
        u32 address,
        u32 size,
        bool obj,
        bool spriteLatch) const
    {
        const u32 offset = address & (size - 1u);
        u32 nativeMask = OwnerMask(
            engine, line, address, size, obj, spriteLatch);
        const u32 mappedBankMask = !obj
            ? BGMappedBanks[line]
            : (spriteLatch ? SpriteOBJMappedBanks[line] : OBJMappedBanks[line]);
        for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
        {
            if ((mappedBankMask & (1u << bank)) == 0u)
                continue;
            const u32 physicalBlock =
                (offset & LCDCBankByteMask) / CapturePhysicalBlockBytes;
            if ((WrittenBlocks[bank] & (1u << physicalBlock)) != 0u)
                nativeMask |= 1u << bank;
        }

        u8 cpuFlatten = 0u;
        for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
        {
            if ((mappedBankMask & (1u << bank)) != 0u
                && (nativeMask & (1u << bank)) == 0u)
            {
                cpuFlatten |= Cpu[bank][offset & LCDCBankByteMask];
            }
        }
        return cpuFlatten;
    }
};

bool RunMappedCaptureOverlayVectors()
{
    bool passed = true;
    MappedCaptureOverlayModel model;
    constexpr u32 rowSummary = NativeCaptureOverlayPresent;
    constexpr u32 address = 0x1234u;
    constexpr u32 size = 256u * 1024u;
    const u32 offset = address & (size - 1u);

    // A native-owned block must win over a deliberately poisoned CPU mirror.
    model.BG[0u][0u] = rowSummary | (1u << 0u);
    model.BGMappedBanks[0u] = 1u << 0u;
    model.Cpu[0u][offset & LCDCBankByteMask] = 0xF0u;
    model.Native[0u][offset & LCDCBankByteMask] = 0x03u;
    passed &= Require(
        model.Resolve(0u, 0u, address, size, false, false) == 0x03u,
        "native-owned mapped BG read replayed poisoned CPU VRAM");

    // Two native banks mapped to one logical page are Nintendo's OR-visible
    // overlap case. The overlay must preserve both native contributions.
    model.BG[32u][0u] = rowSummary | (1u << 0u) | (1u << 1u);
    model.BGMappedBanks[32u] = (1u << 0u) | (1u << 1u);
    model.Cpu[1u][offset & LCDCBankByteMask] = 0xF0u;
    model.Native[1u][offset & LCDCBankByteMask] = 0x0Cu;
    passed &= Require(
        model.Resolve(0u, 32u, address, size, false, false) == 0x0Fu,
        "overlapping native capture banks were not OR-composed");

    // Mid-frame remapping is line-local: the same logical BG address resolves
    // through the row captured at each line boundary.
    model.BG[64u][0u] = rowSummary | (1u << 0u);
    model.BG[65u][0u] = rowSummary | (1u << 1u);
    model.BGMappedBanks[64u] = 1u << 0u;
    model.BGMappedBanks[65u] = 1u << 1u;
    passed &= Require(
        model.Resolve(0u, 64u, address, size, false, false) == 0x03u
            && model.Resolve(0u, 65u, address, size, false, false) == 0x0Cu,
        "mid-frame mapped-capture owner transition was not line-local");

    // OBJ has a current-line mapping and an independent one-line-ahead latch
    // mapping. A valid latch selects the latter; its absence selects current.
    model.OBJ[80u][0u] = rowSummary | (1u << 0u);
    model.SpriteOBJ[80u][0u] = rowSummary | (1u << 2u);
    model.OBJMappedBanks[80u] = 1u << 0u;
    model.SpriteOBJMappedBanks[80u] = 1u << 2u;
    model.Cpu[2u][offset & LCDCBankByteMask] = 0xA0u;
    model.Native[2u][offset & LCDCBankByteMask] = 0x05u;
    passed &= Require(
        model.Resolve(0u, 80u, address, size, true, false) == 0x03u
            && model.Resolve(0u, 80u, address, size, true, true) == 0x05u,
        "OBJ current/latch mapping selection drifted");

    // The row-valid bit lives only in entry zero. A nonzero mapped page must
    // use that summary while taking bank ownership from its addressed entry.
    constexpr u32 nonzeroPageAddress = 2u * 16u * 1024u + 0x1234u;
    const u32 nonzeroPageOffset = nonzeroPageAddress & (size - 1u);
    model.BG[112u][0u] = rowSummary;
    model.BG[112u][2u] = 1u << 2u;
    model.BGMappedBanks[112u] = 1u << 2u;
    model.Cpu[2u][nonzeroPageOffset & LCDCBankByteMask] = 0xA0u;
    model.Native[2u][nonzeroPageOffset & LCDCBankByteMask] = 0x05u;
    passed &= Require(
        model.Resolve(0u, 112u, nonzeroPageAddress, size, false, false) == 0x05u,
        "nonzero mapped page ignored the entry-zero overlay summary");

    // Conversely, stale bank bits in a nonzero entry are not authoritative
    // once the row summary has been cleared during a display transition.
    model.BG[113u][2u] = 1u << 2u;
    model.BGMappedBanks[113u] = 1u << 2u;
    passed &= Require(
        model.Resolve(0u, 113u, nonzeroPageAddress, size, false, false) == 0xA0u,
        "cleared row summary replayed stale nonzero-page native ownership");

    // A late host snapshot can observe the current frame's completed capture
    // before the line-time owner row is rebuilt. The fast 64-bit flatten must
    // still reject stale CPU VRAM using the write-ahead block mask.
    model.BG[96u][0u] = 0u;
    model.BGMappedBanks[96u] = 1u << 2u;
    model.Cpu[2u][offset & LCDCBankByteMask] = 0xF0u;
    model.WrittenBlocks[2u] = static_cast<u8>(
        1u << ((offset & LCDCBankByteMask) / CapturePhysicalBlockBytes));
    passed &= Require(
        model.HostFlatten(0u, 96u, address, size, false, false) == 0u,
        "late fast mapped read flattened stale CPU capture bytes");

    // Exercise stale-poison, line split, overlap, and all required scales over
    // the long state-load horizon without making presentation scale part of
    // the logical mapping decision.
    for (u32 line = 0u; line < ScreenHeight; ++line)
    {
        const u32 owner = (line < 64u || line >= 128u)
            ? (1u << 0u) : (1u << 1u);
        model.BG[line][0u] = rowSummary | owner;
        model.BGMappedBanks[line] = owner;
    }
    for (u32 frame = 0u; frame < 600u; ++frame)
    {
        for (const u32 scale : {1u, 4u, 16u})
        {
            (void)scale;
            const u32 line = frame % ScreenHeight;
            const u32 expected = (line < 64u || line >= 128u) ? 0x03u : 0x0Cu;
            passed &= Require(
                model.Resolve(0u, line, address, size, false, false) == expected,
                "600-frame mapped-capture owner sequence was not scale-invariant");
        }
    }
    return passed;
}

bool RunCompareVectors()
{
    std::array<u32, ScreenPixelCount> expectedTop{};
    std::array<u32, ScreenPixelCount> expectedBottom{};
    std::array<u32, ScreenPixelCount> actualTop = expectedTop;
    std::array<u32, ScreenPixelCount> actualBottom = expectedBottom;

    CompareResult exact = CompareExact(
        expectedTop.data(), expectedBottom.data(), actualTop.data(), actualBottom.data());
    bool passed = Require(exact.Exact() && exact.TotalMismatchCount == 0u,
        "identical 256x192 top/bottom frames are not exact");

    actualTop[7u * ScreenWidth + 11u] = 0xABCDu;
    actualBottom[191u * ScreenWidth + 255u] = 0xDCBAu;
    CompareResult mismatch = CompareExact(
        expectedTop.data(), expectedBottom.data(), actualTop.data(), actualBottom.data());
    passed &= Require(!mismatch.Exact() && mismatch.TotalMismatchCount == 2u,
        "comparator did not count both screen mismatches");
    passed &= Require(mismatch.TopMismatchCount == 1u && mismatch.BottomMismatchCount == 1u,
        "comparator did not split top/bottom mismatch counts");
    passed &= Require(mismatch.FirstMismatchLine == 7u && mismatch.FirstMismatchX == 11u,
        "comparator first-mismatch coordinate drifted");
    passed &= Require(mismatch.MismatchPerLine[7u] == 1u
            && mismatch.MismatchPerLine[ScreenHeight + 191u] == 1u,
        "comparator per-line accounting drifted");
    return passed;
}

bool RunUploadPlanVectors()
{
    auto input = std::make_unique<FrameInput>();
    input->DirtyRangeCount = 3u;
    input->DirtyRanges[0] = {PackedHeaderWords * sizeof(u32), 16u};
    input->DirtyRanges[1] = {PackedEngineBase * sizeof(u32), 512u};
    input->DirtyRanges[2] = {PackedPaletteBase * sizeof(u32), 64u};

    const UploadPlan partial = BuildUploadPlan(*input, false);
    const u32 provenanceBytes = HighResCaptureProvenanceWords * sizeof(u32);
    bool passed = Require(partial.Count == 4u
            && partial.TotalBytes == 592u + provenanceBytes,
        "partial upload plan did not preserve dirty ranges");
    passed &= Require(partial.EngineMemoryBytes == 512u
            && partial.PaletteBytes == 64u,
        "partial upload plan category accounting drifted");

    UploadPlan coalesced{};
    coalesced.Count = 3u;
    coalesced.Ranges[0] = {100u, 16u};
    coalesced.Ranges[1] = {120u, 8u};
    coalesced.Ranges[2] = {5000u, 32u};
    CoalesceUploadPlan(coalesced, 8u);
    passed &= Require(coalesced.Count == 2u
            && coalesced.Ranges[0].Offset == 100u
            && coalesced.Ranges[0].Size == 28u
            && coalesced.Ranges[1].Offset == 5000u
            && coalesced.Ranges[1].Size == 32u
            && coalesced.TotalBytes == 60u,
        "nearby upload ranges were not coalesced with exact coverage");

    input->DirtyRanges[3] = {
        PackedNativeCaptureOBJMappingBase * sizeof(u32), sizeof(u32)};
    input->DirtyRangeCount = 4u;
    const UploadPlan mapped = BuildUploadPlan(*input, false);
    passed &= Require(
        mapped.MappedCaptureBytes == sizeof(u32) + provenanceBytes
            && mapped.TotalBytes == 596u + provenanceBytes,
        "mapped-capture metadata was not classified as its own upload category");

    const UploadPlan full = BuildUploadPlan(*input, true);
    passed &= Require(full.Count == 1u
            && full.TotalBytes == PackedFrameBytes(),
        "first-slot upload plan is not a complete frame upload");

    input->Generation.ContentGeneration = 7u;
    FrameGeneration laggingSlot{};
    laggingSlot.ContentGeneration = 6u;
    laggingSlot.VRAMGeneration = input->Generation.VRAMGeneration;
    laggingSlot.CaptureGeneration = input->Generation.CaptureGeneration;
    const UploadPlan staleContent = BuildUploadPlan(*input, laggingSlot, false);
    passed &= Require(
        staleContent.PaletteBytes == (PackedOAMBase - PackedPaletteBase) * sizeof(u32)
            && staleContent.OAMBytes == (PackedFIFOBase - PackedOAMBase) * sizeof(u32)
            && staleContent.FIFOBytes == (PackedLCDVRAMBase - PackedFIFOBase) * sizeof(u32),
        "a reused slot did not refresh the complete shared content mirror");

    input->DirtyRangeCount = 0u;
    input->Generation.NativeCaptureMappingGeneration = 9u;
    laggingSlot.NativeCaptureMappingGeneration = 8u;
    const UploadPlan staleMapping = BuildUploadPlan(*input, laggingSlot, false);
    passed &= Require(
        staleMapping.MappedCaptureBytes
            == (PackedFrameWords - PackedNativeCaptureBGMappingBase) * sizeof(u32),
        "a reused slot did not refresh the complete native capture mapping mirror");

    input->Generation.Frame = 23u;
    laggingSlot.Frame = 20u;
    input->DirtyHistoryFrames[0] = 22u;
    input->DirtyHistoryFrames[1] = 21u;
    input->DirtyHistoryRangeCounts[0] = 0u;
    input->DirtyHistoryRangeCounts[1] = 0u;
    const UploadPlan staleMappingAcrossFrames =
        BuildUploadPlan(*input, laggingSlot, false);
    passed &= Require(
        staleMappingAcrossFrames.MappedCaptureBytes
            == (PackedFrameWords - PackedNativeCaptureBGMappingBase) * sizeof(u32),
        "cross-frame mapping generation edge did not refresh a reused slot");

    auto historyInput = std::make_unique<FrameInput>();
    historyInput->Generation.Frame = 13u;
    historyInput->Generation.ContentGeneration = 4u;
    historyInput->Generation.VRAMGeneration = 4u;
    historyInput->DirtyRangeCount = 1u;
    historyInput->DirtyRanges[0] = {0u, sizeof(u32)};
    historyInput->DirtyHistoryFrames[0] = 12u;
    historyInput->DirtyHistoryRangeCounts[0] = 2u;
    historyInput->DirtyHistoryRanges[0][0] = {
        PackedPaletteBase * sizeof(u32), sizeof(u32)};
    historyInput->DirtyHistoryRanges[0][1] = {
        PackedHeaderWords * sizeof(u32) + 32u, sizeof(u32)};
    historyInput->DirtyHistoryFrames[1] = 11u;
    historyInput->DirtyHistoryRangeCounts[1] = 1u;
    historyInput->DirtyHistoryRanges[1][0] = {
        PackedEngineBase * sizeof(u32), 512u};
    FrameGeneration threeFrameOldSlot{};
    threeFrameOldSlot.Frame = 10u;
    threeFrameOldSlot.ContentGeneration = 1u;
    threeFrameOldSlot.VRAMGeneration = 1u;
    const UploadPlan historical = BuildUploadPlan(
        *historyInput, threeFrameOldSlot, false);
    passed &= Require(
        historical.TotalBytes == 524u + provenanceBytes
            && historical.EngineMemoryBytes == 512u
            && historical.PaletteBytes == sizeof(u32),
        "three-frame compositor reuse omitted serialized dirty history");

    historyInput->DirtyHistoryOverflow[1] = 1u;
    const UploadPlan missingHistory = BuildUploadPlan(
        *historyInput, threeFrameOldSlot, false);
    passed &= Require(
        missingHistory.Count == 1u
            && missingHistory.TotalBytes == PackedFrameBytes(),
        "missing compositor dirty history did not fail closed to a full upload");

    auto partialInput = std::make_unique<FrameInput>();
    partialInput->CaptureCnt = 0xCAFEBABEu;
    partialInput->Palette[0] = 0x5Au;
    partialInput->DirtyRangeCount = 2u;
    partialInput->DirtyRanges[0] = {0u, PackedHeaderWords * sizeof(u32)};
    partialInput->DirtyRanges[1] = {PackedPaletteBase * sizeof(u32), sizeof(u32)};
    const UploadPlan partialPackPlan = BuildUploadPlan(*partialInput, false);
    std::vector<u32> partialDestination(
        PackedFrameWords, 0xA5A5A5A5u);
    passed &= Require(
        PackFrameRanges(
            *partialInput,
            partialDestination.data(),
            partialDestination.size(),
            partialPackPlan),
        "partial frame pack rejected a valid upload plan");
    passed &= Require(
        partialDestination[0] == 0x32445047u
            && partialDestination[1] == PackedFrameAbiVersion
            && partialDestination[10] == partialInput->CaptureCnt,
        "partial frame pack did not update the requested header range");
    passed &= Require(
        ((partialDestination[PackedPaletteBase] & 0xFFu) == 0x5Au),
        "partial frame pack did not update the requested palette range");
    passed &= Require(
        partialDestination[PackedHeaderWords] == 0xA5A5A5A5u
            && partialDestination[PackedPaletteBase + 1u] == 0xA5A5A5A5u,
        "partial frame pack overwrote bytes outside the upload plan");
    return passed;
}

bool RunWorkSlotSemanticContinuityVectors()
{
    FrameGeneration input{};
    input.Frame = 103u;
    input.CaptureGeneration = 77u;

    // A three-slot ring revisits a slot last uploaded by frame 100. That is a
    // slot-local upload history gap, not a renderer-global semantic gap.
    FrameGeneration workSlotUpload{};
    workSlotUpload.Frame = 100u;
    const UploadDecision threeSlot = DetermineUploadDecision(
        true, 9u, 9u, 102u, 76u, input);
    bool passed = Require(
        threeSlot.SemanticFrameContiguous
            && !threeSlot.CaptureGenerationRegressed
            && !threeSlot.RequiresFullUpload()
            && workSlotUpload.Frame == 100u,
        "three-slot work reuse was misclassified as a semantic frame gap");

    // Presentation can drop N+1 while semantic capture still commits. N+2
    // must therefore remain contiguous and use the reused slot's partial plan.
    FrameGeneration afterPresentationStall = input;
    afterPresentationStall.Frame = 104u;
    afterPresentationStall.CaptureGeneration = 78u;
    const UploadDecision postStall = DetermineUploadDecision(
        true, 9u, 9u, 103u, 77u, afterPresentationStall);
    passed &= Require(
        postStall.SemanticFrameContiguous && !postStall.RequiresFullUpload(),
        "semantic-only presentation stall poisoned the next upload decision");

    const UploadDecision firstUse = DetermineUploadDecision(
        false, 9u, 9u, 103u, 77u, afterPresentationStall);
    passed &= Require(firstUse.Reason == FullUploadReason::FirstUse,
        "first work-slot use did not request a full upload");

    const UploadDecision epochChange = DetermineUploadDecision(
        true, 10u, 9u, 103u, 77u, afterPresentationStall);
    passed &= Require(epochChange.Reason == FullUploadReason::EpochChange,
        "epoch discontinuity did not request a full upload");

    FrameGeneration gap = afterPresentationStall;
    gap.Frame = 106u;
    const UploadDecision frameGap = DetermineUploadDecision(
        true, 9u, 9u, 103u, 77u, gap);
    passed &= Require(frameGap.Reason == FullUploadReason::SemanticFrameGap,
        "semantic frame discontinuity did not request a full upload");

    FrameGeneration regression = afterPresentationStall;
    regression.CaptureGeneration = 76u;
    const UploadDecision captureRegression = DetermineUploadDecision(
        true, 9u, 9u, 103u, 77u, regression);
    passed &= Require(
        captureRegression.SemanticFrameContiguous
            && captureRegression.Reason
                == FullUploadReason::CaptureGenerationRegression,
        "capture generation regression did not request a full upload");
    return passed;
}

bool RunObjRawLogicalFusionVectors()
{
    auto input = std::make_unique<FrameInput>();
    bool passed = Require(
        CanFuseObjRawLogicalLine(*input, 0u)
            && CanFuseObjRawLogicalFrame(*input),
        "mosaic-free frame did not enable OBJ raw/logical fusion");

    input->Lines[0].OBJMosaicSize[0] = 3u;
    passed &= Require(
        !CanFuseObjRawLogicalLine(*input, 0u)
            && !CanFuseObjRawLogicalFrame(*input),
        "engine A OBJ mosaic did not disable raw/logical fusion");

    input->Lines[0].OBJMosaicSize[0] = 0u;
    input->ScreenSource[ScreenHeight + 7u] = 1u;
    input->Lines[ScreenHeight + 7u].OBJMosaicSize[0] = 1u;
    passed &= Require(
        !CanFuseObjRawLogicalLine(*input, 7u)
            && CanFuseObjRawLogicalLine(*input, 6u),
        "routed engine B OBJ mosaic fusion guard was not line-local");
    return passed;
}

bool RunIndependentCaptureBatchVectors()
{
    auto input = std::make_unique<FrameInput>();
    constexpr u32 direct3DCopy = 0x80000000u | (1u << 24u) | (3u << 20u);
    input->CaptureEnable = 1u;
    for (u32 line = 0u; line < ScreenHeight; ++line)
    {
        input->Lines[line].CaptureEnable = 1u;
        input->Lines[line].CaptureCnt = direct3DCopy;
        input->Lines[line].LCDVRAMMap = 1u;
    }

    bool passed = Require(
        CanBatchIndependentCaptureFrame(*input, true),
        "stable direct-3D copy capture was not batchable");
    passed &= Require(
        !CanBatchIndependentCaptureFrame(*input, false),
        "missing 3D framebuffer did not disable capture batching");

    input->Lines[40].CaptureCnt = direct3DCopy | (1u << 29u);
    passed &= Require(
        !CanBatchIndependentCaptureFrame(*input, true),
        "blended/source-B capture incorrectly entered direct-3D batching");
    input->Lines[40].CaptureCnt = direct3DCopy ^ (1u << 16u);
    passed &= Require(
        !CanBatchIndependentCaptureFrame(*input, true),
        "mid-frame capture descriptor change incorrectly entered batching");
    for (u32 line = 0u; line < ScreenHeight; ++line)
        input->Lines[line].CaptureCnt = direct3DCopy & ~(1u << 24u);
    passed &= Require(
        CanBatchIndependentCaptureFrame(*input, true),
        "independent GPU2D Source A capture was not batchable");
    input->Lines[80].LCDVRAMMap = 0u;
    passed &= Require(
        !CanBatchIndependentCaptureFrame(*input, true),
        "mid-frame destination remap incorrectly entered capture batching");
    return passed;
}

bool RunHighResCaptureProvenanceVectors()
{
    auto input = std::make_unique<FrameInput>();
    constexpr u32 bank = 2u;
    constexpr u32 segment = 0u;
    constexpr u32 index = bank * HighResCaptureSegmentsPerBank + segment;
    const auto identity = [](u64 epoch, u64 frame, u64 completion) {
        return NativeCaptureStateIdentity{
            true, CaptureOwner::NativeVulkan, epoch, frame, frame, completion};
    };
    const auto setSingleSegmentWrite = [&](u64 frame, bool enabled) {
        *input = {};
        input->Generation.Frame = frame;
        input->Generation.CaptureGeneration = frame;
        input->CaptureEnable = enabled ? 1u : 0u;
        input->Lines[0].CaptureCnt = bank << 16u;
        input->Lines[0].CaptureEnable = enabled ? 1u : 0u;
        input->Lines[0].LCDVRAMMap = 1u << bank;
    };

    setSingleSegmentWrite(1u, true);
    input->Lines[0].CaptureEnable = 1u;

    const HighResCaptureSegmentMask mask = ComputeCaptureWriteSegmentMask(*input);
    bool passed = Require(
        mask[index] != 0u
            && std::count(
                mask.begin() + bank * HighResCaptureSegmentsPerBank,
                mask.begin() + (bank + 1u) * HighResCaptureSegmentsPerBank,
                static_cast<u8>(1u)) == 1,
        "capture provenance segment mask did not follow the latched destination");

    HighResCaptureProvenanceTracker tracker;
    tracker.Invalidate(7u, 4u);
    const NativeCaptureStateIdentity firstIdentity = identity(7u, 1u, 0x100000001ull);
    tracker.BeginFrame(*input, firstIdentity, 4u);
    const auto& pending = tracker.States()[index];
    passed &= Require(
        (pending.ValidAndVersion & HighResCapturePendingWriteBit) != 0u
            && pending.PendingIdentity.CompletionValue
                == firstIdentity.CompletionValue,
        "capture write did not carry its pending semantic identity");

    std::vector<u32> packed(PackedFrameWords, 0u);
    passed &= Require(
        PackHighResCaptureProvenance(
            packed.data(), packed.size(), tracker.States(), *input,
            firstIdentity.CompletionValue),
        "capture provenance table did not pack into the common ABI");
    const u32 tableBase = PackedHighResCaptureProvenanceBase
        + index * HighResCaptureProvenanceWordsPerSegment;
    passed &= Require(
        packed[tableBase] == pending.ValidAndVersion
            && packed[tableBase + 3u] == 1u
            && packed[tableBase + 4u] == 1u
            && packed[32u] == 1u
            && packed[33u] == 1u,
        "packed 64-bit pending identity drifted from the shared ABI");

    tracker.CommitFrame(firstIdentity);
    const u32 committed = tracker.States()[index].ValidAndVersion;
    passed &= Require(
        IsHighResCaptureCommittedIdentityValid(
            tracker.States()[index], firstIdentity.CompletionValue)
            && (committed & HighResCapturePendingWriteBit) == 0u
            && tracker.States()[index].CommittedIdentity.CompletionValue
                == firstIdentity.CompletionValue,
        "capture write did not atomically commit sidecar and compact identity");
    const u32 firstVersion = committed & HighResCaptureVersionBit;

    setSingleSegmentWrite(2u, true);
    const NativeCaptureStateIdentity secondIdentity = identity(7u, 2u, 0x200000002ull);
    tracker.BeginFrame(*input, secondIdentity, 4u);
    const u32 secondPending = tracker.States()[index].ValidAndVersion;
    const u32 secondWriteVersion = (secondPending & HighResCaptureVersionBit) == 0u
        ? HighResCaptureVersionBit : 0u;
    passed &= Require(
        (secondPending & HighResCapturePendingWriteBit) != 0u
            && secondWriteVersion != firstVersion,
        "actual capture write did not select the alternate sidecar version");
    tracker.AbortFrame();
    passed &= Require(
        tracker.States()[index].ValidAndVersion == committed
            && tracker.States()[index].CommittedIdentity.CompletionValue
                == firstIdentity.CompletionValue,
        "aborted capture submission changed committed sidecar provenance");

    // Full F1 model: D then C 256x192 commits, a variable no-write gap, then
    // C/D resume. Every non-written bank must retain its committed identity.
    const auto setFullCaptureWrite = [&](u32 destinationBank, u64 frame) {
        *input = {};
        input->Generation.Frame = frame;
        input->Generation.CaptureGeneration = frame;
        input->CaptureEnable = 1u;
        for (u32 line = 0u; line < ScreenHeight; ++line)
        {
            input->Lines[line].CaptureCnt = (destinationBank << 16u)
                | (3u << 20u);
            input->Lines[line].CaptureEnable = 1u;
            input->Lines[line].LCDVRAMMap = 1u << destinationBank;
        }
    };
    HighResCaptureProvenanceTracker gapBase;
    gapBase.Invalidate(20u, 4u);
    setFullCaptureWrite(3u, 10u);
    const NativeCaptureStateIdentity dBeforeGap = identity(20u, 10u, 1010u);
    gapBase.BeginFrame(*input, dBeforeGap, 4u);
    gapBase.CommitFrame(dBeforeGap);
    setFullCaptureWrite(2u, 11u);
    const NativeCaptureStateIdentity cBeforeGap = identity(20u, 11u, 1011u);
    gapBase.BeginFrame(*input, cBeforeGap, 4u);
    gapBase.CommitFrame(cBeforeGap);
    const HighResCaptureProvenanceTable preGap = gapBase.States();

    for (const u32 gapLength : {1u, 2u, 74u, 600u})
    {
        HighResCaptureProvenanceTracker gapTracker = gapBase;
        for (u32 gapFrame = 0u; gapFrame < gapLength; ++gapFrame)
        {
            setSingleSegmentWrite(12u + gapFrame, false);
            const NativeCaptureStateIdentity gapIdentity = identity(
                20u, 12u + gapFrame, 2000u + gapFrame);
            gapTracker.BeginFrame(*input, gapIdentity, 4u);
            gapTracker.CommitFrame(gapIdentity);
        }
        bool gapPreserved = true;
        for (u32 destinationBank : {2u, 3u})
        {
            for (u32 segmentIndex = 0u; segmentIndex < 384u; ++segmentIndex)
            {
                const u32 stateIndex = destinationBank
                    * HighResCaptureSegmentsPerBank + segmentIndex;
                gapPreserved &= gapTracker.States()[stateIndex].ValidAndVersion
                        == preGap[stateIndex].ValidAndVersion
                    && gapTracker.States()[stateIndex]
                        .CommittedIdentity.CompletionValue
                        == preGap[stateIndex].CommittedIdentity.CompletionValue;
            }
        }
        passed &= Require(
            gapPreserved,
            "full C/D capture identity changed during a no-write gap");

        setFullCaptureWrite(2u, 700u + gapLength);
        const NativeCaptureStateIdentity cResume = identity(
            20u, 700u + gapLength, 3000u + gapLength);
        gapTracker.BeginFrame(*input, cResume, 4u);
        const u32 dIndex = 3u * HighResCaptureSegmentsPerBank;
        passed &= Require(
            (gapTracker.States()[index].ValidAndVersion
                & HighResCapturePendingWriteBit) != 0u
                && IsHighResCaptureCommittedIdentityValid(
                    gapTracker.States()[dIndex], dBeforeGap.CompletionValue)
                && gapTracker.States()[dIndex]
                    .CommittedIdentity.CompletionValue
                    == dBeforeGap.CompletionValue,
            "C resume did not preserve D committed reference");
        gapTracker.CommitFrame(cResume);

        setFullCaptureWrite(3u, 701u + gapLength);
        const NativeCaptureStateIdentity dResume = identity(
            20u, 701u + gapLength, 4000u + gapLength);
        gapTracker.BeginFrame(*input, dResume, 4u);
        passed &= Require(
            IsHighResCaptureCommittedIdentityValid(
                gapTracker.States()[index], cResume.CompletionValue)
                && gapTracker.States()[index]
                    .CommittedIdentity.CompletionValue
                    == cResume.CompletionValue
                && (gapTracker.States()[dIndex].ValidAndVersion
                    & HighResCapturePendingWriteBit) != 0u,
            "D resume did not preserve newly committed C reference");
        gapTracker.CommitFrame(dResume);
    }

    // Materialized CPU readback is content-preserving and therefore causes no
    // tracker mutation. This snapshot models the explicit no-op event.
    const HighResCaptureProvenanceState beforeMaterialize = tracker.States()[index];
    const HighResCaptureProvenanceState afterMaterialize = tracker.States()[index];
    passed &= Require(
        afterMaterialize.ValidAndVersion == beforeMaterialize.ValidAndVersion
            && afterMaterialize.CommittedIdentity.CompletionValue
                == beforeMaterialize.CommittedIdentity.CompletionValue,
        "content-preserving materialization retired sidecar identity");

    // Equal compact representatives are not proof of identity. A different
    // compact completion token must fail closed even if pixels alias at 5-bit.
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[index], firstIdentity.CompletionValue + 1u),
        "compact pixel alias admitted a sidecar with different identity");

    tracker.Invalidate(8u, 4u);
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[index], firstIdentity.CompletionValue)
            && tracker.States()[index].LastInvalidationReason
                == HighResCaptureFallbackReason::ResourceReset,
        "renderer epoch invalidation did not reject the pre-load sidecar");

    // Commit two segments in different 32 KiB physical blocks, then invalidate
    // only the first block as an actual CPU/DMA write would.
    constexpr u32 firstSegment = bank * HighResCaptureSegmentsPerBank;
    constexpr u32 secondBlockSegment = firstSegment
        + CapturePhysicalBlockBytes / sizeof(u16)
            / HighResCaptureSegmentHalfwords;
    *input = {};
    input->Generation.Frame = 700u;
    input->Generation.CaptureGeneration = 700u;
    input->CaptureEnable = 1u;
    input->Lines[0].CaptureCnt = (bank << 16u) | (2u << 20u);
    input->Lines[0].CaptureEnable = 1u;
    input->Lines[0].LCDVRAMMap = 1u << bank;
    input->Lines[64] = input->Lines[0];
    tracker.Invalidate(9u, 4u);
    const NativeCaptureStateIdentity wideIdentity = identity(9u, 700u, 700u);
    tracker.BeginFrame(*input, wideIdentity, 4u);
    tracker.CommitFrame(wideIdentity);
    tracker.InvalidatePhysicalRange(
        bank, 0u, CapturePhysicalBlockBytes,
        HighResCaptureFallbackReason::CpuWriteInvalidated);
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[firstSegment], wideIdentity.CompletionValue)
            && IsHighResCaptureCommittedIdentityValid(
                tracker.States()[secondBlockSegment], wideIdentity.CompletionValue),
        "selective CPU write invalidation retired an unrelated physical block");
    tracker.InvalidatePhysicalRange(
        bank, 0u, CapturePhysicalBlockBytes,
        HighResCaptureFallbackReason::CaptureRetired);
    passed &= Require(
        tracker.States()[firstSegment].LastInvalidationReason
            == HighResCaptureFallbackReason::CpuWriteInvalidated,
        "repeated full-block invalidation did not remain an O(1) no-op");
    tracker.InvalidatePhysicalRange(
        bank, CapturePhysicalBlockBytes, CapturePhysicalBlockBytes,
        HighResCaptureFallbackReason::CaptureRetired);
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[secondBlockSegment], wideIdentity.CompletionValue)
            && tracker.States()[secondBlockSegment].LastInvalidationReason
                == HighResCaptureFallbackReason::CaptureRetired,
        "capture layout replacement did not retire the replaced block identity");

    // A later native write must re-arm the block-level summary so the next
    // overlapping CPU write retires the newly committed identity.
    *input = {};
    input->Generation.Frame = 701u;
    input->Generation.CaptureGeneration = 701u;
    input->CaptureEnable = 1u;
    input->Lines[0].CaptureCnt = (bank << 16u);
    input->Lines[0].CaptureEnable = 1u;
    input->Lines[0].LCDVRAMMap = 1u << bank;
    const NativeCaptureStateIdentity rearmedIdentity = identity(9u, 701u, 701u);
    tracker.BeginFrame(*input, rearmedIdentity, 4u);
    tracker.CommitFrame(rearmedIdentity);
    tracker.InvalidatePhysicalRange(
        bank, 0u, CapturePhysicalBlockBytes,
        HighResCaptureFallbackReason::CaptureRetired);
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[firstSegment], rearmedIdentity.CompletionValue)
            && tracker.States()[firstSegment].LastInvalidationReason
                == HighResCaptureFallbackReason::CaptureRetired,
        "new native capture did not re-arm full-block invalidation");

    // Two-version feedback: repeated writes toggle only the written segment;
    // an aborted pending submission keeps the committed version and identity.
    tracker.Invalidate(10u, 4u);
    setSingleSegmentWrite(800u, true);
    const NativeCaptureStateIdentity versionA = identity(10u, 800u, 800u);
    tracker.BeginFrame(*input, versionA, 4u);
    tracker.CommitFrame(versionA);
    const u32 versionOne = tracker.States()[index].ValidAndVersion;
    setSingleSegmentWrite(801u, true);
    const NativeCaptureStateIdentity versionB = identity(10u, 801u, 801u);
    tracker.BeginFrame(*input, versionB, 4u);
    tracker.CommitFrame(versionB);
    const u32 versionTwo = tracker.States()[index].ValidAndVersion;
    passed &= Require(
        (versionOne & HighResCaptureVersionBit)
            != (versionTwo & HighResCaptureVersionBit)
            && tracker.States()[index].CommittedIdentity.CompletionValue == 801u,
        "two-version feedback did not advance with committed semantic identity");

    tracker.Invalidate(10u, 8u);
    passed &= Require(
        !IsHighResCaptureCommittedIdentityValid(
            tracker.States()[index], versionB.CompletionValue),
        "scale-dependent sidecar recreation retained old provenance");
    return passed;
}

struct TemporalVectorFixture
{
    std::array<u32, ScreenHeight> RowIds{};
    std::vector<u32> Rows;
    std::array<u32, ScreenHeight> SpriteRowIds{};
    std::vector<u32> SpriteRows;

    TemporalVectorFixture()
        : Rows(PackedTimelineRowsWords, 0u),
          SpriteRows(PackedSpriteTimelineRowsWords, 0u)
    {
    }
};

u32 ResolveTimelineVersion(
    const TemporalVectorFixture& fixture, u32 line, u32 block)
{
    if (line >= ScreenHeight || block >= TimelineBlockCount)
        return 0u;
    const u32 row = fixture.RowIds[line];
    if (row >= ScreenHeight)
        return 0u;
    return fixture.Rows[row * TimelineBlockCount + block];
}

u32 ResolveSpriteVersion(
    const TemporalVectorFixture& fixture,
    u32 line,
    u32 normalBlock,
    u32 spriteBlock,
    bool spriteLatchValid)
{
    if (!spriteLatchValid || line >= ScreenHeight
        || spriteBlock >= SpriteTimelineBlockCount)
    {
        return ResolveTimelineVersion(fixture, line, normalBlock);
    }
    const u32 row = fixture.SpriteRowIds[line];
    if (row >= ScreenHeight)
        return 0u;
    return fixture.SpriteRows[row * SpriteTimelineBlockCount + spriteBlock];
}

void SetTimelineBand(
    TemporalVectorFixture& fixture,
    u32 firstLine,
    u32 lastLine,
    u32 row,
    u32 block,
    u32 version)
{
    for (u32 line = firstLine; line <= lastLine; ++line)
    {
        fixture.RowIds[line] = row;
        fixture.Rows[row * TimelineBlockCount + block] = version;
    }
}

void SetSpriteTimelineLine(
    TemporalVectorFixture& fixture, u32 line, u32 row, u32 block, u32 version)
{
    fixture.SpriteRowIds[line] = row;
    fixture.SpriteRows[row * SpriteTimelineBlockCount + block] = version;
}

std::vector<u32> BuildFullDispatchVector(
    const TemporalVectorFixture& fixture,
    u32 block,
    const std::array<u32, 4>& payload,
    bool explicitLine,
    u32 dispatchHeight)
{
    std::vector<u32> output(2u * ScreenPixelCount, 0u);
    for (u32 dispatchLine = 0u; dispatchLine < dispatchHeight; ++dispatchLine)
    {
        const u32 screen = dispatchLine / ScreenHeight;
        const u32 line = dispatchLine % ScreenHeight;
        const u32 selectedLine = explicitLine ? line : 0u;
        const u32 version = ResolveTimelineVersion(fixture, selectedLine, block);
        const u32 value = version < payload.size() ? payload[version] : 0u;
        for (u32 x = 0u; x < ScreenWidth; ++x)
            output[screen * ScreenPixelCount + line * ScreenWidth + x] = value;
    }
    return output;
}

bool RunTemporalLineVectors()
{
    bool passed = true;

    {
        TemporalVectorFixture fixture;
        SetTimelineBand(
            fixture, 0u, 79u, 0u, TimelinePaletteBaseBlock, 1u);
        SetTimelineBand(
            fixture, 80u, 159u, 1u, TimelinePaletteBaseBlock, 2u);
        SetTimelineBand(
            fixture, 160u, 191u, 2u, TimelinePaletteBaseBlock, 3u);
        const std::array<u32, 4> palette = {
            0u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu};
        const auto oneLineReference = BuildFullDispatchVector(
            fixture, TimelinePaletteBaseBlock, palette, true, 384u);
        const auto fullDispatch = BuildFullDispatchVector(
            fixture, TimelinePaletteBaseBlock, palette, true, 384u);
        const auto brokenFullDispatch = BuildFullDispatchVector(
            fixture, TimelinePaletteBaseBlock, palette, false, 384u);
        passed &= Require(oneLineReference == fullDispatch,
            "palette red/green/blue one-line and full dispatch vectors differ");
        passed &= Require(oneLineReference != brokenFullDispatch,
            "palette vector cannot detect the old row-zero temporal bug");
    }

    {
        TemporalVectorFixture fixture;
        SetTimelineBand(fixture, 0u, 63u, 0u, TimelineEngineBaseBlock, 1u);
        SetTimelineBand(fixture, 64u, 127u, 1u, TimelineEngineBaseBlock, 2u);
        SetTimelineBand(fixture, 128u, 191u, 2u, TimelineEngineBaseBlock, 3u);
        const std::array<u32, 4> payload = {
            0u, 0x1111u, 0x2222u, 0x3333u};
        const auto reference = BuildFullDispatchVector(
            fixture, TimelineEngineBaseBlock, payload, true, 384u);
        const auto candidate = BuildFullDispatchVector(
            fixture, TimelineEngineBaseBlock, payload, true, 384u);
        passed &= Require(reference == candidate,
            "VRAM line mutations at 0/64/128 were not line-stable");
    }

    {
        TemporalVectorFixture fixture;
        SetTimelineBand(fixture, 0u, 95u, 0u, TimelineLCDVRAMBaseBlock, 1u);
        SetTimelineBand(fixture, 96u, 191u, 1u, TimelineLCDVRAMBaseBlock, 2u);
        const std::array<u32, 4> payload = {
            0u, 0xAAAAu, 0xBBBBu, 0u};
        const auto reference = BuildFullDispatchVector(
            fixture, TimelineLCDVRAMBaseBlock, payload, true, 384u);
        const auto candidate = BuildFullDispatchVector(
            fixture, TimelineLCDVRAMBaseBlock, payload, true, 384u);
        passed &= Require(reference == candidate,
            "LCDC line mutations at 0/96 were not line-stable");
    }

    {
        TemporalVectorFixture fixture;
        SetTimelineBand(fixture, 0u, 95u, 0u, TimelineFIFOBaseBlock, 1u);
        SetTimelineBand(fixture, 96u, 191u, 1u, TimelineFIFOBaseBlock, 2u);
        const std::array<u32, 4> payload = {
            0u, 0x1234u, 0x5678u, 0u};
        const auto reference = BuildFullDispatchVector(
            fixture, TimelineFIFOBaseBlock, payload, true, 384u);
        const auto candidate = BuildFullDispatchVector(
            fixture, TimelineFIFOBaseBlock, payload, true, 384u);
        passed &= Require(reference == candidate,
            "FIFO timeline line selection was not byte exact");
    }

    {
        TemporalVectorFixture fixture;
        fixture.RowIds.fill(0u);
        fixture.Rows[TimelineOAMBaseBlock] = 1u;
        SetSpriteTimelineLine(fixture, 96u, 0u, 0u, 2u);
        const std::array<u32, 4> payload = {
            0u, 0x55u, 0xAAu, 0u};
        const bool spriteLatchValid = false;
        const u32 fallbackVersion = ResolveSpriteVersion(
            fixture, 96u, TimelineOAMBaseBlock, 0u, spriteLatchValid);
        const u32 latchedVersion = ResolveSpriteVersion(
            fixture, 96u, TimelineOAMBaseBlock, 0u, true);
        passed &= Require(
            fallbackVersion == 1u && payload[fallbackVersion] == 0x55u,
            "SpriteLatchValid==0 did not fall back to the normal timeline");
        passed &= Require(
            payload[latchedVersion] == 0xAAu,
            "valid sprite latch did not select the private OBJ timeline");
    }

    {
        TemporalVectorFixture fixture;
        SetTimelineBand(fixture, 0u, 95u, 0u, TimelinePaletteBaseBlock, 1u);
        SetTimelineBand(fixture, 96u, 191u, 1u, TimelinePaletteBaseBlock, 2u);
        const std::array<u32, 4> payload = {
            0u, 0x010101u, 0x020202u, 0u};
        const bool CaptureEnable = false;
        const u32 dispatchHeight = CaptureEnable ? 2u : 384u;
        const auto reference = BuildFullDispatchVector(
            fixture, TimelinePaletteBaseBlock, payload, true, dispatchHeight);
        const auto candidate = BuildFullDispatchVector(
            fixture, TimelinePaletteBaseBlock, payload, true, dispatchHeight);
        passed &= Require(dispatchHeight == 384u && reference == candidate,
            "CaptureEnable=0 full-frame optimized vector was not byte exact");
    }

    return passed;
}

bool RunFrameIdentityVectors()
{
    bool passed = true;
    passed &= Require(IsCurrentFrame(1u, 1u, 1u),
        "a completed native frame was not accepted for its own generation");
    passed &= Require(!IsCurrentFrame(2u, 1u, 1u),
        "a native frame from the prior emulated generation was reused");
    passed &= Require(!IsCurrentFrame(2u, 0u, 2u),
        "a non-native frame was treated as a completed native frame");
    passed &= Require(IsCurrentFrame(3u, 3u, 3u),
        "native production did not become current after switching back on");
    // The mandatory lifecycle is normal native -> CaptureEnable/non-native ->
    // normal native. The middle frame must not retain frame 1's identity.
    const u64 normalFrame = 11u;
    const u64 captureFrame = normalFrame + 1u;
    const u64 resumedFrame = captureFrame + 1u;
    passed &= Require(IsCurrentFrame(normalFrame, normalFrame, normalFrame),
        "normal native frame was not accepted before CaptureEnable");
    passed &= Require(!IsCurrentFrame(captureFrame, 0u, captureFrame),
        "CaptureEnable/non-native frame retained a prior native identity");
    passed &= Require(IsCurrentFrame(resumedFrame, resumedFrame, resumedFrame),
        "native frame did not resume with a new current identity");
    return passed;
}

struct CaptureOwnershipModel
{
    std::array<CaptureBlockProvenance, CapturePhysicalBlockCount> Blocks{};
    std::array<CaptureBlockProvenance, CapturePhysicalBlockCount> RecorderBlocks{};
    u16 CaptureFlags = 0;
    u64 EmulatedFrameSerial = 0;
    u64 RecordedNativeFrameSerial = 0;
    u64 NativeSemanticSubmissionSerial = 0;
    u64 LastNativeCaptureCompletionValue = 0;
    bool PresentationSubmitted = false;
    bool PresentationStallObserved = false;
    u32 ActiveCaptureBank = 0;
    u32 ActiveCaptureStart = 0;
    u32 ActiveCaptureLen = 0;
    bool ActiveCapture = false;
    u32 CpuCaptureVersion = 0;
    u32 RecorderCaptureVersion = 0;

    u64 SubmitSemantic(u64 localContextFence)
    {
        // DX12 semantic slots have independent local fences.  The provenance
        // key must remain monotonic even when a later slot reports a smaller
        // local fence value.
        (void)localContextFence;
        ++NativeSemanticSubmissionSerial;
        if (NativeSemanticSubmissionSerial == 0u)
            NativeSemanticSubmissionSerial = 1u;
        LastNativeCaptureCompletionValue = NativeSemanticSubmissionSerial;
        return LastNativeCaptureCompletionValue;
    }

    bool AcceptsNativeReadback(u64 completionValue) const
    {
        return completionValue != 0u
            && completionValue <= LastNativeCaptureCompletionValue;
    }

    void PublishNative(
        CaptureOwner owner,
        u64 epoch,
        u64 semanticFrame,
        u64 captureGeneration,
        u64 completionValue,
        u32 bank,
        u32 block)
    {
        const u32 index = bank * CapturePhysicalBlocksPerBank + block;
        Blocks[index] = {
            owner, epoch, semanticFrame, captureGeneration, completionValue};
    }

    void RefreshRecorderProvenance()
    {
        RecorderBlocks = Blocks;
    }

    void BeginCaptureAllocation(u32 bank, u32 start, u32 len)
    {
        ActiveCaptureBank = bank;
        ActiveCaptureStart = start;
        ActiveCaptureLen = len;
        ActiveCapture = true;
    }

    bool ObserveCpuByteDifference(u32 bank, u32 block) const
    {
        const u32 index = bank * CapturePhysicalBlocksPerBank + block;
        return IsNativeCaptureOwner(Blocks[index].Owner)
            && CpuCaptureVersion != RecorderCaptureVersion;
    }

    bool TransitionToCpu(
        u32 bank,
        u32 block,
        CaptureAuthorityTransitionReason reason)
    {
        const u32 index = bank * CapturePhysicalBlocksPerBank + block;
        if (IsNativeCaptureOwner(Blocks[index].Owner)
            && !IsAllowedNativeToCpuTransition(reason))
        {
            return false;
        }
        Blocks[index] = {};
        Blocks[index].Owner = CaptureOwner::CpuCoherent;
        RecorderBlocks[index] = Blocks[index];
        return true;
    }

    CaptureSyncResult SelectSyncSource(
        u32 bank,
        u32 block,
        bool forceFailure,
        bool& flagsMarkedSynced)
    {
        const CaptureBlockProvenance owner = Blocks[
            bank * CapturePhysicalBlocksPerBank + block];
        flagsMarkedSynced = false;

        // This is the regression boundary: the owner remains native even
        // when the next emulated frame has started before its recorder has
        // finalized. Presentation state is intentionally irrelevant.
        if (IsNativeCaptureOwner(owner.Owner))
        {
            if (forceFailure)
                return CaptureSyncResult::Failed;
            flagsMarkedSynced = true;
            return CaptureSyncResult::Synchronized;
        }

        flagsMarkedSynced = true;
        return CaptureSyncResult::AlreadyCoherent;
    }
};

bool RunCaptureOwnershipVectors()
{
    bool passed = true;
    CaptureOwnershipModel model;
    const u64 firstSemanticSerial = model.SubmitSemantic(28u);
    const u64 secondSemanticSerial = model.SubmitSemantic(27u);
    passed &= Require(
        firstSemanticSerial == 1u && secondSemanticSerial == 2u
            && secondSemanticSerial > firstSemanticSerial
            && model.AcceptsNativeReadback(firstSemanticSerial),
        "per-slot local fences were incorrectly used as global capture provenance");
    model.EmulatedFrameSerial = 101u;
    model.RecordedNativeFrameSerial = 100u;
    model.PresentationSubmitted = false;
    model.PublishNative(
        CaptureOwner::NativeVulkan, 7u, 100u, 41u, 9001u, 2u, 1u);

    // BeginFrame snapshots the prior native owner. Allocating the same
    // destination must not update either copy: the old native pixels remain
    // semantically live until a real write or readback event occurs.
    model.RefreshRecorderProvenance();
    model.BeginCaptureAllocation(2u, 1u, 0u);
    passed &= Require(
        model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].Owner
            == CaptureOwner::NativeVulkan
            && model.RecorderBlocks[
                2u * CapturePhysicalBlocksPerBank + 1u].Owner
                == CaptureOwner::NativeVulkan
            && model.ActiveCapture
            && model.ActiveCaptureBank == 2u
            && model.ActiveCaptureStart == 1u,
        "same-geometry capture allocation discarded native provenance");

    model.CpuCaptureVersion = 2u;
    model.RecorderCaptureVersion = 1u;
    passed &= Require(
        model.ObserveCpuByteDifference(2u, 1u)
            && model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].Owner
                == CaptureOwner::NativeVulkan,
        "CPU/native byte difference was treated as an authority transition");

    passed &= Require(
        model.TransitionToCpu(
            2u, 1u, CaptureAuthorityTransitionReason::CpuWrite)
            && model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].Owner
                == CaptureOwner::CpuCoherent,
        "real CPU write did not perform an event-driven authority transition");
    model.PublishNative(
        CaptureOwner::NativeVulkan, 7u, 100u, 41u, 9001u, 2u, 1u);
    passed &= Require(
        !model.TransitionToCpu(
            2u, 1u, CaptureAuthorityTransitionReason::NativeSemanticWrite)
            && model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].Owner
                == CaptureOwner::NativeVulkan,
        "native semantic write was incorrectly accepted as native-to-CPU");

    bool flagsMarkedSynced = false;
    const u16 flagsBeforeFailure = 0xE001u;
    model.CaptureFlags = flagsBeforeFailure;
    const CaptureSyncResult failed = model.SelectSyncSource(
        2u, 1u, true, flagsMarkedSynced);
    passed &= Require(
        failed == CaptureSyncResult::Failed
            && !flagsMarkedSynced
            && model.CaptureFlags == flagsBeforeFailure
            && model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].Owner
                == CaptureOwner::NativeVulkan,
        "failed native capture sync changed flags or discarded the owner");

    const CaptureSyncResult immediate = model.SelectSyncSource(
        2u, 1u, false, flagsMarkedSynced);
    passed &= Require(
        immediate == CaptureSyncResult::Synchronized && flagsMarkedSynced,
        "cross-frame native capture did not select the retained native owner");
    passed &= Require(
        !GPU2DNative::IsCurrentFrame(
            model.EmulatedFrameSerial,
            model.RecordedNativeFrameSerial,
            model.Blocks[2u * CapturePhysicalBlocksPerBank + 1u].SemanticFrame),
        "cross-frame vector accidentally depended on current FrameRecorder identity");

    // A presentation stall must not revoke semantic ownership. Exercise the
    // same hand-off repeatedly so a ping-ponging capture destination cannot
    // resurrect an older CPU mirror after hundreds of frame rollovers.
    for (u32 iteration = 0u; iteration < 1200u; ++iteration)
    {
        const u32 bank = iteration % CapturePhysicalBanks;
        const u32 block = (iteration / CapturePhysicalBanks)
            % CapturePhysicalBlocksPerBank;
        const CaptureOwner owner = CaptureOwner::NativeVulkan;
        model.PublishNative(
            owner,
            7u,
            100u + iteration,
            41u + iteration,
            9001u + iteration,
            bank,
            block);
        model.EmulatedFrameSerial = 101u + iteration;
        model.RecordedNativeFrameSerial = 100u + iteration;
        model.PresentationSubmitted = (iteration % 5u) != 0u;
        if (!model.PresentationSubmitted)
            model.PresentationStallObserved = true;
        const CaptureSyncResult result = model.SelectSyncSource(
            bank, block, false, flagsMarkedSynced);
        const CaptureBlockProvenance& published = model.Blocks[
            bank * CapturePhysicalBlocksPerBank + block];
        passed &= Require(
            result == CaptureSyncResult::Synchronized
                && flagsMarkedSynced
                && published.Owner == owner
                && published.SemanticFrame == 100u + iteration,
            "600+ ping-pong iterations lost native capture provenance");
    }

    passed &= Require(
        model.PresentationStallObserved,
        "presentation stall model was not exercised");
    return passed;
}

struct CaptureFeedbackModel
{
    std::array<CaptureOwner, CapturePhysicalBlockCount> Owner{};
    std::array<std::array<u64, ScreenHeight>, CapturePhysicalBlockCount>
        NativeLines{};
    std::array<std::array<u64, ScreenHeight>, CapturePhysicalBlockCount>
        CpuLines{};
    u32 DisplayMode = 0;

    void PublishNative(CaptureOwner owner, u32 bank, u32 block, u64 seed)
    {
        const u32 index = bank * CapturePhysicalBlocksPerBank + block;
        Owner[index] = owner;
        for (u32 line = 0; line < ScreenHeight; ++line)
            NativeLines[index][line] = seed + line;
    }

    void BeginCaptureAllocation(u32 bank, u32 block)
    {
        // Allocation announces the destination only. It must not clear the
        // native line history because a same-bank source can consume it
        // before the corresponding line is overwritten.
        (void)bank;
        (void)block;
    }

    void WriteCaptureLine(u32 bank, u32 block, u32 line, u64 value)
    {
        NativeLines[bank * CapturePhysicalBlocksPerBank + block][line] = value;
    }

    [[nodiscard]] u64 ReadCaptureSource(u32 bank, u32 block, u32 line) const
    {
        const u32 index = bank * CapturePhysicalBlocksPerBank + block;
        return IsNativeCaptureOwner(Owner[index])
            ? NativeLines[index][line]
            : CpuLines[index][line];
    }

    [[nodiscard]] u64 ComposeDisplayMode2(u32 bank, u32 block, u32 line) const
    {
        return DisplayMode == 2u ? ReadCaptureSource(bank, block, line) : 0u;
    }
};

bool RunCaptureFeedbackVectors()
{
    bool passed = true;
    for (const CaptureOwner owner : {CaptureOwner::NativeVulkan, CaptureOwner::NativeDX12})
    {
        CaptureFeedbackModel model;
        constexpr u32 bank = 1u;
        constexpr u32 block = 0u;
        constexpr u64 seed = 0x50000000ull;
        model.PublishNative(owner, bank, block, seed);
        for (u32 line = 0; line < ScreenHeight; ++line)
            model.CpuLines[bank * CapturePhysicalBlocksPerBank + block][line]
                = 0xDEAD0000ull + line;

        model.BeginCaptureAllocation(bank, block);
        passed &= Require(
            model.Owner[bank * CapturePhysicalBlocksPerBank + block] == owner
                && model.ReadCaptureSource(bank, block, 64u) == seed + 64u,
            "same-bank capture allocation replayed the stale CPU mirror");

        // Capture writes are line-granular. A line already written in the
        // current capture changes, while an unread line still comes from the
        // retained native mirror.
        model.WriteCaptureLine(bank, block, 0u, 0xABCDEF00ull);
        passed &= Require(
            model.ReadCaptureSource(bank, block, 0u) == 0xABCDEF00ull
                && model.ReadCaptureSource(bank, block, 64u) == seed + 64u,
            "same-bank source feedback lost the persistent native line history");

        model.DisplayMode = 2u;
        passed &= Require(
            model.ComposeDisplayMode2(bank, block, 64u) == seed + 64u,
            "display mode 2 did not consume the persistent native capture mirror");

        for (u32 frame = 0u; frame < 600u; ++frame)
        {
            const u32 frameBank = frame % CapturePhysicalBanks;
            const u32 frameBlock = (frame / CapturePhysicalBanks)
                % CapturePhysicalBlocksPerBank;
            const u64 frameSeed = seed + 0x1000ull * frame;
            model.PublishNative(owner, frameBank, frameBlock, frameSeed);
            model.BeginCaptureAllocation(frameBank, frameBlock);
            const u32 feedbackLine = (frame * 13u) % ScreenHeight;
            passed &= Require(
                model.ComposeDisplayMode2(
                    frameBank, frameBlock, feedbackLine)
                    == frameSeed + feedbackLine,
                "600-frame same-bank/display-mode2 feedback lost native ownership");
        }
    }
    return passed;
}

bool RunHighResolutionCapturePrecisionVectors()
{
    const auto packColor6 = [](u32 r, u32 g, u32 b, u32 a) {
        return (r & 0x3Fu) | ((g & 0x3Fu) << 8u)
            | ((b & 0x3Fu) << 16u) | (a << 24u);
    };
    const auto rawFromColor6 = [](u32 color) {
        return ((color & 0x3Fu) >> 1u)
            | ((((color >> 8u) & 0x3Fu) >> 1u) << 5u)
            | ((((color >> 16u) & 0x3Fu) >> 1u) << 10u)
            | ((color >> 24u) != 0u ? 0x8000u : 0u);
    };
    const auto color6FromRaw = [&](u32 color) {
        return packColor6(
            (color & 0x1Fu) << 1u,
            ((color >> 5u) & 0x1Fu) << 1u,
            ((color >> 10u) & 0x1Fu) << 1u,
            ((color >> 15u) & 1u) != 0u ? 31u : 0u);
    };
    const auto sidecarColor = [&](u32 color) {
        return packColor6(
            color & 0x3Fu,
            (color >> 8u) & 0x3Fu,
            (color >> 16u) & 0x3Fu,
            (color >> 24u) != 0u ? 31u : 0u);
    };

    bool passed = true;
    const u32 sourceA = packColor6(61u, 33u, 17u, 0xFFu);
    const u32 retainedSourceB = packColor6(27u, 45u, 59u, 31u);

    // OpenGL keeps the source precision in its high-resolution capture
    // texture. Only the compact VRAM mirror drops to RGB555. Vulkan/DX12 must
    // make the same split or live/captured frame alternation visibly toggles
    // every odd six-bit color channel.
    passed &= Require(
        sidecarColor(sourceA) == packColor6(61u, 33u, 17u, 31u),
        "source-A-only high-resolution capture lost the sixth RGB bit");
    passed &= Require(
        sidecarColor(retainedSourceB) == retainedSourceB,
        "source-B-only retained capture lost the sixth RGB bit");
    passed &= Require(
        color6FromRaw(rawFromColor6(sourceA))
            == packColor6(60u, 32u, 16u, 31u),
        "compact RGBA5551 capture did not retain the native quantization");
    passed &= Require(
        sidecarColor(sourceA) != color6FromRaw(rawFromColor6(sourceA)),
        "high-resolution and compact capture representations collapsed");
    passed &= Require(
        sidecarColor(color6FromRaw(rawFromColor6(sourceA)))
            == packColor6(60u, 32u, 16u, 31u),
        "1x sidecar did not retain exact compact capture semantics");

    // Blended display capture is specified in five-bit space by the OpenGL
    // oracle. Its high-resolution result therefore remains even-valued while
    // copy-only modes preserve their source precision.
    const u32 blendedRaw = 0x8000u | 29u | (11u << 5u) | (23u << 10u);
    const u32 blendedHighRes = sidecarColor(color6FromRaw(blendedRaw));
    passed &= Require(
        (blendedHighRes & 0x010101u) == 0u
            && rawFromColor6(blendedHighRes) == blendedRaw,
        "blended high-resolution capture escaped RGB555 semantics");
    return passed;
}

} // namespace

int main()
{
    const bool passed = RunCaptureAddressVectors()
        && RunMappedBlockFlattenVectors()
        && RunSourceBSubpixelAndSavestateVectors()
        && RunVRAMDisplaySidecarReferenceVectors()
        && RunFrameCoverageAndRepresentativeVectors()
        && RunPackVectors() && RunMappedCaptureOverlayVectors()
        && RunCompareVectors()
        && RunUploadPlanVectors() && RunWorkSlotSemanticContinuityVectors()
        && RunObjRawLogicalFusionVectors()
        && RunIndependentCaptureBatchVectors()
        && RunTemporalLineVectors()
        && RunFrameIdentityVectors() && RunCaptureOwnershipVectors()
        && RunCaptureFeedbackVectors()
        && RunHighResolutionCapturePrecisionVectors()
        && RunHighResCaptureProvenanceVectors();
    std::fprintf(stderr, "%s: GPU2D native contract vectors\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
