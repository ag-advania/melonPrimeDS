/*
    Copyright 2016-2026 melonDS team
*/

#include "GPU2DNative.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "GPU.h"

namespace melonDS::GPU2DNative
{

bool ExactValidationEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_GPU2D_EXACT_VALIDATE");
        if (!value || value[0] == '\0')
            value = std::getenv("MELONPRIME_GPU2D_EXACT");
        if (!value)
            return false;
        return value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
            || value[0] == 't' || value[0] == 'T';
    }();
    return enabled;
}

bool IsExactValidationSavestateTransitionFrame(u64 frame) noexcept
{
    static const bool diagnosticSavestate = [] {
        const char* value = std::getenv("MELONPRIME_TEST_SAVESTATE");
        return value && value[0] != '\0';
    }();
    return diagnosticSavestate && frame == 1u;
}

namespace
{
void MarkDirtyRange(FrameInput& input, u32 offset, u32 size) noexcept
{
    if (size == 0u)
        return;
    if (input.DirtyRangeCount != 0u)
    {
        DirtyRange& previous = input.DirtyRanges[input.DirtyRangeCount - 1u];
        if (previous.Offset + previous.Size == offset)
        {
            previous.Size += size;
            return;
        }
    }
    if (input.DirtyRangeCount >= MaxDirtyRanges)
    {
        input.DirtyRangeCount = 1u;
        input.DirtyRanges[0] = {
            0u, static_cast<u32>(PackedFrameBytes())};
        return;
    }
    input.DirtyRanges[input.DirtyRangeCount++] = {offset, size};
}

void CopyChangedBlocks(
    FrameInput& input,
    u8* destination,
    const u8* source,
    u32 size,
    u32 packedOffset) noexcept
{
    if (!destination || !source || size == 0u)
        return;
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        if (std::memcmp(destination + offset, source + offset, blockSize) == 0)
            continue;
        std::memcpy(destination + offset, source + offset, blockSize);
        MarkDirtyRange(input, packedOffset + offset, blockSize);
    }
}

void CopyLineState(LineState& destination, const GPU2D& source, u32 renderXPos) noexcept
{
    destination = {};
    destination.DispCnt = source.DispCnt;
    destination.LayerEnable = source.LayerEnable;
    destination.OBJEnable = source.OBJEnable;
    destination.ForcedBlank = source.ForcedBlank;
    for (u32 i = 0; i < 4u; ++i)
    {
        destination.BGCnt[i] = source.BGCnt[i];
        destination.BGXPos[i] = source.BGXPos[i];
        destination.BGYPos[i] = source.BGYPos[i];
        destination.Win0Coords[i] = source.Win0Coords[i];
        destination.Win1Coords[i] = source.Win1Coords[i];
        destination.WinCnt[i] = source.WinCnt[i];
    }
    for (u32 i = 0; i < 2u; ++i)
    {
        destination.BGXRefInternal[i] = source.BGXRefInternal[i];
        destination.BGYRefInternal[i] = source.BGYRefInternal[i];
        destination.BGRotA[i] = source.BGRotA[i];
        destination.BGRotB[i] = source.BGRotB[i];
        destination.BGRotC[i] = source.BGRotC[i];
        destination.BGRotD[i] = source.BGRotD[i];
        destination.BGMosaicSize[i] = source.BGMosaicSize[i];
        destination.OBJMosaicSize[i] = source.OBJMosaicSize[i];
    }
    destination.Win0Active = source.Win0Active;
    destination.Win1Active = source.Win1Active;
    destination.BGMosaicLine = source.BGMosaicLine;
    destination.OBJMosaicLine = source.OBJMosaicLine;
    destination.BlendCnt = source.BlendCnt;
    destination.BlendAlpha = source.BlendAlpha;
    destination.EVA = source.EVA;
    destination.EVB = source.EVB;
    destination.EVY = source.EVY;
    destination.RenderXPos = renderXPos & 0x1FFu;

    // Keep the same packed window representation as the OpenGL scanline
    // configuration.  The active bit is the vertical latch; the horizontal
    // bit is consumed locally here, so a shader does not need to mutate state
    // while evaluating a pixel.
    destination.WinRegs = (source.DispCnt & 0xE000u) != 0u
        ? source.WinCnt[2]
        : 0xFFu;
    destination.WinRegs |= (source.DispCnt & (1u << 15u))
        ? static_cast<u32>(source.WinCnt[3]) << 8u : 0xFF00u;
    destination.WinRegs |= (source.DispCnt & (1u << 14u))
        ? static_cast<u32>(source.WinCnt[1]) << 16u : 0xFF0000u;
    destination.WinRegs |= (source.DispCnt & (1u << 13u))
        ? static_cast<u32>(source.WinCnt[0]) << 24u : 0xFF000000u;

    destination.WinPos = {256u, 256u, 256u, 256u};
    destination.WinMask = 0;
    if ((source.DispCnt & (1u << 13u)) && (source.Win0Active & 0x1u))
    {
        const u32 x0 = source.Win0Coords[0];
        const u32 x1 = source.Win0Coords[1];
        if (x0 <= x1)
        {
            destination.WinPos[0] = x0;
            destination.WinPos[1] = x1;
            if (source.Win0Active == 0x3u)
                destination.WinMask |= 1u << 0u;
            destination.WinMask |= 1u << 1u;
        }
        else
        {
            destination.WinPos[0] = x1;
            destination.WinPos[1] = x0;
            if (source.Win0Active == 0x3u)
                destination.WinMask |= 1u << 0u;
            destination.WinMask |= 1u << 2u;
        }
    }
    if ((source.DispCnt & (1u << 14u)) && (source.Win1Active & 0x1u))
    {
        const u32 x0 = source.Win1Coords[0];
        const u32 x1 = source.Win1Coords[1];
        if (x0 <= x1)
        {
            destination.WinPos[2] = x0;
            destination.WinPos[3] = x1;
            if (source.Win1Active == 0x3u)
                destination.WinMask |= 1u << 3u;
            destination.WinMask |= 1u << 4u;
        }
        else
        {
            destination.WinPos[2] = x1;
            destination.WinPos[3] = x0;
            if (source.Win1Active == 0x3u)
                destination.WinMask |= 1u << 3u;
            destination.WinMask |= 1u << 5u;
        }
    }
}

