/*
    Copyright 2016-2026 melonDS team
*/

#include "GPU2DNative.h"

#include <algorithm>
#include <cstring>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
#include "Platform.h"
#endif

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
    addOverlap(
        PackedTimelineBase * sizeof(u32),
        PackedNativeCaptureBGMappingBase * sizeof(u32),
        plan.TimelineBytes);
    addOverlap(
        PackedNativeCaptureBGMappingBase * sizeof(u32),
        PackedFrameWords * sizeof(u32), plan.MappedCaptureBytes);
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
    plan.MappedCaptureBytes = 0;
    for (u32 i = 0; i < plan.Count; ++i)
        AddClassifiedBytes(plan, plan.Ranges[i].Offset, plan.Ranges[i].Size);
}

void CopySegmentForRanges(
    u8* destination,
    u32 destinationOffset,
    const void* source,
    std::size_t sourceBytes,
    const UploadPlan& plan) noexcept
{
    if (!destination || !source || sourceBytes == 0u)
        return;
    const u8* sourceBytesPtr = static_cast<const u8*>(source);
    const u64 destinationEnd = static_cast<u64>(destinationOffset) + sourceBytes;
    for (u32 i = 0; i < plan.Count; ++i)
    {
        const DirtyRange& range = plan.Ranges[i];
        const u64 rangeBegin = range.Offset;
        const u64 rangeEnd = rangeBegin + range.Size;
        const u64 begin = std::max<u64>(rangeBegin, destinationOffset);
        const u64 end = std::min<u64>(rangeEnd, destinationEnd);
        if (end <= begin)
            continue;
        const std::size_t sourceOffset = static_cast<std::size_t>(begin - destinationOffset);
        std::memcpy(
            destination + begin,
            sourceBytesPtr + sourceOffset,
            static_cast<std::size_t>(end - begin));
    }
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
    if (uploadedGeneration.NativeCaptureMappingGeneration
        != input.Generation.NativeCaptureMappingGeneration)
    {
        AddRange(plan, {
            PackedNativeCaptureBGMappingBase * sizeof(u32),
            (PackedFrameWords - PackedNativeCaptureBGMappingBase) * sizeof(u32)});
    }
    // Provenance changes on an actual capture write even when none of the
    // emulated-state generations changed. Keep the fixed segment table in
    // every partial upload so a reused GPU slot cannot retain a prior epoch's
    // pending/valid bits.
    AddRange(plan, {
        PackedHighResCaptureProvenanceBase * sizeof(u32),
        HighResCaptureProvenanceWords * sizeof(u32)});
    ClassifyRanges(plan);
    return plan;
}

HighResCaptureSegmentMask ComputeCaptureWriteSegmentMask(
    const FrameInput& input) noexcept
{
    HighResCaptureSegmentMask result{};
    for (u32 line = 0u; line < ScreenHeight; ++line)
    {
        const LineState& state = input.Lines[line];
        if (state.CaptureEnable == 0u)
            continue;

        const u32 captureCnt = state.CaptureCnt;
        const u32 destinationBank = (captureCnt >> 16u) & 3u;
        if ((state.LCDVRAMMap & (1u << destinationBank)) == 0u)
            continue;

        const u32 sizeCode = (captureCnt >> 20u) & 3u;
        const u32 width = CaptureWidthForSize(sizeCode);
        const u32 height = CaptureHeightForSize(sizeCode);
        if (line >= height)
            continue;

        const u32 first = WrapLCDCHalfword(
            CaptureOffsetHalfwords((captureCnt >> 18u) & 3u)
            + line * width);
        for (u32 x = 0u; x < width; x += HighResCaptureSegmentHalfwords)
        {
            const u32 address = WrapLCDCHalfword(first + x);
            const u32 segment = address / HighResCaptureSegmentHalfwords;
            result[destinationBank * HighResCaptureSegmentsPerBank + segment] = 1u;
        }
    }
    return result;
}

