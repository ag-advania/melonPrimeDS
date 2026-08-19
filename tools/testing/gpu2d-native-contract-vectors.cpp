/*
    GPU-independent contract vectors for the native Vulkan/DX12 GPU2D input
    ABI and exact logical-pixel comparator.
*/

#include <array>
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

bool RunPackVectors()
{
    auto input = std::make_unique<FrameInput>();
    input->Generation.Frame = 0x1122334455667788ull;
    input->Generation.ContentGeneration = 0x8877665544332211ull;
    input->CaptureCnt = 0xA5A5A5A5u;
    input->Engine[0].BGSize = 0x80000u;
    input->Engine[1].OBJSize = 0x20000u;
    input->Lines[192u + 17u].DispCnt = 0x12345678u;
    input->Lines[192u + 17u].UnitEnabled = 1u;
    input->ScreenSource[191u] = 1u;
    input->Palette[37u] = 0x5Au;
    input->OAM[513u] = 0xC3u;
    input->LCDVRAM[0x1234u] = 0xE7u;

    std::vector<u32> packed(PackedFrameWords, 0u);
    bool passed = true;
    passed &= Require(PackFrame(*input, packed.data(), packed.size()),
        "PackFrame rejected a correctly sized destination");
    passed &= Require(packed[0] == 0x32445047u && packed[1] == 1u,
        "native frame header magic/version drifted");
    passed &= Require(packed[2] == 0x55667788u && packed[3] == 0x11223344u,
        "frame generation is not serialized little-endian");
    passed &= Require(packed[10] == input->CaptureCnt,
        "global capture state is not serialized");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords] == 0x12345678u,
        "engine-B line state offset drifted");
    passed &= Require(
        packed[PackedHeaderWords + (192u + 17u) * PackedLineWords + 65u] == 1u,
        "engine-enable state is not serialized");
    passed &= Require(
        ((packed[PackedPaletteBase + 37u / 4u] >> ((37u & 3u) * 8u)) & 0xFFu)
            == 0x5Au,
        "palette byte mirror is not packed verbatim");
    passed &= Require(
        ((packed[PackedLCDVRAMBase + 0x1234u / 4u]
            >> ((0x1234u & 3u) * 8u)) & 0xFFu) == 0xE7u,
        "LCD VRAM byte mirror is not packed verbatim");
    passed &= Require(!PackFrame(*input, packed.data(), PackedFrameWords - 1u),
        "PackFrame accepted a short destination");
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
    bool passed = Require(partial.Count == 3u && partial.TotalBytes == 592u,
        "partial upload plan did not preserve dirty ranges");
    passed &= Require(partial.EngineMemoryBytes == 512u
            && partial.PaletteBytes == 64u,
        "partial upload plan category accounting drifted");

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
    return passed;
}

} // namespace

int main()
{
    const bool passed = RunPackVectors() && RunCompareVectors()
        && RunUploadPlanVectors();
    std::fprintf(stderr, "%s: GPU2D native contract vectors\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
