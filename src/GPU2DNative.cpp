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
}

FrameRecorder::FrameRecorder(melonDS::GPU& gpu) noexcept
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
    Input.Generation.ContentGeneration = frame;
    Input.Generation.VRAMGeneration = frame;
    Input.Generation.CaptureGeneration = frame;
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

void FrameRecorder::SnapshotEngine(u32 engine, const melonDS::GPU2D& gpu2D) noexcept
{
    if (engine >= 2u)
        return;

    if (engine == 0u)
    {
        auto bgDirty = GPU.VRAMDirty_ABG.DeriveState(GPU.VRAMMap_ABG, GPU);
        GPU.MakeVRAMFlat_ABGCoherent(bgDirty);
        auto objDirty = GPU.VRAMDirty_AOBJ.DeriveState(GPU.VRAMMap_AOBJ, GPU);
        GPU.MakeVRAMFlat_AOBJCoherent(objDirty);
        auto bgExtDirty = GPU.VRAMDirty_ABGExtPal.DeriveState(GPU.VRAMMap_ABGExtPal, GPU);
        GPU.MakeVRAMFlat_ABGExtPalCoherent(bgExtDirty);
        auto objExtDirty = GPU.VRAMDirty_AOBJExtPal.DeriveState(&GPU.VRAMMap_AOBJExtPal, GPU);
        GPU.MakeVRAMFlat_AOBJExtPalCoherent(objExtDirty);
    }
    else
    {
        auto bgDirty = GPU.VRAMDirty_BBG.DeriveState(GPU.VRAMMap_BBG, GPU);
        GPU.MakeVRAMFlat_BBGCoherent(bgDirty);
        auto objDirty = GPU.VRAMDirty_BOBJ.DeriveState(GPU.VRAMMap_BOBJ, GPU);
        GPU.MakeVRAMFlat_BOBJCoherent(objDirty);
        auto bgExtDirty = GPU.VRAMDirty_BBGExtPal.DeriveState(GPU.VRAMMap_BBGExtPal, GPU);
        GPU.MakeVRAMFlat_BBGExtPalCoherent(bgExtDirty);
        auto objExtDirty = GPU.VRAMDirty_BOBJExtPal.DeriveState(&GPU.VRAMMap_BOBJExtPal, GPU);
        GPU.MakeVRAMFlat_BOBJExtPalCoherent(objExtDirty);
    }

    MemorySnapshot& destination = Input.Engine[engine];
    u8* bg = nullptr;
    u32 bgMask = 0;
    gpu2D.GetBGVRAM(bg, bgMask);
    destination.BGSize = std::min<u32>(bgMask + 1u, destination.BGVRAM.size());
    if (bg && destination.BGSize != 0u)
        CopyChangedBlocks(
            Input, destination.BGVRAM.data(), bg, destination.BGSize,
            (PackedEngineBase + engine * PackedEngineWords) * sizeof(u32));

    u8* obj = nullptr;
    u32 objMask = 0;
    gpu2D.GetOBJVRAM(obj, objMask);
    destination.OBJSize = std::min<u32>(objMask + 1u, destination.OBJVRAM.size());
    if (obj && destination.OBJSize != 0u)
        CopyChangedBlocks(
            Input, destination.OBJVRAM.data(), obj, destination.OBJSize,
            (PackedEngineBase + engine * PackedEngineWords + PackedBGWords)
                * sizeof(u32));

    const u8* bgExt = engine == 0u ? GPU.VRAMFlat_ABGExtPal : GPU.VRAMFlat_BBGExtPal;
    const u8* objExt = engine == 0u ? GPU.VRAMFlat_AOBJExtPal : GPU.VRAMFlat_BOBJExtPal;
    destination.BGExtendedPaletteSize = 32u * 1024u;
    destination.OBJExtendedPaletteSize = 8u * 1024u;
    CopyChangedBlocks(
        Input, destination.BGExtendedPalette.data(), bgExt,
        destination.BGExtendedPaletteSize,
        (PackedEngineBase + engine * PackedEngineWords + PackedBGWords
            + PackedOBJWords) * sizeof(u32));
    CopyChangedBlocks(
        Input, destination.OBJExtendedPalette.data(), objExt,
        destination.OBJExtendedPaletteSize,
        (PackedEngineBase + engine * PackedEngineWords + PackedBGWords
            + PackedOBJWords + PackedBGExtendedPaletteWords) * sizeof(u32));
}

void FrameRecorder::FinalizeMemory() noexcept
{
    if (!EngineLineSeen[0] || !EngineLineSeen[1])
        return;

    SnapshotEngine(0u, GPU.GPU2D_A);
    SnapshotEngine(1u, GPU.GPU2D_B);
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
    Input.CaptureCnt = GPU.CaptureCnt;
    Input.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    Input.ScreenSwap = GPU.ScreenSwap ? 1u : 0u;
    Input.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    Valid = true;
}

} // namespace melonDS::GPU2DNative
