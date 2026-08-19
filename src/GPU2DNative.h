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

// Developer-only Gate B switch. The normal renderer never waits for a
// compositor readback; setting MELONPRIME_GPU2D_EXACT_VALIDATE=1 (or the
// shorter MELONPRIME_GPU2D_EXACT=1) enables exact native-output validation.
[[nodiscard]] bool ExactValidationEnabled() noexcept;

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
    std::array<u32, 2> Padding{};
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
    u64 ContentGeneration = 0;
    u64 VRAMGeneration = 0;
    u64 CaptureGeneration = 0;
};

inline constexpr u32 DirtyBlockBytes = 512u;
inline constexpr u32 MaxDirtyRanges = 8192u;

struct DirtyRange
{
    u32 Offset = 0;
    u32 Size = 0;
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
    FrameGeneration Generation{};
    // Byte ranges in the serialized frame that changed since the previous
    // frame. They are metadata only and are not part of PackedFrameWords.
    std::array<DirtyRange, MaxDirtyRanges> DirtyRanges{};
    u32 DirtyRangeCount = 0;
};

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
inline constexpr u32 PackedFrameWords = PackedRouteBase + PackedRouteWords;

static_assert(PackedLineWords == 68u, "native line serialization drift");

[[nodiscard]] constexpr std::size_t PackedFrameBytes() noexcept
{
    return static_cast<std::size_t>(PackedFrameWords) * sizeof(u32);
}

// Returns false only when the destination is too small or null.  This is a
// state/memory pack operation; it never converts pixels or runs the software
// renderer.
bool PackFrame(const FrameInput& input, u32* destination, std::size_t wordCount) noexcept;

// Builds non-overlapping serialized upload ranges. The first use of a device
// slot must pass fullUpload=true; subsequent uses can copy only the ranges
// recorded by FrameRecorder, leaving unchanged GPU-resident mirrors intact.
UploadPlan BuildUploadPlan(const FrameInput& input, bool fullUpload) noexcept;

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
    explicit FrameRecorder(melonDS::GPU& gpu) noexcept;

    void Reset() noexcept;
    void BeginFrame(u64 frame) noexcept;
    void CaptureLine(
        u32 engine,
        const melonDS::GPU2D& gpu2D,
        u32 line,
        bool screenSwap) noexcept;
    void FinalizeMemory() noexcept;

    [[nodiscard]] const FrameInput& GetFrame() const noexcept { return Input; }
    [[nodiscard]] bool IsValid() const noexcept { return Valid; }

private:
    melonDS::GPU& GPU;
    FrameInput Input{};
    bool Valid = false;
    bool EngineLineSeen[2] = {false, false};

    void SnapshotEngine(u32 engine, const melonDS::GPU2D& gpu2D) noexcept;
};

} // namespace GPU2DNative
} // namespace melonDS

#endif // GPU2D_NATIVE_H
