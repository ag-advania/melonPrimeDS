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
inline constexpr u32 CoverageWordCount = (ScreenHeight + 63u) / 64u;

[[nodiscard]] constexpr u32 RepresentativeSubpixel(u32 scale) noexcept
{
    return scale >> 1u;
}

struct LineCoverage
{
    std::array<u64, CoverageWordCount> Words{};

    void Reset() noexcept
    {
        Words.fill(0u);
    }

    void Mark(u32 line) noexcept
    {
        if (line >= ScreenHeight)
            return;
        Words[line >> 6u] |= 1ull << (line & 63u);
    }

    [[nodiscard]] bool Complete() const noexcept
    {
        constexpr u64 finalWordMask = (ScreenHeight & 63u) == 0u
            ? ~0ull
            : ((1ull << (ScreenHeight & 63u)) - 1ull);
        return Words[0] == ~0ull
            && Words[1] == ~0ull
            && Words[2] == finalWordMask;
    }

    [[nodiscard]] u32 Count() const noexcept
    {
        u32 count = 0u;
        for (u32 line = 0u; line < ScreenHeight; ++line)
        {
            if ((Words[line >> 6u] & (1ull << (line & 63u))) != 0u)
                ++count;
        }
        return count;
    }
};

// DISPCAPCNT destination/source-B offsets are DS halfword addresses. Native
// compact LCDC mirrors and source-B fetches, however, use byte addresses.
// Keep the conversion and the two independent LCDC wrap domains named so a
// future shader change cannot silently mix the units again.
inline constexpr u32 LCDCBankBytes = 128u * 1024u;
inline constexpr u32 LCDCBankByteMask = LCDCBankBytes - 1u;
// Native display-capture writes are 128 or 256 halfwords wide and every
// DISPCAPCNT destination offset is 128-halfword aligned. A 128-halfword
// segment therefore describes the smallest complete write unit without
// promoting an untouched part of a 32 KiB physical block to a new version.
inline constexpr u32 HighResCaptureSegmentHalfwords = 128u;
inline constexpr u32 HighResCaptureSegmentsPerBank =
    (LCDCBankBytes / sizeof(u16)) / HighResCaptureSegmentHalfwords;
inline constexpr u32 HighResCaptureSegmentCount =
    CapturePhysicalBanks * HighResCaptureSegmentsPerBank;

[[nodiscard]] constexpr u32 CaptureOffsetHalfwords(u32 code) noexcept
{
    return (code & 3u) << 14u;
}

[[nodiscard]] constexpr u32 CaptureOffsetBytes(u32 code) noexcept
{
    return CaptureOffsetHalfwords(code) << 1u;
}

[[nodiscard]] constexpr u32 WrapLCDCHalfword(u32 address) noexcept
{
    return address & 0xFFFFu;
}

[[nodiscard]] constexpr u32 WrapLCDCByte(u32 address) noexcept
{
    return address & LCDCBankByteMask;
}

[[nodiscard]] constexpr u32 CaptureWidthForSize(u32 sizeCode) noexcept
{
    return (sizeCode & 3u) == 0u ? 128u : 256u;
}

[[nodiscard]] constexpr u32 CaptureHeightForSize(u32 sizeCode) noexcept
{
    const u32 normalized = sizeCode & 3u;
    return normalized == 0u ? 128u : 64u * normalized;
}

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

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
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

// Developer-only A/B switch for the savestate timeline discontinuity gate.
// The safe behavior is enabled by default; setting this to 0 deliberately
// restores the legacy partial-frame publication for comparison runs.
[[nodiscard]] bool DropDiscontinuousSavestateFrameEnabled() noexcept;

// Developer-only presentation backpressure injection. This consumes one
// available presentation publication slot without delaying semantic GPU2D
// execution, allowing the persistent LCDC mirror to be validated while the
// visible frame is intentionally retained. Shipping builds always return
// false; this is never a frame limiter or a sleep-based timing mechanism.
[[nodiscard]] bool ConsumeForcedPresentationStallFrame() noexcept;
#else
// Shipping builds contain no exact/stage/readback diagnostic switches.
[[nodiscard]] inline constexpr bool ExactValidationEnabled() noexcept
{
    return false;
}

