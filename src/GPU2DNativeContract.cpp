/*
    Copyright 2016-2026 melonDS team
*/

#include "GPU2DNative.h"

#include <algorithm>
#include <cstring>

namespace melonDS::GPU2DNative
{

CompareResult CompareExact(
    const u32* expectedTop,
    const u32* expectedBottom,
    const u32* actualTop,
    const u32* actualBottom) noexcept
{
    CompareResult result{};
    if (!expectedTop || !expectedBottom || !actualTop || !actualBottom)
    {
        // A missing logical frame is a failed comparison, but there is no
        // coordinate to report. Keep the result machine-readable and make
        // the failure impossible to confuse with an exact zero-pixel frame.
        result.TotalMismatchCount = 1;
        result.TopMismatchCount = 1;
        return result;
    }

    for (u32 screen = 0; screen < 2; ++screen)
    {
        const u32* expected = screen == 0 ? expectedTop : expectedBottom;
        const u32* actual = screen == 0 ? actualTop : actualBottom;
        for (u32 y = 0; y < ScreenHeight; ++y)
        {
            for (u32 x = 0; x < ScreenWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * ScreenWidth + x;
                if (expected[index] == actual[index])
                    continue;

                ++result.TotalMismatchCount;
                if (screen == 0)
                    ++result.TopMismatchCount;
                else
                    ++result.BottomMismatchCount;
                ++result.MismatchPerLine[screen * ScreenHeight + y];

                if (result.FirstMismatchLine == ScreenHeight)
                {
                    result.FirstMismatchLine = y;
                    result.FirstMismatchX = x;
                }
                if (result.SampleCount < result.Samples.size())
                {
                    result.Samples[result.SampleCount++] = {
                        screen, x, y, expected[index], actual[index]};
                }
            }
        }
    }
    return result;
}

bool PackFrame(const FrameInput& input, u32* destination, std::size_t wordCount) noexcept
{
    if (!destination || wordCount < PackedFrameWords)
        return false;

    std::fill(destination, destination + PackedFrameWords, 0u);
    destination[0] = 0x32445047u; // "GPU2"
    destination[1] = 1u;
    const auto storeU64 = [&](u32 word, u64 value) {
        destination[word] = static_cast<u32>(value);
        destination[word + 1u] = static_cast<u32>(value >> 32u);
    };
    storeU64(2u, input.Generation.Frame);
    storeU64(4u, input.Generation.ContentGeneration);
    storeU64(6u, input.Generation.VRAMGeneration);
    storeU64(8u, input.Generation.CaptureGeneration);
    destination[10] = input.CaptureCnt;
    destination[11] = input.CaptureEnable;
    destination[12] = input.ScreenSwap;
    destination[13] = input.ScreensEnabled;
    destination[14] = input.LCDVRAMMap;
    destination[16] = input.Engine[0].BGSize;
    destination[17] = input.Engine[0].OBJSize;
    destination[18] = input.Engine[0].BGExtendedPaletteSize;
    destination[19] = input.Engine[0].OBJExtendedPaletteSize;
    destination[20] = input.Engine[1].BGSize;
    destination[21] = input.Engine[1].OBJSize;
    destination[22] = input.Engine[1].BGExtendedPaletteSize;
    destination[23] = input.Engine[1].OBJExtendedPaletteSize;
    destination[24] = PackedLineWords;
    destination[25] = PackedEngineWords;

    std::memcpy(
        destination + PackedHeaderWords,
        input.Lines.data(),
        static_cast<std::size_t>(PackedLinesWords) * sizeof(u32));

    for (u32 engine = 0; engine < 2u; ++engine)
    {
        const MemorySnapshot& source = input.Engine[engine];
        u32* destinationEngine = destination
            + PackedEngineBase + engine * PackedEngineWords;
        std::memcpy(destinationEngine, source.BGVRAM.data(), source.BGVRAM.size());
        destinationEngine += PackedBGWords;
        std::memcpy(destinationEngine, source.OBJVRAM.data(), source.OBJVRAM.size());
        destinationEngine += PackedOBJWords;
        std::memcpy(
            destinationEngine,
            source.BGExtendedPalette.data(),
            source.BGExtendedPalette.size());
        destinationEngine += PackedBGExtendedPaletteWords;
        std::memcpy(
            destinationEngine,
            source.OBJExtendedPalette.data(),
            source.OBJExtendedPalette.size());
    }

    std::memcpy(
        destination + PackedPaletteBase,
        input.Palette.data(),
        input.Palette.size());
    std::memcpy(
        destination + PackedOAMBase,
        input.OAM.data(),
        input.OAM.size());
    std::memcpy(
        destination + PackedFIFOBase,
        input.DisplayFIFO.data(),
        input.DisplayFIFO.size() * sizeof(u16));
    std::memcpy(
        destination + PackedLCDVRAMBase,
        input.LCDVRAM.data(),
        input.LCDVRAM.size());
    for (u32 i = 0; i < PackedRouteWords; ++i)
        destination[PackedRouteBase + i] = input.ScreenSource[i];
    return true;
}

} // namespace melonDS::GPU2DNative
