/*
    Copyright 2016-2026 melonDS team
*/

#include "GPU2DNative.h"

#include <algorithm>
#include <cstring>

namespace melonDS::GPU2DNative
{

namespace
{
void AddClassifiedBytes(UploadPlan& plan, u32 offset, u32 size) noexcept
{
    if (size == 0u)
        return;

    plan.TotalBytes += size;
    const u32 end = offset + size;
    const auto addOverlap = [&](u32 begin, u32 finish, u64& target) {
        const u32 overlapBegin = std::max(offset, begin);
        const u32 overlapEnd = std::min(end, finish);
        if (overlapEnd > overlapBegin)
            target += overlapEnd - overlapBegin;
    };
    addOverlap(PackedEngineBase * sizeof(u32), PackedPaletteBase * sizeof(u32),
        plan.EngineMemoryBytes);
    addOverlap(PackedPaletteBase * sizeof(u32), PackedOAMBase * sizeof(u32),
        plan.PaletteBytes);
    addOverlap(PackedOAMBase * sizeof(u32), PackedFIFOBase * sizeof(u32),
        plan.OAMBytes);
    addOverlap(PackedFIFOBase * sizeof(u32), PackedLCDVRAMBase * sizeof(u32),
        plan.FIFOBytes);
    addOverlap(PackedLCDVRAMBase * sizeof(u32), PackedRouteBase * sizeof(u32),
        plan.LCDVRAMBytes);
    addOverlap(PackedTimelineBase * sizeof(u32), PackedFrameWords * sizeof(u32),
        plan.TimelineBytes);
}

void AddRange(UploadPlan& plan, DirtyRange range) noexcept
{
    if (range.Size == 0u)
        return;

    u64 begin = range.Offset;
    u64 end = begin + range.Size;
    for (u32 i = 0; i < plan.Count;)
    {
        const u64 existingBegin = plan.Ranges[i].Offset;
        const u64 existingEnd = existingBegin + plan.Ranges[i].Size;
        if (end < existingBegin || begin > existingEnd)
        {
            ++i;
            continue;
        }

        begin = std::min(begin, existingBegin);
        end = std::max(end, existingEnd);
        for (u32 j = i + 1u; j < plan.Count; ++j)
            plan.Ranges[j - 1u] = plan.Ranges[j];
        --plan.Count;
    }

    if (plan.Count >= MaxDirtyRanges)
        return;

    u32 insertion = plan.Count;
    while (insertion != 0u
        && plan.Ranges[insertion - 1u].Offset > static_cast<u32>(begin))
    {
        plan.Ranges[insertion] = plan.Ranges[insertion - 1u];
        --insertion;
    }
    plan.Ranges[insertion] = {
        static_cast<u32>(begin), static_cast<u32>(end - begin)};
    ++plan.Count;
}

void ClassifyRanges(UploadPlan& plan) noexcept
{
    plan.TotalBytes = 0;
    plan.EngineMemoryBytes = 0;
    plan.PaletteBytes = 0;
    plan.OAMBytes = 0;
    plan.FIFOBytes = 0;
    plan.LCDVRAMBytes = 0;
    for (u32 i = 0; i < plan.Count; ++i)
        AddClassifiedBytes(plan, plan.Ranges[i].Offset, plan.Ranges[i].Size);
}
}

UploadPlan BuildUploadPlan(const FrameInput& input, bool fullUpload) noexcept
{
    return BuildUploadPlan(input, input.Generation, fullUpload);
}

UploadPlan BuildUploadPlan(
    const FrameInput& input,
    const FrameGeneration& uploadedGeneration,
    bool fullUpload) noexcept
{
    UploadPlan plan{};
    if (fullUpload)
    {
        AddRange(plan, {0u, static_cast<u32>(PackedFrameBytes())});
        ClassifyRanges(plan);
        return plan;
    }

    const u32 count = std::min(input.DirtyRangeCount, MaxDirtyRanges);
    for (u32 i = 0; i < count; ++i)
        AddRange(plan, input.DirtyRanges[i]);

    if (uploadedGeneration.VRAMGeneration != input.Generation.VRAMGeneration)
    {
        AddRange(plan, {
            PackedEngineBase * sizeof(u32),
            (PackedPaletteBase - PackedEngineBase) * sizeof(u32)});
    }
    if (uploadedGeneration.ContentGeneration != input.Generation.ContentGeneration)
    {
        AddRange(plan, {
            PackedPaletteBase * sizeof(u32),
            (PackedLCDVRAMBase - PackedPaletteBase) * sizeof(u32)});
    }
    if (uploadedGeneration.CaptureGeneration != input.Generation.CaptureGeneration)
    {
        AddRange(plan, {
            PackedLCDVRAMBase * sizeof(u32),
            (PackedRouteBase - PackedLCDVRAMBase) * sizeof(u32)});
    }
    ClassifyRanges(plan);
    return plan;
}

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
    destination[26] = TimelineBlockCount;
    destination[27] = input.TimelineDeltaCount;
    destination[28] = input.TimelineOverflow;
    destination[29] = PackedTimelineIndexWords;
    destination[30] = PackedTimelinePayloadWords;

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
    std::memcpy(
        destination + PackedTimelineBase,
        input.TimelineIndex.data(),
        static_cast<std::size_t>(PackedTimelineIndexWords) * sizeof(u32));
    std::memcpy(
        destination + PackedTimelinePayloadBase,
        input.TimelinePayload.data(),
        input.TimelinePayload.size());
    std::memcpy(
        destination + PackedSpriteTimelineBase,
        input.SpriteTimelineIndex.data(),
        static_cast<std::size_t>(PackedSpriteTimelineIndexWords) * sizeof(u32));
    return true;
}

} // namespace melonDS::GPU2DNative