[[nodiscard]] inline constexpr bool StageDiagnosticsEnabled() noexcept
{
    return false;
}

[[nodiscard]] inline constexpr bool DirectOutputDiagnosticsEnabled() noexcept
{
    return false;
}

[[nodiscard]] inline constexpr bool DropDiscontinuousSavestateFrameEnabled() noexcept
{
    return true;
}

[[nodiscard]] inline constexpr bool ConsumeForcedPresentationStallFrame() noexcept
{
    return false;
}
#endif

// Renderer instances use a process-wide epoch allocator so a renderer/backend
// transition cannot accidentally reuse an older presentation identity.
[[nodiscard]] u64 AllocateRendererEpoch() noexcept;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
enum class BlankClass : u8
{
    NonBlank = 0,
    AllBlack,
    AllWhite,
    // No Software oracle was supplied for this diagnostic snapshot. This is
    // distinct from NonBlank so stage-only high-resolution runs do not turn
    // an intentionally unavailable comparison into a false failure.
    Unknown,
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
#else
// State-load notification is only meaningful to the developer-only exact
// validation gate. Keep the lifecycle call site harmless in shipping.
inline void NotifySavestateLoaded() noexcept {}
#endif

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
    // Native capture mapping rows are packed in the frame input but are
    // maintained independently from the VRAM/content mirrors.  A compositor
    // ring slot must refresh them when it missed the frame in which a mapping
    // row changed, even if the current frame's row is byte-identical to the
    // previous frame.
    u64 NativeCaptureMappingGeneration = 0;
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

// A-D capture ownership is resolved at the 16 KiB logical mapping
// granularity used by VRAMCNT. The native shader uses these rows to restore
// GPU-resident capture bytes after the CPU flatten has deliberately omitted
// stale native-owned bytes. BG and OBJ keep separate current-line/latch
// timelines because DrawSprites(line) prepares the OBJ line consumed by the
// following scanline.
inline constexpr u32 NativeCaptureBGMappingStride = 32u + 8u;
inline constexpr u32 NativeCaptureOBJMappingStride = 16u + 8u;
inline constexpr u32 NativeCaptureBGMappingWords =
    ScreenHeight * NativeCaptureBGMappingStride;
inline constexpr u32 NativeCaptureOBJMappingWords =
    ScreenHeight * NativeCaptureOBJMappingStride;
inline constexpr u32 NativeCaptureSpriteOBJMappingWords =
    ScreenHeight * NativeCaptureOBJMappingStride;
// Native capture ownership uses only the low four bits of a mapping entry.
// Bit 4 is a per-line/engine summary flag so shaders can skip the mapping
// lookup and bank loop entirely when that row has no GPU-owned bytes.
inline constexpr u32 NativeCaptureBankMask = 0x0Fu;
inline constexpr u32 NativeCaptureOverlayPresent = 1u << 4u;
inline constexpr u32 NativeCaptureOverlayAnyMask = 1u;

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
    u64 MappedReadWordCalls = 0;
    u64 MappedReadFastPathCalls = 0;
    u64 MappedReadSlowPathCalls = 0;
    u64 NativeCaptureHistoryScanLines = 0;
    u64 NativeMappingBuildCalls = 0;
    u64 NativeMappingRowsUploaded = 0;
    u64 NativeMappingBytesUploaded = 0;
    // These are row-level shader routing decisions, not per-pixel samples:
    // they prove that ordinary rows take the zero-overlay branch without
    // adding an atomic counter to the shader hot path.
    u64 BGOverlayFastPath = 0;
    u64 BGOverlaySlowPath = 0;
    u64 OBJOverlayFastPath = 0;
    u64 OBJOverlaySlowPath = 0;
    // A valid shipping frame must keep this at zero: native-owned A-D
    // capture bytes are resolved in the GPU shader overlay, never by a host
    // VRAM read. Developer proof materialization intentionally increments it.
    u64 NativeOwnedMappedCpuRead = 0;
    u64 NativeOwnedMappedCpuMaterialized = 0;
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
inline constexpr u32 MaxMappedCaptureViolations = 128u;

struct MappedCaptureViolation
{
    u64 Frame = 0;
    u32 Line = CaptureStartLineNone;
    u32 Engine = 0;
    u32 Section = 0;
    u32 LogicalAddress = 0;
    u32 MappingIndex = 0;
    u32 Bank = 0;
    u32 PhysicalAddress = 0;
    u32 PhysicalBlock = 0;
    CaptureOwner Owner = CaptureOwner::None;
    u64 OwnerSemanticFrame = 0;
    u64 OwnerCaptureGeneration = 0;
    u64 CpuHash = 0;
    u64 NativeHash = 0;
    u32 Materialized = 0;
};
#endif

// Developer-only, host-side evidence for the address units used by a native
// Display Capture command. This is deliberately not part of the packed shader
// ABI; it is emitted once per observed command at frame finalization.
struct CaptureAddressDiagnostic
{
    u64 Frame = 0;
    u32 Line = CaptureStartLineNone;
    u32 CaptureCnt = 0;
    u32 Bank = 0;
    u32 SizeCode = 0;
    u32 DstOffsetCode = 0;
    u32 DstHalfwordBase = 0;
    u32 DstByteBase = 0;
    u32 SourceBOffsetCode = 0;
    u32 FirstByte = 0xFFFFFFFFu;
    u32 LastByte = 0;
    u32 WrapCount = 0;
    u32 ExpectedBlockMask = 0;
    u32 ActualBlockMask = 0;
    u32 DestinationAddressMismatch = 0;
    u32 SourceBAddressMismatch = 0;
    u32 OutsideBank = 0;
    u32 NeighborBankCorruption = 0;
    u32 ProvenanceExpectedFirstByte = 0;
    u32 ProvenanceAddressMismatch = 0;
    u32 LastTrackedLine = CaptureStartLineNone;
};

inline constexpr u32 MaxCaptureAddressDiagnostics = 16u;

// Developer-only accounting for the persistent native LCDC capture mirror.
// Native-owned blocks are expected to be skipped by the host upload planner;
// the reupload counter is a fail-closed tripwire immediately before the copy
// call, and must remain zero in a valid frame stream.
struct NativeCaptureHostCopyDiagnostics
{
    u64 NativeOwnedBlocksSkipped = 0;
    u64 NativeOwnedHostReupload = 0;
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
void RecordNativeOwnedCaptureCopySkipped() noexcept;
void RecordNativeOwnedHostReupload() noexcept;
[[nodiscard]] NativeCaptureHostCopyDiagnostics
GetNativeCaptureHostCopyDiagnostics() noexcept;
#else
inline void RecordNativeOwnedCaptureCopySkipped() noexcept {}
inline void RecordNativeOwnedHostReupload() noexcept {}
[[nodiscard]] inline NativeCaptureHostCopyDiagnostics
GetNativeCaptureHostCopyDiagnostics() noexcept
{
    return {};
}
#endif

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
    u64 MappedCaptureBytes = 0;
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
    // Packed header word 15. This lets the shader bypass all native capture
    // mapping work on ordinary frames whose BG/OBJ rows have no overlay.
    u32 NativeCaptureOverlayAny = 0;
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
    std::array<u32, NativeCaptureBGMappingWords> NativeCaptureBGMapping{};
    std::array<u32, NativeCaptureOBJMappingWords> NativeCaptureOBJMapping{};
    std::array<u32, NativeCaptureSpriteOBJMappingWords>
        NativeCaptureSpriteOBJMapping{};
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
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    std::array<MappedCaptureViolation, MaxMappedCaptureViolations>
        MappedCaptureViolations{};
    u32 MappedCaptureViolationCount = 0;
    u32 MappedCaptureViolationOverflow = 0;
#endif
};

// High-resolution display-capture provenance is a renderer-private derived
// cache of the persistent compact LCDC capture mirror. Capture stopping does
// not change either image. A sidecar segment remains readable until the
// corresponding compact content identity changes or renderer-private storage
// is reset. The table is indexed by 128 native-pixel segments, not by 32 KiB
// physical blocks: a one-line/128-pixel capture must never make the other
// pixels in that block readable from a stale sidecar.
enum class HighResCaptureFallbackReason : u32
{
    None = 0,
    InvalidProvenance,
    IdentityMismatch,
    ResourceReset,
    CpuWriteInvalidated,
    CaptureRetired,
    NoSidecarStorage,
    RepresentativeCompactMismatch,
    Count,
};

struct HighResCaptureProvenanceState
{
    // bit 0: a committed sidecar version is readable
    // bit 1: committed sidecar version (0/1)
    // bit 2: this semantic frame writes the physical block
    u32 ValidAndVersion = 0;
    NativeCaptureStateIdentity CommittedIdentity{};
    NativeCaptureStateIdentity CompactIdentity{};
    NativeCaptureStateIdentity PendingIdentity{};
    HighResCaptureFallbackReason LastInvalidationReason =
        HighResCaptureFallbackReason::InvalidProvenance;
};

inline constexpr u32 HighResCaptureValidBit = 1u << 0u;
inline constexpr u32 HighResCaptureVersionBit = 1u << 1u;
inline constexpr u32 HighResCapturePendingWriteBit = 1u << 2u;
// Shader ABI per segment:
// flags, committed completion lo/hi, pending completion lo/hi,
// compact completion lo/hi, last invalidation reason.
inline constexpr u32 HighResCaptureProvenanceWordsPerSegment = 8u;
// Kept as a source-compatibility alias for older diagnostics; the value is
// now explicitly the per-segment stride.
inline constexpr u32 HighResCaptureProvenanceWordsPerBlock =
    HighResCaptureProvenanceWordsPerSegment;
inline constexpr u32 HighResCaptureProvenanceWords =
    HighResCaptureSegmentCount * HighResCaptureProvenanceWordsPerSegment;
inline constexpr u32 PackedFrameAbiVersion = 6u;
using HighResCaptureProvenanceTable =
    std::array<HighResCaptureProvenanceState, HighResCaptureSegmentCount>;
using HighResCaptureSegmentMask = std::array<u8, HighResCaptureSegmentCount>;

[[nodiscard]] constexpr bool IsHighResCaptureCommittedIdentityValid(
    const HighResCaptureProvenanceState& state) noexcept
{
    return (state.ValidAndVersion & HighResCaptureValidBit) != 0u
        && state.CommittedIdentity.Valid
        && state.CompactIdentity.Valid
        && state.CommittedIdentity.CompletionValue != 0u
        && state.CommittedIdentity.CompletionValue
            == state.CompactIdentity.CompletionValue;
}

static_assert(CaptureWidthForSize(0u) == HighResCaptureSegmentHalfwords);
static_assert(CaptureWidthForSize(1u) == 2u * HighResCaptureSegmentHalfwords);
static_assert(CaptureOffsetHalfwords(0u) % HighResCaptureSegmentHalfwords == 0u);
static_assert(CaptureOffsetHalfwords(1u) % HighResCaptureSegmentHalfwords == 0u);
static_assert(CaptureOffsetHalfwords(2u) % HighResCaptureSegmentHalfwords == 0u);
static_assert(CaptureOffsetHalfwords(3u) % HighResCaptureSegmentHalfwords == 0u);
static_assert((ScreenHeight * CaptureWidthForSize(3u))
    % HighResCaptureSegmentHalfwords == 0u);

// Computes the 128-halfword segments written by the native capture shader in
// this semantic frame. It consumes the same latched per-line registers as the
// shader; presentation scale never changes the result.
[[nodiscard]] HighResCaptureSegmentMask ComputeCaptureWriteSegmentMask(
    const FrameInput& input) noexcept;

// Appends the fixed per-segment identity table to the common packed GPU2D input.
// This is separate from PackFrameRanges because the table belongs to the
// backend's renderer-private lifetime tracker, not to emulated state.
bool PackHighResCaptureProvenance(
    u32* destination,
    std::size_t wordCount,
    const HighResCaptureProvenanceTable& table,
    const FrameInput& input,
    u64 pendingCompletionValue) noexcept;

// Host-side state machine shared by Vulkan and DX12. BeginFrame exposes a
// pending write version for only the segments actually written by this frame;
// CommitFrame makes those versions readable only after submission succeeds.
// This is the same-bank read-before-write rule without a second full sidecar.
class HighResCaptureProvenanceTracker
{
public:
    void Invalidate(u64 epoch, u32 scaleFactor) noexcept;
    void BeginFrame(
        const FrameInput& input,
        const NativeCaptureStateIdentity& pendingIdentity,
        u32 scaleFactor) noexcept;
    void CommitFrame(const NativeCaptureStateIdentity& committedIdentity) noexcept;
    void AbortFrame() noexcept;
    void InvalidatePhysicalRange(
        u32 bank,
        u32 firstByte,
        u32 byteCount,
        HighResCaptureFallbackReason reason) noexcept;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    void LogPostGapTrace(const char* backend, const FrameInput& input) noexcept;
#else
    void LogPostGapTrace(const char*, const FrameInput&) noexcept {}
#endif

