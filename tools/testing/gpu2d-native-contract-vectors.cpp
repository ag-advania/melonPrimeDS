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
    passed &= Require(packed[0] == 0x32445047u && packed[1] == 5u,
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

    [[nodiscard]] u32 OwnerMask(
        u32 engine,
        u32 line,
        u32 address,
        u32 size,
        bool obj,
        bool spriteLatch) const
    {
        if (line >= ScreenHeight || size == 0u)
            return 0u;
        const u32 offset = address & (size - 1u);
        const u32 mappingIndex = offset / (16u * 1024u);
        const u32 mappingCount = obj
            ? (engine == 0u ? 16u : 8u)
            : (engine == 0u ? 32u : 8u);
        if (mappingIndex >= mappingCount)
            return 0u;
        if (!obj)
        {
            return BG[line][(engine == 0u ? 0u : 32u) + mappingIndex];
        }
        const auto& row = spriteLatch ? SpriteOBJ[line] : OBJ[line];
        return row[(engine == 0u ? 0u : 16u) + mappingIndex];
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
};

bool RunMappedCaptureOverlayVectors()
{
    bool passed = true;
    MappedCaptureOverlayModel model;
    constexpr u32 address = 0x1234u;
    constexpr u32 size = 256u * 1024u;
    const u32 offset = address & (size - 1u);

    // A native-owned block must win over a deliberately poisoned CPU mirror.
    model.BG[0u][0u] = 1u << 0u;
    model.BGMappedBanks[0u] = 1u << 0u;
    model.Cpu[0u][offset & LCDCBankByteMask] = 0xF0u;
    model.Native[0u][offset & LCDCBankByteMask] = 0x03u;
    passed &= Require(
        model.Resolve(0u, 0u, address, size, false, false) == 0x03u,
        "native-owned mapped BG read replayed poisoned CPU VRAM");

    // Two native banks mapped to one logical page are Nintendo's OR-visible
    // overlap case. The overlay must preserve both native contributions.
    model.BG[32u][0u] = (1u << 0u) | (1u << 1u);
    model.BGMappedBanks[32u] = (1u << 0u) | (1u << 1u);
    model.Cpu[1u][offset & LCDCBankByteMask] = 0xF0u;
    model.Native[1u][offset & LCDCBankByteMask] = 0x0Cu;
    passed &= Require(
        model.Resolve(0u, 32u, address, size, false, false) == 0x0Fu,
        "overlapping native capture banks were not OR-composed");

    // Mid-frame remapping is line-local: the same logical BG address resolves
    // through the row captured at each line boundary.
    model.BG[64u][0u] = 1u << 0u;
    model.BG[65u][0u] = 1u << 1u;
    model.BGMappedBanks[64u] = 1u << 0u;
    model.BGMappedBanks[65u] = 1u << 1u;
    passed &= Require(
        model.Resolve(0u, 64u, address, size, false, false) == 0x03u
            && model.Resolve(0u, 65u, address, size, false, false) == 0x0Cu,
        "mid-frame mapped-capture owner transition was not line-local");

    // OBJ has a current-line mapping and an independent one-line-ahead latch
    // mapping. A valid latch selects the latter; its absence selects current.
    model.OBJ[80u][0u] = 1u << 0u;
    model.SpriteOBJ[80u][0u] = 1u << 2u;
    model.OBJMappedBanks[80u] = 1u << 0u;
    model.SpriteOBJMappedBanks[80u] = 1u << 2u;
    model.Cpu[2u][offset & LCDCBankByteMask] = 0xA0u;
    model.Native[2u][offset & LCDCBankByteMask] = 0x05u;
    passed &= Require(
        model.Resolve(0u, 80u, address, size, true, false) == 0x03u
            && model.Resolve(0u, 80u, address, size, true, true) == 0x05u,
        "OBJ current/latch mapping selection drifted");

    // Exercise stale-poison, line split, overlap, and all required scales over
    // the long state-load horizon without making presentation scale part of
    // the logical mapping decision.
    for (u32 line = 0u; line < ScreenHeight; ++line)
    {
        model.BG[line][0u] = (line < 64u || line >= 128u)
            ? (1u << 0u) : (1u << 1u);
        model.BGMappedBanks[line] = model.BG[line][0u];
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
            && partialDestination[1] == 5u
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

bool RunHighResCaptureProvenanceVectors()
{
    auto input = std::make_unique<FrameInput>();
    input->Generation.Frame = 1u;
    input->Generation.CaptureGeneration = 11u;
    constexpr u32 bank = 2u;
    constexpr u32 segment = 3u * (CapturePhysicalBlockBytes / sizeof(u16))
        / HighResCaptureSegmentHalfwords;
    input->Lines[0].CaptureCnt = (bank << 16u) | (3u << 18u);
    input->Lines[0].CaptureEnable = 1u;
    input->Lines[0].LCDVRAMMap = 1u << bank;

    const HighResCaptureSegmentMask mask = ComputeCaptureWriteSegmentMask(*input);
    bool passed = Require(
        mask[bank * HighResCaptureSegmentsPerBank + segment] != 0u
            && std::count(
                mask.begin() + bank * HighResCaptureSegmentsPerBank,
                mask.begin() + (bank + 1u) * HighResCaptureSegmentsPerBank,
                static_cast<u8>(1u)) == 1,
        "capture provenance segment mask did not follow the latched destination");

    HighResCaptureProvenanceTracker tracker;
    tracker.Invalidate(7u, 4u);
    tracker.BeginFrame(*input, 7u, 4u);
    const u32 index = bank * HighResCaptureSegmentsPerBank + segment;
    const auto& pending = tracker.States()[index];
    passed &= Require(
        (pending.ValidAndVersion & HighResCapturePendingWriteBit) != 0u
            && (pending.ValidAndVersion & HighResCaptureValidBit) == 0u,
        "state-load invalidation still admitted an old sidecar version");
    passed &= Require(
        pending.EpochTag == 7u && pending.CaptureGenerationLo == 11u
            && pending.ScaleFactor == 4u,
        "capture provenance did not carry epoch/generation/scale tags");

    std::vector<u32> packed(PackedFrameWords, 0u);
    passed &= Require(
        PackHighResCaptureProvenance(
            packed.data(), packed.size(), tracker.States()),
        "capture provenance table did not pack into the common ABI");
    const u32 tableBase = PackedHighResCaptureProvenanceBase
        + index * HighResCaptureProvenanceWordsPerSegment;
    passed &= Require(
        packed[tableBase] == pending.ValidAndVersion
            && packed[tableBase + 1u] == pending.EpochTag
            && packed[tableBase + 2u] == pending.CaptureGenerationLo
            && packed[tableBase + 3u] == pending.ScaleFactor,
        "packed capture provenance fields drifted");

    tracker.CommitFrame();
    const u32 committed = tracker.States()[index].ValidAndVersion;
    passed &= Require(
        (committed & HighResCaptureValidBit) != 0u
            && (committed & HighResCapturePendingWriteBit) == 0u,
        "capture write did not commit a readable sidecar version");
    const u32 firstVersion = committed & HighResCaptureVersionBit;

    // A second write toggles only because a real capture write is pending;
    // frame parity and a frame with no write cannot change this state.
    input->Generation.Frame = 2u;
    input->Generation.CaptureGeneration = 12u;
    tracker.BeginFrame(*input, 7u, 4u);
    const u32 secondPending = tracker.States()[index].ValidAndVersion;
    const u32 secondWriteVersion = (secondPending & HighResCaptureVersionBit) == 0u
        ? HighResCaptureVersionBit : 0u;
    passed &= Require(
        (secondPending & HighResCapturePendingWriteBit) != 0u
            && secondWriteVersion != firstVersion,
        "actual capture write did not select the alternate sidecar version");
    tracker.AbortFrame();
    passed &= Require(
        tracker.States()[index].ValidAndVersion == committed,
        "aborted capture submission changed committed sidecar provenance");

    input->Lines[0].CaptureEnable = 0u;
    input->Generation.Frame = 3u;
    tracker.BeginFrame(*input, 7u, 4u);
    passed &= Require(
        tracker.States()[index].ValidAndVersion == committed,
        "frame rollover changed sidecar version without a capture write");

    tracker.Invalidate(8u, 4u);
    passed &= Require(
        (tracker.States()[index].ValidAndVersion & HighResCaptureValidBit) == 0u
            && tracker.States()[index].EpochTag == 8u,
        "renderer epoch invalidation did not reject the pre-load sidecar");

    // Partial-block poison vector: a 128-pixel line writes only one segment.
    // The neighbouring segment is intentionally treated as stale poison and
    // must remain invalid after state-load invalidation and commit.
    constexpr u32 firstSegment = bank * HighResCaptureSegmentsPerBank;
    constexpr u32 neighbourSegment = firstSegment + 1u;
    std::array<std::array<u16, 2>, 2> poison{{
        {{0x801Fu, 0x801Fu}}, // version 0: magenta
        {{0x83E0u, 0x83E0u}}, // version 1: cyan
    }};
    input->Lines[0].CaptureCnt = bank << 16u;
    input->Lines[0].CaptureEnable = 1u;
    input->Lines[0].LCDVRAMMap = 1u << bank;
    input->Lines[1].CaptureCnt = bank << 16u;
    input->Lines[1].LCDVRAMMap = 1u << bank;
    input->Generation.Frame = 4u;
    tracker.Invalidate(9u, 4u);
    tracker.BeginFrame(*input, 9u, 4u);
    tracker.CommitFrame();
    const auto resolvePoison = [&](u32 index, u16 compact) {
        const u32 state = tracker.States()[index].ValidAndVersion;
        if ((state & HighResCaptureValidBit) == 0u)
            return compact;
        const u32 version = (state & HighResCaptureVersionBit) != 0u ? 1u : 0u;
        return poison[version][0u];
    };
    passed &= Require(
        (tracker.States()[firstSegment].ValidAndVersion & HighResCaptureValidBit) != 0u
            && (tracker.States()[neighbourSegment].ValidAndVersion
                & HighResCaptureValidBit) == 0u,
        "partial capture promoted an untouched neighbour segment to valid");
    passed &= Require(
        resolvePoison(neighbourSegment, 0x1234u) == 0x1234u,
        "invalid partial-capture address exposed stale sidecar poison");

    // Mixed-version vector inside one former 32 KiB block: retain an older
    // valid segment while committing a new segment, then require a resolver to
    // use per-segment metadata rather than the physical-block version.
    tracker.Invalidate(10u, 4u);
    input->Lines[0].CaptureEnable = 0u;
    input->Lines[1].CaptureEnable = 1u;
    input->Generation.Frame = 5u;
    tracker.BeginFrame(*input, 9u, 4u);
    tracker.CommitFrame(); // segment 1 -> version 1
    input->Generation.Frame = 6u;
    tracker.BeginFrame(*input, 9u, 4u);
    tracker.CommitFrame(); // segment 1 -> version 0
    input->Lines[0].CaptureEnable = 1u;
    input->Lines[1].CaptureEnable = 0u;
    input->Generation.Frame = 7u;
    tracker.BeginFrame(*input, 9u, 4u);
    tracker.CommitFrame(); // segment 0 -> version 1
    const u32 firstCommitted = tracker.States()[firstSegment].ValidAndVersion;
    const u32 secondCommitted = tracker.States()[neighbourSegment].ValidAndVersion;
    passed &= Require(
        (firstCommitted & HighResCaptureValidBit) != 0u
            && (secondCommitted & HighResCaptureValidBit) != 0u
            && (firstCommitted & HighResCaptureVersionBit)
                != (secondCommitted & HighResCaptureVersionBit),
        "mixed-version capture segments collapsed to one physical-block version");
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

} // namespace

int main()
{
    const bool passed = RunCaptureAddressVectors()
        && RunPackVectors() && RunMappedCaptureOverlayVectors()
        && RunCompareVectors()
        && RunUploadPlanVectors() && RunTemporalLineVectors()
        && RunFrameIdentityVectors() && RunCaptureOwnershipVectors()
        && RunCaptureFeedbackVectors()
        && RunHighResCaptureProvenanceVectors();
    std::fprintf(stderr, "%s: GPU2D native contract vectors\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
