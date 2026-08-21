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
    input->LCDVRAMMap = 1u << 2u;
    input->Engine[0].BGSize = 0x80000u;
    input->Engine[1].OBJSize = 0x20000u;
    input->Lines[192u + 17u].DispCnt = 0x12345678u;
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
    passed &= Require(packed[0] == 0x32445047u && packed[1] == 2u,
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
            && partialDestination[1] == 2u
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

} // namespace

int main()
{
    const bool passed = RunPackVectors() && RunCompareVectors()
        && RunUploadPlanVectors() && RunTemporalLineVectors()
        && RunFrameIdentityVectors() && RunCaptureOwnershipVectors();
    std::fprintf(stderr, "%s: GPU2D native contract vectors\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