    [[nodiscard]] const HighResCaptureProvenanceTable& States() const noexcept
    {
        return Entries;
    }

    [[nodiscard]] const std::array<u64, HighResCaptureSegmentCount>&
    SemanticFrames() const noexcept
    {
        return LastSemanticFrame;
    }

private:
    HighResCaptureProvenanceTable Entries{};
    std::array<u64, HighResCaptureSegmentCount> LastSemanticFrame{};
    std::array<u8, HighResCaptureSegmentCount> Pending{};
    u64 Epoch = 0;
    u32 ScaleFactor = 0;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    u32 DiagnosticGapFrames = 0;
    u32 DiagnosticLastGapFrames = 0;
    u32 DiagnosticPostGapFrames = 0;
    bool DiagnosticSawCaptureWrite = false;
    std::array<u64, static_cast<u32>(HighResCaptureFallbackReason::Count)>
        DiagnosticFallbackCounts{};
#endif
};

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
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
#else
// Shipping builds do not allocate/read back or log the diagnostic stages.
inline void LogStageSnapshot(
    const char*, u64, u64, u64, u64, u32, const FrameInput&, const u32*,
    const u32*, const u32*, const char*, const u32*, const u32*) noexcept {}
inline void LogPresentedIdentity(
    const char*, u64, u64, u64, u64, u32) noexcept {}
inline void LogSemanticIdentity(
    const char*, u64, u64, u64, bool, bool, bool, u32) noexcept {}
#endif

static_assert(std::is_trivially_copyable_v<FrameInput>,
    "FrameInput must remain memset-resettable without a stack-sized temporary");

// Fixed serialization layout consumed by both native shader backends.  The
// byte arrays are copied verbatim into u32 words; shaders perform the byte
// and BGR555 reads, so no host-side pixel generation is hidden in the packer.
// Words 32..33 carry the renderer-global completion identity for the semantic
// submission currently being recorded. The first 32 words retain their prior
// meanings so the ABI extension is explicit and append-only.
inline constexpr u32 PackedHeaderWords = 34;
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
inline constexpr u32 PackedNativeCaptureBGMappingBase =
    PackedSpriteTimelineRowsBase + PackedSpriteTimelineRowsWords;
inline constexpr u32 PackedNativeCaptureOBJMappingBase =
    PackedNativeCaptureBGMappingBase + NativeCaptureBGMappingWords;
inline constexpr u32 PackedNativeCaptureSpriteOBJMappingBase =
    PackedNativeCaptureOBJMappingBase + NativeCaptureOBJMappingWords;
inline constexpr u32 PackedHighResCaptureProvenanceBase =
    PackedNativeCaptureSpriteOBJMappingBase
    + NativeCaptureSpriteOBJMappingWords;
inline constexpr u32 PackedFrameWords =
    PackedHighResCaptureProvenanceBase + HighResCaptureProvenanceWords;

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
    // Capture control registers can change after the line-start snapshot and
    // before the hardware capture write. Refresh engine-A state at that
    // boundary so Native observes the same state as Software.
    void CaptureCaptureStateForLine(u32 line) noexcept;
    void CaptureMemoryForLine(u32 line) noexcept;
    void RecordSoftwareCaptureLine(u64 nanoseconds) noexcept;
    // Called from the renderer's DrawSprites hook at the hardware latch
    // point, after DrawScanline(line) and before DrawScanline(line+1).
    void CaptureSpriteLatchForLine(u32 line) noexcept;
    void FinalizeMemory() noexcept;

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

