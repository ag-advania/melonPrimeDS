/*
    Copyright 2016-2026 melonDS team

    Backend-neutral input and differential contract for native GPU 2D.

    This file deliberately contains no Vulkan, DirectX, OpenGL, or frontend
    types.  The two native backends consume the same line-state and memory
    snapshot so a shader change cannot silently change the Nintendo DS 2D
    semantics on only one API.
*/

#ifndef GPU2D_NATIVE_H
#define GPU2D_NATIVE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "GPU.h"
#include "types.h"

namespace melonDS
{

class GPU;
class GPU2D;

namespace GPU2DNative
{

inline constexpr u32 ScreenWidth = 256;
inline constexpr u32 ScreenHeight = 192;
inline constexpr u32 ScreenPixelCount = ScreenWidth * ScreenHeight;

// SpriteLatchValid is the last word in LineState and is consumed by the
// native shader's OBJ timeline selector.  Its low bit remains the original
// latch flag; the high byte carries the start line of the current display
// capture command so Stage A can publish a precise CaptureReference without
// walking earlier lines in every logical pixel invocation.  0xFF means that
// no capture command has started in this frame.
inline constexpr u32 SpriteLatchValidMask = 0x1u;
inline constexpr u32 CaptureStartLineShift = 8u;
inline constexpr u32 CaptureStartLineMask = 0xFFu;
inline constexpr u32 CaptureStartLineNone = 0xFFu;

// Developer-only Gate B switch. The normal renderer never waits for a
// compositor readback; setting MELONPRIME_GPU2D_EXACT_VALIDATE=1 (or the
// shorter MELONPRIME_GPU2D_EXACT=1) enables exact native-output validation.
[[nodiscard]] bool ExactValidationEnabled() noexcept;

// Developer-only three-stage evidence.  This is intentionally a separate
// switch from the shipping renderer path: it enables readback of Stage A's
// structured planes and Stage B's resolved pixels so a blank LCD can be
// attributed to the producer, compositor, or presenter without changing
// normal frame ownership.
[[nodiscard]] bool StageDiagnosticsEnabled() noexcept;
// Developer-only A/B switch.  When enabled together with StageDiagnostics,
// Stage B keeps the production direct-image path and reads those images back
// instead of silently replacing it with the composed-buffer path.
[[nodiscard]] bool DirectOutputDiagnosticsEnabled() noexcept;

// Developer-only presentation backpressure injection. This consumes one
// available presentation publication slot without delaying semantic GPU2D
// execution, allowing the persistent LCDC mirror to be validated while the
// visible frame is intentionally retained. Shipping builds always return
// false; this is never a frame limiter or a sleep-based timing mechanism.
[[nodiscard]] bool ConsumeForcedPresentationStallFrame() noexcept;

// Renderer instances use a process-wide epoch allocator so a renderer/backend
// transition cannot accidentally reuse an older presentation identity.
[[nodiscard]] u64 AllocateRendererEpoch() noexcept;

enum class BlankClass : u8
{
    NonBlank = 0,
    AllBlack,
    AllWhite,
};

[[nodiscard]] u64 HashWords(
    const u32* words,
    std::size_t count,
    u64 seed = 1469598103934665603ull) noexcept;
[[nodiscard]] BlankClass ClassifyNativePixels(
    const u32* pixels,
    std::size_t count) noexcept;
[[nodiscard]] const char* BlankClassName(BlankClass value) noexcept;

// Physical A/B state-load runs deliberately begin with ordinary ROM startup
// frames. Do not compare those transition frames against a later savestate;
// arm exact validation at the same lifecycle boundary as the UI state load.
void NotifySavestateLoaded() noexcept;

[[nodiscard]] constexpr bool IsCurrentFrame(
    u64 emulatedFrame,
    u64 recordedFrame,
    u64 inputFrame) noexcept
{
    return emulatedFrame != 0u
        && recordedFrame == emulatedFrame
        && inputFrame == emulatedFrame;
}

// All members are 32-bit slots on purpose.  This is the canonical layout for
// both std430 (Vulkan) and StructuredBuffer (DX12); it also keeps packing rules
// out of the backend implementations.
struct LineState
{
    u32 DispCnt = 0;
    u32 LayerEnable = 0;
    u32 OBJEnable = 0;
    u32 ForcedBlank = 0;