bool PackHighResCaptureProvenance(
    u32* destination,
    std::size_t wordCount,
    const HighResCaptureProvenanceTable& table,
    const FrameInput& input,
    u64 pendingCompletionValue) noexcept
{
    if (!destination || wordCount < PackedFrameWords)
        return false;

    for (u32 index = 0u; index < HighResCaptureSegmentCount; ++index)
    {
        const u32 base = PackedHighResCaptureProvenanceBase
            + index * HighResCaptureProvenanceWordsPerSegment;
        const HighResCaptureProvenanceState& state = table[index];
        const u32 bank = index / HighResCaptureSegmentsPerBank;
        const u32 segment = index % HighResCaptureSegmentsPerBank;
        const u32 byteAddress = segment * HighResCaptureSegmentHalfwords
            * sizeof(u16);
        const u32 physicalBlock = byteAddress / CapturePhysicalBlockBytes;
        const CaptureBlockProvenance& compact = input.LCDVRAMProvenance[
            bank * CapturePhysicalBlocksPerBank + physicalBlock];
        destination[base + 0u] = state.ValidAndVersion;
        destination[base + 1u] = static_cast<u32>(
            state.CommittedIdentity.CompletionValue);
        destination[base + 2u] = static_cast<u32>(
            state.CommittedIdentity.CompletionValue >> 32u);
        destination[base + 3u] = static_cast<u32>(
            state.PendingIdentity.CompletionValue);
        destination[base + 4u] = static_cast<u32>(
            state.PendingIdentity.CompletionValue >> 32u);
        destination[base + 5u] = static_cast<u32>(compact.CompletionValue);
        destination[base + 6u] = static_cast<u32>(compact.CompletionValue >> 32u);
        destination[base + 7u] = static_cast<u32>(state.LastInvalidationReason);
    }
    destination[32u] = static_cast<u32>(pendingCompletionValue);
    destination[33u] = static_cast<u32>(pendingCompletionValue >> 32u);
    return true;
}

void HighResCaptureProvenanceTracker::Invalidate(
    u64 epoch, u32 scaleFactor) noexcept
{
    Entries.fill({});
    LastSemanticFrame.fill(0u);
    Pending.fill(0u);
    Epoch = epoch;
    ScaleFactor = scaleFactor;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    DiagnosticGapFrames = 0u;
    DiagnosticLastGapFrames = 0u;
    DiagnosticPostGapFrames = 0u;
    DiagnosticSawCaptureWrite = false;
    DiagnosticFallbackCounts.fill(0u);
#endif
    for (HighResCaptureProvenanceState& state : Entries)
    {
        state.LastInvalidationReason = HighResCaptureFallbackReason::ResourceReset;
    }
}

void HighResCaptureProvenanceTracker::BeginFrame(
    const FrameInput& input,
    const NativeCaptureStateIdentity& pendingIdentity,
    u32 scaleFactor) noexcept
{
    AbortFrame();
    if (Epoch != pendingIdentity.Epoch || ScaleFactor != scaleFactor)
        Invalidate(pendingIdentity.Epoch, scaleFactor);

    // Capture OFF is the common gap path and must remain O(1): it changes no
    // compact content and therefore cannot change any sidecar entry.
    if (input.CaptureEnable == 0u)
    {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (DiagnosticSawCaptureWrite && DiagnosticGapFrames != 0xFFFFFFFFu)
            ++DiagnosticGapFrames;
#endif
        return;
    }

    const HighResCaptureSegmentMask writes = ComputeCaptureWriteSegmentMask(input);
    const bool hasCaptureWrite = std::any_of(
        writes.begin(), writes.end(), [](u8 value) { return value != 0u; });
    // A no-write frame is a strict no-op. Display Capture OFF does not alter
    // physical LCDC VRAM, so time or frame count cannot retire its sidecar.
    if (!hasCaptureWrite)
    {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (DiagnosticSawCaptureWrite && DiagnosticGapFrames != 0xFFFFFFFFu)
            ++DiagnosticGapFrames;
#endif
        return;
    }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (DiagnosticSawCaptureWrite && DiagnosticGapFrames != 0u)
    {
        DiagnosticLastGapFrames = DiagnosticGapFrames;
        DiagnosticPostGapFrames = 4u;
    }
    DiagnosticSawCaptureWrite = true;
    DiagnosticGapFrames = 0u;
#endif
    for (u32 index = 0u; index < HighResCaptureSegmentCount; ++index)
    {
        if (writes[index] == 0u)
            continue;

        HighResCaptureProvenanceState& state = Entries[index];
        state.ValidAndVersion |= HighResCapturePendingWriteBit;
        state.PendingIdentity = pendingIdentity;
        Pending[index] = 1u;
        LastSemanticFrame[index] = input.Generation.Frame;
    }
}