    // Native Display Capture writes one LCDC line ahead of the line that can
    // consume it. Keep that boundary incrementally instead of rebuilding it
    // by scanning all preceding LineState records for every mapped read.
    std::array<u8, CapturePhysicalBanks> NativeCaptureWrittenBlocks{};

    // CaptureNativeMappingForLine can be reached from the memory snapshot,
    // line-state, and late capture-state hooks. Cache the source mapping and
    // write-ahead inputs so unchanged calls are O(1) no-ops while preserving
    // a rebuild when a mid-frame remap or ownership boundary changes.
    std::array<std::array<u32, 64>, 2> NativeCaptureMappingSources{};
    std::array<std::array<u8, CapturePhysicalBanks>, 2>
        NativeCaptureMappingWrittenBlocks{};
    std::array<u64, 2> NativeCaptureMappingProvenanceSerial{};
    std::array<u32, 2> NativeCaptureMappingLines{
        ScreenHeight, ScreenHeight};
    std::array<bool, 2> NativeCaptureMappingBuilt{false, false};

    std::array<CaptureAddressDiagnostic, MaxCaptureAddressDiagnostics>
        CaptureAddressLog{};
    u32 CaptureAddressLogCount = 0u;
    u32 CaptureAddressLogOverflow = 0u;

    void SnapshotEngine(u32 engine, u32 line) noexcept;
    void CaptureAllMappedMemoryForLine(u32 line) noexcept;
    void CaptureCoherentLCDVRAMForLine() noexcept;
    void CaptureJournalWritesForLine(u32 line) noexcept;
    void RecordCaptureAddressLine(u32 line, u32 captureCnt) noexcept;
    void BeginCaptureAddressDiagnostic(u32 line, u32 captureCnt) noexcept;
    void FinalizeCaptureAddressDiagnostics() noexcept;
    void FinalizeMappedCaptureDiagnostics() noexcept;
    void RefreshCaptureProvenance() noexcept;
    void MarkInputCaptureBlockCpuCoherent(u32 bank, u32 physicalBlock) noexcept;
    void CaptureMappedPhysicalBlock(
        u32 line,
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
        u32 line,
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
    void CaptureNativeMappingForLine(u32 line, bool spriteLatch) noexcept;
    void CommitNativeCaptureWriteAheadForLine(u32 line) noexcept;
    void ApplyPendingNativeSpriteMapping() noexcept;

    std::array<u32, NativeCaptureOBJMappingStride>
        PendingNativeCaptureSpriteOBJMapping{};
};

} // namespace GPU2DNative
} // namespace melonDS

#endif // GPU2D_NATIVE_H