bool HasDirtyOverlap(const FrameInput& input, u32 begin, u32 end) noexcept
{
    for (u32 i = 0; i < input.DirtyRangeCount; ++i)
    {
        const DirtyRange& range = input.DirtyRanges[i];
        const u32 rangeEnd = range.Offset + range.Size;
        if (range.Offset < end && rangeEnd > begin)
            return true;
    }
    return false;
}

template <u32 MappingBytes>
u64 ReadMappedWord(
    const melonDS::GPU& gpu,
    const u32* mappings,
    u32 mappingCount,
    u32 address) noexcept
{
    const u32 mappingIndex = address / MappingBytes;
    if (mappingIndex >= mappingCount)
        return 0;

    const u32 bankMask = mappings[mappingIndex];
    u64 value = 0;
    for (u32 bank = 0; bank < 9u; ++bank)
    {
        if ((bankMask & (1u << bank)) == 0u)
            continue;

        u64 bankValue = 0;
        std::memcpy(
            &bankValue,
            gpu.VRAM[bank] + (address & gpu.VRAMMask[bank]),
            sizeof(bankValue));
        value |= bankValue;
    }
    return value;
}

template <u32 MappingBytes>
void CopyMappedVRAMBlocks(
    FrameInput& input,
    u8* destination,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    const melonDS::GPU& gpu,
    u32 packedOffset) noexcept
{
    std::array<u8, DirtyBlockBytes> source{};
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        source.fill(0);
        for (u32 blockOffset = 0; blockOffset < blockSize; blockOffset += sizeof(u64))
        {
            const u64 value = ReadMappedWord<MappingBytes>(
                gpu, mappings, mappingCount, offset + blockOffset);
            std::memcpy(source.data() + blockOffset, &value, sizeof(value));
        }

        if (std::memcmp(destination + offset, source.data(), blockSize) == 0)
            continue;
        std::memcpy(destination + offset, source.data(), blockSize);
        MarkDirtyRange(input, packedOffset + offset, blockSize);
    }
}
}