    std::array<u32, 4> BGCnt{};
    std::array<u32, 4> BGXPos{};
    std::array<u32, 4> BGYPos{};

    std::array<s32, 2> BGXRefInternal{};
    std::array<s32, 2> BGYRefInternal{};
    std::array<s32, 2> BGRotA{};
    std::array<s32, 2> BGRotB{};
    std::array<s32, 2> BGRotC{};
    std::array<s32, 2> BGRotD{};

    std::array<u32, 4> Win0Coords{};
    std::array<u32, 4> Win1Coords{};
    std::array<u32, 4> WinCnt{};
    u32 Win0Active = 0;
    u32 Win1Active = 0;

    std::array<u32, 2> BGMosaicSize{};
    std::array<u32, 2> OBJMosaicSize{};
    u32 BGMosaicLine = 0;
    u32 OBJMosaicLine = 0;

    u32 BlendCnt = 0;
    u32 BlendAlpha = 0;
    u32 EVA = 0;
    u32 EVB = 0;
    u32 EVY = 0;

    u32 MasterBrightness = 0;
    u32 RenderXPos = 0;
    u32 CaptureCnt = 0;
    u32 CaptureEnable = 0;
    u32 ScreensEnabled = 0;
    u32 ScreenSwap = 0;
    u32 WinRegs = 0;
    u32 WinMask = 0;
    std::array<u32, 4> WinPos{};
    // POWCNT1's engine-enable latch is distinct from LayerEnable. The
    // software renderer uses it before BG/OBJ evaluation, while display modes
    // 2/3 are still sourced by the outer display circuit.
    u32 UnitEnabled = 0;
    // VRAMCNT/LCDC routing is latched per visible line.  It must not be read
    // from the frame header when Display Mode 2 or capture crosses a
    // mid-frame VRAM remap.
    u32 LCDVRAMMap = 0;
    // DrawSprites(line) prepares the OBJ line consumed by the next
    // DrawScanline.  This bit selects the private OBJ/OAM latch timeline for
    // the line; palette and OBJ extended-palette reads remain on the normal
    // current-line timeline, matching InterleaveSprites().
    u32 SpriteLatchValid = 0;
};

static_assert(sizeof(LineState) == 68u * sizeof(u32),
    "GPU2D native line state must stay a 68-word block");
static_assert(offsetof(LineState, WinRegs) == 59u * sizeof(u32),
    "GPU2D native derived window state offset");

struct MemorySnapshot
{
    // The flattened mirrors are the same address spaces consumed by
    // SoftRenderer2D::GetBGVRAM/GetOBJVRAM.  The size fields are explicit so
    // shaders never infer an engine's address mask from a backend constant.
    std::array<u8, 512 * 1024> BGVRAM{};
    std::array<u8, 256 * 1024> OBJVRAM{};
    std::array<u8, 32 * 1024> BGExtendedPalette{};
    std::array<u8, 8 * 1024> OBJExtendedPalette{};
    u32 BGSize = 0;
    u32 OBJSize = 0;
    u32 BGExtendedPaletteSize = 0;
    u32 OBJExtendedPaletteSize = 0;
};

struct FrameGeneration
{
    u64 Frame = 0;
    // These generations describe persistent device mirrors, not the logical
    // frame number.  A compositor ring slot can miss the one frame in which a
    // dirty range was observed, so the backend uses these values to request a
    // category refresh when that slot is reused later.
    u64 ContentGeneration = 0; // palette, OAM, and display FIFO
    u64 VRAMGeneration = 0;    // BG/OBJ VRAM and extended palettes
    u64 CaptureGeneration = 0; // LCDC VRAM mirror
};

inline constexpr u32 DirtyBlockBytes = 512u;
inline constexpr u32 MaxDirtyRanges = 8192u;
// TimelinePayload stores unique 512-byte contents, not one copy per write
// event.  The open-addressing table is deliberately larger than the payload
// so a full valid payload still has an empty insertion slot.
inline constexpr u32 MaxMemoryDeltas = 8192u;
inline constexpr u32 TimelineHashTableSize = MaxMemoryDeltas * 2u;
static_assert((TimelineHashTableSize & (TimelineHashTableSize - 1u)) == 0u,
    "timeline hash table must be a power of two");

// Memory is resolved at the beginning of each visible line. A row ID points at
// an immutable row in the per-frame table; version zero means the frame-start
// snapshot and non-zero versions index TimelinePayload. Consecutive lines that
// see no memory mutation reuse the same row ID, so a steady frame no longer
// materializes a 192 x 4265 CPU matrix.
inline constexpr u32 TimelineEngineBGBlocks = (512u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineEngineOBJBlocks = (256u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineEngineBGExtBlocks = (32u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineEngineOBJExtBlocks = (8u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineEngineBlocks = TimelineEngineBGBlocks
    + TimelineEngineOBJBlocks + TimelineEngineBGExtBlocks + TimelineEngineOBJExtBlocks;
inline constexpr u32 TimelineEngineAllBlocks = 2u * TimelineEngineBlocks;
inline constexpr u32 TimelinePaletteBlocks = (2u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineOAMBlocks = (2u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineFIFOBlocks = (256u * sizeof(u16)) / DirtyBlockBytes;
inline constexpr u32 TimelineLCDVRAMBlocks = (4u * 128u * 1024u) / DirtyBlockBytes;
inline constexpr u32 TimelineEngineBaseBlock = 0u;
inline constexpr u32 TimelinePaletteBaseBlock = TimelineEngineAllBlocks;
inline constexpr u32 TimelineOAMBaseBlock = TimelinePaletteBaseBlock + TimelinePaletteBlocks;
inline constexpr u32 TimelineFIFOBaseBlock = TimelineOAMBaseBlock + TimelineOAMBlocks;
inline constexpr u32 TimelineLCDVRAMBaseBlock = TimelineFIFOBaseBlock + TimelineFIFOBlocks;
inline constexpr u32 TimelineBlockCount = TimelineLCDVRAMBaseBlock + TimelineLCDVRAMBlocks;
inline constexpr u32 PackedTimelineRowIdWords = ScreenHeight;
inline constexpr u32 PackedTimelineRowsWords = TimelineBlockCount * ScreenHeight;
inline constexpr u32 PackedTimelinePayloadWords =
    MaxMemoryDeltas * (DirtyBlockBytes / sizeof(u32));
// OBJ/OAM are prepared one HBlank before the scanline that consumes them.
// Keep a compact second timeline for just those memory classes instead of
// shifting palette visibility, which the software renderer resolves on the
// current line in InterleaveSprites().
inline constexpr u32 SpriteTimelineOAMBlocks = TimelineOAMBlocks;
inline constexpr u32 SpriteTimelineEngineOBJBlocks = TimelineEngineOBJBlocks;
inline constexpr u32 SpriteTimelineBlockCount = SpriteTimelineOAMBlocks
    + 2u * SpriteTimelineEngineOBJBlocks;
inline constexpr u32 PackedSpriteTimelineRowIdWords = ScreenHeight;
inline constexpr u32 PackedSpriteTimelineRowsWords =
    SpriteTimelineBlockCount * ScreenHeight;

struct DirtyRange
{
    u32 Offset = 0;
    u32 Size = 0;
};

// Host-only recorder accounting.  This is intentionally outside the packed
// frame layout: it describes how the private observer was built, not GPU2D
// state consumed by either shader backend.
struct RecorderMetrics
{
    u64 BlocksScanned = 0;
    u64 BytesScanned = 0;
    u64 BlocksCopied = 0;
    u64 BytesCopied = 0;
    u64 CaptureCPU2DLines = 0;
    u64 CaptureCPU2DNs = 0;
    u64 GPU2DRecorderNs = 0;
    u64 TimelineRowDedupNs = 0;
    u64 SpriteTimelineRowDedupNs = 0;
    u64 NativeGPU2DPackNs = 0;
};

struct UploadPlan
{
    std::array<DirtyRange, MaxDirtyRanges> Ranges{};
    u32 Count = 0;
    u64 TotalBytes = 0;
    u64 EngineMemoryBytes = 0;
    u64 PaletteBytes = 0;
    u64 OAMBytes = 0;
    u64 FIFOBytes = 0;
    u64 LCDVRAMBytes = 0;
    u64 TimelineBytes = 0;
};

struct FrameInput
{
    std::array<LineState, 2 * ScreenHeight> Lines{};
    MemorySnapshot Engine[2]{};
    std::array<u8, 2 * ScreenHeight> ScreenSource{};

    // Palette and OAM are shared by the engines in the emulated GPU address
    // space.  Keeping the original byte representation avoids an accidental
    // BGR555/RGB555 conversion in the upload path.
    std::array<u8, 2 * 1024> Palette{};
    std::array<u8, 2 * 1024> OAM{};
    std::array<u16, 256> DisplayFIFO{};
    u32 CaptureCnt = 0;
    u32 CaptureEnable = 0;
    u32 ScreenSwap = 0;
    u32 ScreensEnabled = 0;
    u32 LCDVRAMMap = 0;
    std::array<u8, 4 * 128 * 1024> LCDVRAM{};
    // Host-only authority metadata for the persistent LCDC mirror. This is
    // deliberately outside PackedFrame: native Vulkan/DX12 capture writes
    // remain GPU-resident, and the composer must not replay a stale CPU
    // snapshot over a block whose owner outlived the FrameRecorder.
    std::array<CaptureBlockProvenance, CapturePhysicalBlockCount>
        LCDVRAMProvenance{};
    FrameGeneration Generation{};
    std::array<u32, PackedTimelineRowIdWords> TimelineRowIds{};
    std::array<u32, PackedTimelineRowsWords> TimelineRows{};
    std::array<u8, MaxMemoryDeltas * DirtyBlockBytes> TimelinePayload{};
    std::array<u32, PackedSpriteTimelineRowIdWords> SpriteTimelineRowIds{};
    std::array<u32, PackedSpriteTimelineRowsWords> SpriteTimelineRows{};
    u32 TimelineDeltaCount = 0;
    u32 TimelineOverflow = 0;
    u32 TimelineRowCount = 0;
    u32 SpriteTimelineRowCount = 0;
    // Host-only mutation sequence. It lets the recorder reuse the previous
    // row without comparing 4265 versions for every unchanged line.
    u32 TimelineMutationSerial = 0;
    // Host-only hash-consing metadata. It is intentionally excluded from the
    // packed ABI; shader-visible indices still point at ordinary full blocks.
    std::array<u64, TimelineHashTableSize> TimelineHashKeys{};
    std::array<u32, TimelineHashTableSize> TimelineHashVersions{};
    // Row hashes use a separate namespace from payload-block hashes. A row
    // hit is always verified with a full memcmp, so collisions cannot alter
    // the shader-visible timeline.
    std::array<u64, TimelineHashTableSize> TimelineRowHashKeys{};
    std::array<u32, TimelineHashTableSize> TimelineRowHashRows{};
    std::array<u64, TimelineHashTableSize> SpriteTimelineRowHashKeys{};
    std::array<u32, TimelineHashTableSize> SpriteTimelineRowHashRows{};
    // Byte ranges in the serialized frame that changed since the previous
    // frame. They are metadata only and are not part of PackedFrameWords.
    std::array<DirtyRange, MaxDirtyRanges> DirtyRanges{};
    u32 DirtyRangeCount = 0;
    RecorderMetrics Recorder{};
};

// Structured Stage A layout shared by the Vulkan and DX12 diagnostic
// readbacks.  The hash covers the four per-screen planes and that screen's
// line metadata; it does not include capture-only source planes.
inline constexpr u32 StructuredPlaneCount = 14u;
inline constexpr u32 StructuredLineMetaBase =
    StructuredPlaneCount * ScreenPixelCount;

[[nodiscard]] u64 HashStructuredScreen(
    const u32* structured,
    u32 screen) noexcept;
[[nodiscard]] BlankClass ClassifyStructuredScreen(
    const u32* structured,
    u32 screen) noexcept;

// Emit one developer-only record for Stage A (logical structured planes) and
// Stage B (resolved native pixels), including state dumps for blank physical
// screens.  `expectedTop`/`expectedBottom` are the software oracle when exact
// validation is active and may be null for a pure stage diagnostic run.
void LogStageSnapshot(
    const char* backend,
    u64 emulatedFrame,
    u64 recordedFrame,
    u64 rendererSerial,
    u64 generation,
    u32 slot,
    const FrameInput& input,
    const u32* structured,
    const u32* actualTop,
    const u32* actualBottom,
    const char* resolvedSource,
    const u32* expectedTop,
    const u32* expectedBottom) noexcept;

void LogPresentedIdentity(
    const char* backend,
    u64 emulatedFrame,
    u64 rendererSerial,
    u64 generation,
    u64 epoch,
    u32 slot) noexcept;

void LogSemanticIdentity(
    const char* backend,
    u64 emulatedFrame,
    u64 captureGeneration,
    u64 epoch,
    bool published,
    bool forcedPresentationStall,
    bool mirrorFullResync,
    u32 publishedSlot) noexcept;

static_assert(std::is_trivially_copyable_v<FrameInput>,
    "FrameInput must remain memset-resettable without a stack-sized temporary");

// Fixed serialization layout consumed by both native shader backends.  The
// byte arrays are copied verbatim into u32 words; shaders perform the byte
// and BGR555 reads, so no host-side pixel generation is hidden in the packer.
inline constexpr u32 PackedHeaderWords = 32;
inline constexpr u32 PackedLineWords = sizeof(LineState) / sizeof(u32);
inline constexpr u32 PackedLineCount = 2 * ScreenHeight;
inline constexpr u32 PackedLinesWords = PackedLineWords * PackedLineCount;
inline constexpr u32 PackedBGWords = (512 * 1024) / sizeof(u32);
inline constexpr u32 PackedOBJWords = (256 * 1024) / sizeof(u32);
inline constexpr u32 PackedBGExtendedPaletteWords = (32 * 1024) / sizeof(u32);
inline constexpr u32 PackedOBJExtendedPaletteWords = (8 * 1024) / sizeof(u32);
inline constexpr u32 PackedEngineWords =
    PackedBGWords + PackedOBJWords
    + PackedBGExtendedPaletteWords + PackedOBJExtendedPaletteWords;
inline constexpr u32 PackedPaletteWords = (2 * 1024) / sizeof(u32);
inline constexpr u32 PackedOAMWords = (2 * 1024) / sizeof(u32);
inline constexpr u32 PackedFIFOWords = 256;
inline constexpr u32 PackedLCDVRAMWords = (4 * 128 * 1024) / sizeof(u32);
inline constexpr u32 PackedRouteWords = 2 * ScreenHeight;
inline constexpr u32 PackedEngineBase = PackedHeaderWords + PackedLinesWords;
inline constexpr u32 PackedPaletteBase = PackedEngineBase + 2 * PackedEngineWords;
inline constexpr u32 PackedOAMBase = PackedPaletteBase + PackedPaletteWords;
inline constexpr u32 PackedFIFOBase = PackedOAMBase + PackedOAMWords;
inline constexpr u32 PackedLCDVRAMBase = PackedFIFOBase + PackedFIFOWords;
inline constexpr u32 PackedRouteBase = PackedLCDVRAMBase + PackedLCDVRAMWords;
inline constexpr u32 PackedTimelineBase = PackedRouteBase + PackedRouteWords;
inline constexpr u32 PackedTimelineRowsBase = PackedTimelineBase + PackedTimelineRowIdWords;
inline constexpr u32 PackedTimelinePayloadBase = PackedTimelineRowsBase
    + PackedTimelineRowsWords;
inline constexpr u32 PackedSpriteTimelineBase = PackedTimelinePayloadBase
    + PackedTimelinePayloadWords;
inline constexpr u32 PackedSpriteTimelineRowsBase = PackedSpriteTimelineBase
    + PackedSpriteTimelineRowIdWords;
inline constexpr u32 PackedFrameWords = PackedSpriteTimelineRowsBase
    + PackedSpriteTimelineRowsWords;

static_assert(PackedLineWords == 68u, "native line serialization drift");

[[nodiscard]] constexpr std::size_t PackedFrameBytes() noexcept
{
    return static_cast<std::size_t>(PackedFrameWords) * sizeof(u32);
}

// Returns false only when the destination is too small or null.  This is a
// state/memory pack operation; it never converts pixels or runs the software
// renderer.
bool PackFrame(const FrameInput& input, u32* destination, std::size_t wordCount) noexcept;

// Writes only the serialized byte ranges selected by BuildUploadPlan. The
// destination is an already resident slot; unchanged bytes are intentionally
// left untouched so a normal frame does not repack or upload the full ABI.
bool PackFrameRanges(
    const FrameInput& input,
    u32* destination,
    std::size_t wordCount,
    const UploadPlan& plan) noexcept;

// Builds non-overlapping serialized upload ranges. The first use of a device
// slot must pass fullUpload=true; subsequent uses can copy only the ranges
// recorded by FrameRecorder, leaving unchanged GPU-resident mirrors intact.
UploadPlan BuildUploadPlan(const FrameInput& input, bool fullUpload) noexcept;

// Builds a plan against one compositor slot's last uploaded generations.  The
// bool-only overload above remains the isolated-contract form: it consumes the
// current frame's dirty ranges without a per-slot history.
UploadPlan BuildUploadPlan(
    const FrameInput& input,
    const FrameGeneration& uploadedGeneration,
    bool fullUpload) noexcept;

struct Mismatch
{
    u32 Screen = 0;
    u32 X = 0;
    u32 Y = 0;
    u32 Expected = 0;
    u32 Actual = 0;
};

struct CompareResult
{
    u32 TopMismatchCount = 0;
    u32 BottomMismatchCount = 0;
    u32 TotalMismatchCount = 0;
    u32 FirstMismatchLine = ScreenHeight;
    u32 FirstMismatchX = ScreenWidth;
    std::array<u32, 2 * ScreenHeight> MismatchPerLine{};
    std::array<Mismatch, 64> Samples{};
    u32 SampleCount = 0;

    [[nodiscard]] bool Exact() const noexcept
    {
        return TotalMismatchCount == 0;
    }
};

// Exact logical-pixel comparator.  Inputs are native 6-bit renderer words,
// not Qt images, scaled screenshots, or sRGB samples.  The comparator is
// intentionally tiny and deterministic so it can be shared by isolated Gate
// A tests and real renderer Gate B diagnostics.
CompareResult CompareExact(
    const u32* expectedTop,
    const u32* expectedBottom,
    const u32* actualTop,
    const u32* actualBottom) noexcept;

// Collects emulation-time line state and the coherent VRAM/PAL/OAM mirrors
// that native Vulkan/DX12 GPU2D consumes.  Both backends receive this exact
// contract; they must not invent separate register semantics.
class FrameRecorder
{
public:
    explicit FrameRecorder(const melonDS::GPU& gpu) noexcept;

    void Reset() noexcept;
    void BeginFrame(u64 frame) noexcept;
    void CaptureLine(
        u32 engine,
        const melonDS::GPU2D& gpu2D,
        u32 line,
        bool screenSwap) noexcept;
    void CaptureMemoryForLine(u32 line) noexcept;
    void RecordSoftwareCaptureLine(u64 nanoseconds) noexcept;
    // Called from the renderer's DrawSprites hook at the hardware latch
    // point, after DrawScanline(line) and before DrawScanline(line+1).
    void CaptureSpriteLatchForLine(u32 line) noexcept;
    void FinalizeMemory() noexcept;
    // GPU::CheckCaptureStart can allocate a destination after BeginFrame has
    // copied the renderer's physical-block provenance. Refresh that snapshot
    // at the ownership hand-off so a newly allocated CPU-coherent destination
    // is uploaded instead of being filtered as the prior native capture.
    void MarkCaptureAllocationCpuCoherent(
        u32 bank, u32 start, u32 len) noexcept;

    [[nodiscard]] const FrameInput& GetFrame() const noexcept { return Input; }
    [[nodiscard]] bool IsValid() const noexcept { return Valid; }

private:
    const melonDS::GPU& GPU;
    FrameInput Input{};
    bool Valid = false;
    std::array<bool, 2 * ScreenHeight> LineSeen{};
    u32 EngineLineCount[2] = {0, 0};
    MemorySnapshot CurrentEngine[2]{};
    std::array<u8, 2 * 1024> CurrentPalette{};
    std::array<u8, 2 * 1024> CurrentOAM{};
    std::array<u16, 256> CurrentDisplayFIFO{};
    std::array<u8, 4 * 128 * 1024> CurrentLCDVRAM{};
    std::array<u32, TimelineBlockCount> CurrentTimelineVersion{};
    u32 LastTimelineMutationSerial = 0;
    u32 LastSpriteTimelineMutationSerial = 0;
    bool MemoryBaselineReady = false;
    std::array<bool, ScreenHeight> SpriteLatchSeen{};
    std::array<u8, 256u * 1024u> PendingEngineAOBJ{};
    std::array<u8, 128u * 1024u> PendingEngineBOBJ{};
    std::array<u8, 2u * 1024u> PendingOAM{};
    bool PendingSpriteLatchReady = false;
    u32 LastJournalSequence = 0u;
    std::array<GPU2DWriteJournalEntry, GPU2DWriteJournalCapacity> JournalScratch{};
    u64 RecorderStartNs = 0u;
    u32 CaptureStartLine = CaptureStartLineNone;
    u32 CaptureStateCnt = 0u;
    bool CaptureStateEnabled = false;

    void SnapshotEngine(u32 engine) noexcept;
    void CaptureAllMappedMemoryForLine() noexcept;
    void CaptureCoherentLCDVRAMForLine() noexcept;
    void CaptureJournalWritesForLine() noexcept;
    void RefreshCaptureProvenance() noexcept;
    void MarkInputCaptureBlockCpuCoherent(u32 bank, u32 physicalBlock) noexcept;
    void CaptureMappedPhysicalBlock(
        u32 engine,
        u32 section,
        u8* current,
        u32 size,
        const u32* mappings,
        u32 mappingCount,
        u32 mappingBytes,
        u32 blockBase,
        u32 bank,
        u32 physicalBlock) noexcept;
    void CaptureDirectMemoryBlock(
        const u8* source,
        u8* current,
        u32 size,
        u32 blockBase,
        u32 block) noexcept;
    void CaptureMappedMemoryForLine(
        u32 engine,
        u32 section,
        u8* current,
        u32 size,
        const u32* mappings,
        u32 mappingCount,
        u32 mappingBytes,
        u32 blockBase) noexcept;
    void CaptureDirectMemoryForLine(
        const u8* source,
        u8* current,
        u32 size,
        u32 blockBase) noexcept;
    void FillTimelineLine(u32 line) noexcept;
    void FillSpriteTimelineLine(u32 line) noexcept;
    void ApplyPendingSpriteLatch() noexcept;
};

} // namespace GPU2DNative
} // namespace melonDS

#endif // GPU2D_NATIVE_H