void HighResCaptureProvenanceTracker::CommitFrame(
    const NativeCaptureStateIdentity& committedIdentity) noexcept
{
    for (u32 index = 0u; index < HighResCaptureSegmentCount; ++index)
    {
        if (Pending[index] == 0u)
            continue;

        HighResCaptureProvenanceState& state = Entries[index];
        if (!committedIdentity.Valid
            || state.PendingIdentity.CompletionValue
                != committedIdentity.CompletionValue
            || state.PendingIdentity.Epoch != committedIdentity.Epoch
            || state.PendingIdentity.Owner != committedIdentity.Owner)
        {
            state.ValidAndVersion = 0u;
            state.CommittedIdentity = {};
            state.PendingIdentity = {};
            state.LastInvalidationReason =
                HighResCaptureFallbackReason::IdentityMismatch;
            Pending[index] = 0u;
            continue;
        }
        const u32 nextVersion = (state.ValidAndVersion
            & HighResCaptureVersionBit) == 0u
            ? HighResCaptureVersionBit : 0u;
        state.ValidAndVersion = HighResCaptureValidBit | nextVersion;
        state.CommittedIdentity = committedIdentity;
        state.CompactIdentity = committedIdentity;
        state.PendingIdentity = {};
        state.LastInvalidationReason = HighResCaptureFallbackReason::None;
        Pending[index] = 0u;
    }
}

void HighResCaptureProvenanceTracker::AbortFrame() noexcept
{
    for (u32 index = 0u; index < HighResCaptureSegmentCount; ++index)
    {
        if (Pending[index] == 0u)
            continue;
        Entries[index].ValidAndVersion &= ~HighResCapturePendingWriteBit;
        Entries[index].PendingIdentity = {};
        Pending[index] = 0u;
    }
}

void HighResCaptureProvenanceTracker::InvalidatePhysicalRange(
    u32 bank,
    u32 firstByte,
    u32 byteCount,
    HighResCaptureFallbackReason reason) noexcept
{
    if (bank >= CapturePhysicalBanks || byteCount == 0u)
        return;

    constexpr u32 SegmentBytes = HighResCaptureSegmentHalfwords * sizeof(u16);
    const u32 begin = std::min(firstByte, LCDCBankBytes);
    const u32 end = static_cast<u32>(std::min<u64>(
        static_cast<u64>(firstByte) + byteCount, LCDCBankBytes));
    if (end <= begin)
        return;
    const u32 firstSegment = begin / SegmentBytes;
    const u32 lastSegment = (end - 1u) / SegmentBytes;
    for (u32 segment = firstSegment; segment <= lastSegment; ++segment)
    {
        const u32 index = bank * HighResCaptureSegmentsPerBank + segment;
        Entries[index] = {};
        Entries[index].LastInvalidationReason = reason;
        LastSemanticFrame[index] = 0u;
        Pending[index] = 0u;
    }
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
void HighResCaptureProvenanceTracker::LogPostGapTrace(
    const char* backend,
    const FrameInput& input) noexcept
{
    if (DiagnosticPostGapFrames == 0u)
        return;
    --DiagnosticPostGapFrames;

    const HighResCaptureSegmentMask writes = ComputeCaptureWriteSegmentMask(input);
    for (u32 index = 0u; index < HighResCaptureSegmentCount; ++index)
    {
        if (writes[index] == 0u)
            continue;
        const u32 bank = index / HighResCaptureSegmentsPerBank;
        if (bank < 2u)
            continue;

        const HighResCaptureProvenanceState& state = Entries[index];
        const u32 segment = index % HighResCaptureSegmentsPerBank;
        const u32 physicalBlock = (segment * HighResCaptureSegmentHalfwords
            * sizeof(u16)) / CapturePhysicalBlockBytes;
        const CaptureBlockProvenance& compact = input.LCDVRAMProvenance[
            bank * CapturePhysicalBlocksPerBank + physicalBlock];
        HighResCaptureFallbackReason fallback = HighResCaptureFallbackReason::None;
        if ((state.ValidAndVersion & HighResCaptureValidBit) == 0u)
            fallback = state.LastInvalidationReason;
        else if (state.CommittedIdentity.CompletionValue == 0u
            || state.CommittedIdentity.CompletionValue
                != compact.CompletionValue)
            fallback = HighResCaptureFallbackReason::IdentityMismatch;
        if (fallback != HighResCaptureFallbackReason::None)
            ++DiagnosticFallbackCounts[static_cast<u32>(fallback)];

        Platform::Log(
            Platform::LogLevel::Info,
            "[GPU2DHighResCapture] backend=%s event=post_gap frame=%llu "
            "gapFrames=%u captureEnable=%u captureCnt=0x%08x bank=%u segment=%u "
            "compactOwner=%s compactEpoch=%llu compactCaptureGeneration=%llu "
            "compactCompletion=%llu sidecarValid=%u sidecarVersion=%u "
            "sidecarIdentity=%llu pendingCompletion=%llu selectedReference=%u "
            "pendingWritable=%u "
            "fallbackReason=%u fallbackCount=%llu traceRemaining=%u\n",
            backend ? backend : "Unknown",
            static_cast<unsigned long long>(input.Generation.Frame),
            DiagnosticLastGapFrames,
            input.CaptureEnable,
            input.CaptureCnt,
            bank,
            index % HighResCaptureSegmentsPerBank,
            CaptureOwnerName(compact.Owner),
            static_cast<unsigned long long>(compact.Epoch),
            static_cast<unsigned long long>(compact.CaptureGeneration),
            static_cast<unsigned long long>(
                compact.CompletionValue),
            (state.ValidAndVersion & HighResCaptureValidBit) != 0u ? 1u : 0u,
            (state.ValidAndVersion & HighResCaptureVersionBit) != 0u ? 1u : 0u,
            static_cast<unsigned long long>(
                state.CommittedIdentity.CompletionValue),
            static_cast<unsigned long long>(
                state.PendingIdentity.CompletionValue),
            fallback == HighResCaptureFallbackReason::None ? 1u : 0u,
            (state.ValidAndVersion & HighResCapturePendingWriteBit) != 0u
                ? 1u : 0u,
            static_cast<u32>(fallback),
            static_cast<unsigned long long>(
                DiagnosticFallbackCounts[static_cast<u32>(fallback)]),
            DiagnosticPostGapFrames);
        return;
    }
}
#endif

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
    UploadPlan fullPlan{};
    fullPlan.Count = 1u;
    fullPlan.Ranges[0] = {0u, static_cast<u32>(PackedFrameBytes())};
    return PackFrameRanges(input, destination, wordCount, fullPlan);
}