FrameRecorder::FrameRecorder(const melonDS::GPU& gpu) noexcept
    : GPU(gpu)
{
}

void FrameRecorder::Reset() noexcept
{
    Input = {};
    Valid = false;
    EngineLineSeen[0] = false;
    EngineLineSeen[1] = false;
}

void FrameRecorder::BeginFrame(u64 frame) noexcept
{
    const FrameGeneration previousGeneration = Input.Generation;
    const bool hadPreviousFrame = Valid;
    if (!Valid)
        Input = {};
    else
    {
        // Retain the coherent memory mirrors so changed blocks can be copied
        // into both the CPU frame and the backend's device-resident mirror.
        std::fill(Input.Lines.begin(), Input.Lines.end(), LineState{});
        std::fill(Input.ScreenSource.begin(), Input.ScreenSource.end(), 0u);
        Input.DirtyRangeCount = 0u;
    }
    Input.Generation.Frame = frame;
    if (hadPreviousFrame)
    {
        Input.Generation.ContentGeneration = previousGeneration.ContentGeneration;
        Input.Generation.VRAMGeneration = previousGeneration.VRAMGeneration;
        Input.Generation.CaptureGeneration = previousGeneration.CaptureGeneration;
    }
    else
    {
        // A newly created recorder has no device mirror history.  The first
        // slot use still performs a full upload, while the non-zero seed makes
        // a later slot generation comparison unambiguous after the first frame.
        Input.Generation.ContentGeneration = 1u;
        Input.Generation.VRAMGeneration = 1u;
        Input.Generation.CaptureGeneration = 1u;
    }
    Input.CaptureCnt = GPU.CaptureCnt;
    Input.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    Input.ScreenSwap = GPU.ScreenSwap ? 1u : 0u;
    Input.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    Input.LCDVRAMMap = GPU.VRAMMap_LCDC;
    MarkDirtyRange(Input, 0u, PackedHeaderWords * sizeof(u32));
    MarkDirtyRange(Input, PackedHeaderWords * sizeof(u32),
        PackedLinesWords * sizeof(u32));
    MarkDirtyRange(Input, PackedRouteBase * sizeof(u32),
        PackedRouteWords * sizeof(u32));
    // A display-capture write is visible to later scanlines, not to the
    // scanline whose display read happened before DoCapture(). Keep the
    // frame-start LCD mirror here; the core owns the final VRAM state that is
    // copied into the next frame's snapshot.
    for (u32 bank = 0; bank < 4u; ++bank)
    {
        CopyChangedBlocks(
            Input,
            Input.LCDVRAM.data()
                + static_cast<std::size_t>(bank) * 128u * 1024u,
            GPU.VRAM[bank],
            128u * 1024u,
            PackedLCDVRAMBase * sizeof(u32)
                + bank * 128u * 1024u);
    }
    EngineLineSeen[0] = false;
    EngineLineSeen[1] = false;
    Valid = false;
}

void FrameRecorder::CaptureLine(
    u32 engine,
    const melonDS::GPU2D& gpu2D,
    u32 line,
    bool screenSwap) noexcept
{
    if (engine >= 2u || line >= ScreenHeight)
        return;

    if (line == 0u)
        EngineLineSeen[engine] = true;
    CopyLineState(
        Input.Lines[engine * ScreenHeight + line],
        gpu2D,
        GPU.GPU3D.GetRenderXPos());
    LineState& state = Input.Lines[engine * ScreenHeight + line];
    state.UnitEnabled = gpu2D.Enabled ? 1u : 0u;
    state.MasterBrightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
    state.CaptureCnt = GPU.CaptureCnt;
    state.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    state.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    state.ScreenSwap = screenSwap ? 1u : 0u;

    // This is the emulation-time LCD assignment.  Present-time POWCNT1 is not
    // authoritative when a title changes routing within a frame.
    const u32 screenA = screenSwap ? 0u : 1u;
    const u32 screenB = screenA ^ 1u;
    Input.ScreenSource[screenA * ScreenHeight + line] = 0u;
    Input.ScreenSource[screenB * ScreenHeight + line] = 1u;
}