bool PackFrameRanges(
    const FrameInput& input,
    u32* destination,
    std::size_t wordCount,
    const UploadPlan& plan) noexcept
{
    if (!destination || wordCount < PackedFrameWords)
        return false;

    std::array<u32, PackedHeaderWords> header{};
    header[0] = 0x32445047u; // "GPU2"
    header[1] = PackedFrameAbiVersion;
    const auto storeU64 = [&](u32 word, u64 value) {
        header[word] = static_cast<u32>(value);
        header[word + 1u] = static_cast<u32>(value >> 32u);
    };
    storeU64(2u, input.Generation.Frame);
    storeU64(4u, input.Generation.ContentGeneration);
    storeU64(6u, input.Generation.VRAMGeneration);
    storeU64(8u, input.Generation.CaptureGeneration);
    header[10] = input.CaptureCnt;
    header[11] = input.CaptureEnable;
    header[12] = input.ScreenSwap;
    header[13] = input.ScreensEnabled;
    header[14] = input.LCDVRAMMap;
    header[15] = input.NativeCaptureOverlayAny;
    header[16] = input.Engine[0].BGSize;
    header[17] = input.Engine[0].OBJSize;
    header[18] = input.Engine[0].BGExtendedPaletteSize;
    header[19] = input.Engine[0].OBJExtendedPaletteSize;
    header[20] = input.Engine[1].BGSize;
    header[21] = input.Engine[1].OBJSize;
    header[22] = input.Engine[1].BGExtendedPaletteSize;
    header[23] = input.Engine[1].OBJExtendedPaletteSize;
    header[24] = PackedLineWords;
    header[25] = PackedEngineWords;
    header[26] = TimelineBlockCount;
    header[27] = input.TimelineDeltaCount;
    header[28] = input.TimelineOverflow;
    header[29] = input.TimelineRowCount;
    header[30] = input.SpriteTimelineRowCount;
    header[31] = PackedTimelinePayloadWords;
    // Words 32..33 are filled by PackHighResCaptureProvenance immediately
    // before submission. PackFrame alone deliberately leaves them zero.

    u8* destinationBytes = reinterpret_cast<u8*>(destination);
    CopySegmentForRanges(
        destinationBytes, 0u, header.data(), sizeof(header), plan);
    CopySegmentForRanges(
        destinationBytes, PackedHeaderWords * sizeof(u32), input.Lines.data(),
        static_cast<std::size_t>(PackedLinesWords) * sizeof(u32), plan);

    for (u32 engine = 0; engine < 2u; ++engine)
    {
        const MemorySnapshot& source = input.Engine[engine];
        const u32 engineBase = (PackedEngineBase + engine * PackedEngineWords) * sizeof(u32);
        CopySegmentForRanges(destinationBytes, engineBase,
            source.BGVRAM.data(), source.BGVRAM.size(), plan);
        CopySegmentForRanges(destinationBytes, engineBase + PackedBGWords * sizeof(u32),
            source.OBJVRAM.data(), source.OBJVRAM.size(), plan);
        CopySegmentForRanges(destinationBytes,
            engineBase + (PackedBGWords + PackedOBJWords) * sizeof(u32),
            source.BGExtendedPalette.data(), source.BGExtendedPalette.size(), plan);
        CopySegmentForRanges(destinationBytes,
            engineBase + (PackedBGWords + PackedOBJWords + PackedBGExtendedPaletteWords)
                * sizeof(u32),
            source.OBJExtendedPalette.data(), source.OBJExtendedPalette.size(), plan);
    }

    CopySegmentForRanges(destinationBytes, PackedPaletteBase * sizeof(u32),
        input.Palette.data(), input.Palette.size(), plan);
    CopySegmentForRanges(destinationBytes, PackedOAMBase * sizeof(u32),
        input.OAM.data(), input.OAM.size(), plan);
    CopySegmentForRanges(destinationBytes, PackedFIFOBase * sizeof(u32),
        input.DisplayFIFO.data(), input.DisplayFIFO.size() * sizeof(u16), plan);
    CopySegmentForRanges(destinationBytes, PackedLCDVRAMBase * sizeof(u32),
        input.LCDVRAM.data(), input.LCDVRAM.size(), plan);
    std::array<u32, PackedRouteWords> routeWords{};
    for (u32 i = 0; i < PackedRouteWords; ++i)
        routeWords[i] = input.ScreenSource[i];
    CopySegmentForRanges(destinationBytes, PackedRouteBase * sizeof(u32),
        routeWords.data(), routeWords.size() * sizeof(u32), plan);
    CopySegmentForRanges(destinationBytes, PackedTimelineBase * sizeof(u32),
        input.TimelineRowIds.data(),
        static_cast<std::size_t>(PackedTimelineRowIdWords) * sizeof(u32), plan);
    CopySegmentForRanges(destinationBytes, PackedTimelineRowsBase * sizeof(u32),
        input.TimelineRows.data(),
        static_cast<std::size_t>(input.TimelineRowCount) * TimelineBlockCount
            * sizeof(u32), plan);
    CopySegmentForRanges(destinationBytes, PackedTimelinePayloadBase * sizeof(u32),
        input.TimelinePayload.data(), input.TimelinePayload.size(), plan);
    CopySegmentForRanges(destinationBytes, PackedSpriteTimelineBase * sizeof(u32),
        input.SpriteTimelineRowIds.data(),
        static_cast<std::size_t>(PackedSpriteTimelineRowIdWords) * sizeof(u32), plan);
    CopySegmentForRanges(destinationBytes, PackedSpriteTimelineRowsBase * sizeof(u32),
        input.SpriteTimelineRows.data(),
        static_cast<std::size_t>(input.SpriteTimelineRowCount) * SpriteTimelineBlockCount
            * sizeof(u32), plan);
    CopySegmentForRanges(
        destinationBytes, PackedNativeCaptureBGMappingBase * sizeof(u32),
        input.NativeCaptureBGMapping.data(),
        input.NativeCaptureBGMapping.size() * sizeof(u32), plan);
    CopySegmentForRanges(
        destinationBytes, PackedNativeCaptureOBJMappingBase * sizeof(u32),
        input.NativeCaptureOBJMapping.data(),
        input.NativeCaptureOBJMapping.size() * sizeof(u32), plan);
    CopySegmentForRanges(
        destinationBytes, PackedNativeCaptureSpriteOBJMappingBase * sizeof(u32),
        input.NativeCaptureSpriteOBJMapping.data(),
        input.NativeCaptureSpriteOBJMapping.size() * sizeof(u32), plan);
    return true;
}

} // namespace melonDS::GPU2DNative