void FrameRecorder::SnapshotEngine(u32 engine) noexcept
{
    if (engine >= 2u)
        return;

    MemorySnapshot& destination = Input.Engine[engine];

    const bool engineA = engine == 0u;
    const u32 bgSize = engineA ? 512u * 1024u : 128u * 1024u;
    const u32 objSize = engineA ? 256u * 1024u : 128u * 1024u;
    const u32* bgMappings = engineA ? GPU.VRAMMap_ABG : GPU.VRAMMap_BBG;
    const u32 bgMappingCount = engineA ? 32u : 8u;
    const u32* objMappings = engineA ? GPU.VRAMMap_AOBJ : GPU.VRAMMap_BOBJ;
    const u32 objMappingCount = engineA ? 16u : 8u;
    const u32* bgExtMappings = engineA ? GPU.VRAMMap_ABGExtPal : GPU.VRAMMap_BBGExtPal;
    const u32 bgExtMappingCount = 4u;
    const u32* objExtMappings = engineA
        ? &GPU.VRAMMap_AOBJExtPal : &GPU.VRAMMap_BOBJExtPal;

    destination.BGSize = bgSize;
    destination.OBJSize = objSize;
    destination.BGExtendedPaletteSize = 32u * 1024u;
    destination.OBJExtendedPaletteSize = 8u * 1024u;
    const u32 engineBase = PackedEngineBase + engine * PackedEngineWords;
    CopyMappedVRAMBlocks<16u * 1024u>(
        Input, destination.BGVRAM.data(), destination.BGSize,
        bgMappings, bgMappingCount, GPU,
        engineBase * sizeof(u32));
    CopyMappedVRAMBlocks<16u * 1024u>(
        Input, destination.OBJVRAM.data(), destination.OBJSize,
        objMappings, objMappingCount, GPU,
        (engineBase + PackedBGWords) * sizeof(u32));
    CopyMappedVRAMBlocks<8u * 1024u>(
        Input, destination.BGExtendedPalette.data(),
        destination.BGExtendedPaletteSize, bgExtMappings, bgExtMappingCount,
        GPU, (engineBase + PackedBGWords + PackedOBJWords) * sizeof(u32));
    CopyMappedVRAMBlocks<8u * 1024u>(
        Input, destination.OBJExtendedPalette.data(),
        destination.OBJExtendedPaletteSize, objExtMappings, 1u, GPU,
        (engineBase + PackedBGWords + PackedOBJWords
            + PackedBGExtendedPaletteWords) * sizeof(u32));
}

void FrameRecorder::FinalizeMemory() noexcept
{
    if (!EngineLineSeen[0] || !EngineLineSeen[1])
        return;

    SnapshotEngine(0u);
    SnapshotEngine(1u);

    CopyChangedBlocks(
        Input, Input.Palette.data(), GPU.Palette,
        static_cast<u32>(Input.Palette.size()), PackedPaletteBase * sizeof(u32));
    CopyChangedBlocks(
        Input, Input.OAM.data(), GPU.OAM,
        static_cast<u32>(Input.OAM.size()), PackedOAMBase * sizeof(u32));
    CopyChangedBlocks(
        Input,
        reinterpret_cast<u8*>(Input.DisplayFIFO.data()),
        reinterpret_cast<const u8*>(GPU.DispFIFOBuffer),
        static_cast<u32>(Input.DisplayFIFO.size() * sizeof(u16)),
        PackedFIFOBase * sizeof(u32));
    if (HasDirtyOverlap(
            Input,
            PackedEngineBase * sizeof(u32),
            PackedPaletteBase * sizeof(u32)))
    {
        ++Input.Generation.VRAMGeneration;
    }
    if (HasDirtyOverlap(
            Input,
            PackedPaletteBase * sizeof(u32),
            PackedLCDVRAMBase * sizeof(u32)))
    {
        ++Input.Generation.ContentGeneration;
    }
    if (HasDirtyOverlap(
            Input,
            PackedLCDVRAMBase * sizeof(u32),
            PackedRouteBase * sizeof(u32)))
    {
        ++Input.Generation.CaptureGeneration;
    }
    Input.CaptureCnt = GPU.CaptureCnt;
    Input.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    Input.ScreenSwap = GPU.ScreenSwap ? 1u : 0u;
    Input.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    Valid = true;
}

} // namespace melonDS::GPU2DNative
