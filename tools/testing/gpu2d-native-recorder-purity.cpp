/*
    Side-effect and multi-consumer tests for the native GPU2D observer.

    The FrameRecorder may update only its private FrameInput.  In particular,
    it must not consume the shared VRAM dirty state or mapping history that
    existing 2D/3D consumers use.
*/

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "GPU2DNative.h"
#include "NDS.h"
#include "gpu2d-native-recorder-purity.h"

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

struct GPUStateSnapshot
{
    std::array<std::vector<u8>, 9> VRAM;
    std::array<std::vector<u64>, 9> VRAMDirty;
    std::array<u16, 32> ABGMapping{};
    std::array<u16, 16> AOBJMapping{};
    std::array<u16, 8> BBGMapping{};
    std::array<u16, 8> BOBJMapping{};
    std::array<u16, 4> ABGExtPalMapping{};
    std::array<u16, 4> BBGExtPalMapping{};
    u16 AOBJExtPalMapping = 0;
    u16 BOBJExtPalMapping = 0;
    std::array<u8, 2 * 1024> Palette{};
    std::array<u8, 2 * 1024> OAM{};
    std::array<u16, 256> DisplayFIFO{};
    std::array<u32, 2> DispCnt{};
    std::array<u8, 2> LayerEnable{};
    std::array<u8, 2> OBJEnable{};
    std::array<u8, 2> ForcedBlank{};
    u32 CaptureCnt = 0;
    bool CaptureEnable = false;
    bool ScreenSwap = false;
    bool ScreensEnabled = false;
    u32 VCount = 0;
    u32 LCDCMap = 0;
    u32 PaletteDirty = 0;
    u32 OAMDirty = 0;
    u32 RenderXPos = 0;
};

GPUStateSnapshot CaptureState(const GPU& gpu)
{
    GPUStateSnapshot snapshot;
    for (u32 bank = 0; bank < 9u; ++bank)
    {
        const u32 size = gpu.VRAMMask[bank] + 1u;
        snapshot.VRAM[bank].assign(gpu.VRAM[bank], gpu.VRAM[bank] + size);
        snapshot.VRAMDirty[bank].assign(
            gpu.VRAMDirty[bank].Data,
            gpu.VRAMDirty[bank].Data
                + NonStupidBitField<128 * 1024 / VRAMDirtyGranularity>::DataLength);
    }
    std::memcpy(snapshot.ABGMapping.data(), gpu.VRAMDirty_ABG.Mapping,
        sizeof(snapshot.ABGMapping));
    std::memcpy(snapshot.AOBJMapping.data(), gpu.VRAMDirty_AOBJ.Mapping,
        sizeof(snapshot.AOBJMapping));
    std::memcpy(snapshot.BBGMapping.data(), gpu.VRAMDirty_BBG.Mapping,
        sizeof(snapshot.BBGMapping));
    std::memcpy(snapshot.BOBJMapping.data(), gpu.VRAMDirty_BOBJ.Mapping,
        sizeof(snapshot.BOBJMapping));
    std::memcpy(snapshot.ABGExtPalMapping.data(), gpu.VRAMDirty_ABGExtPal.Mapping,
        sizeof(snapshot.ABGExtPalMapping));
    std::memcpy(snapshot.BBGExtPalMapping.data(), gpu.VRAMDirty_BBGExtPal.Mapping,
        sizeof(snapshot.BBGExtPalMapping));
    snapshot.AOBJExtPalMapping = gpu.VRAMDirty_AOBJExtPal.Mapping[0];
    snapshot.BOBJExtPalMapping = gpu.VRAMDirty_BOBJExtPal.Mapping[0];
    std::memcpy(snapshot.Palette.data(), gpu.Palette, snapshot.Palette.size());
    std::memcpy(snapshot.OAM.data(), gpu.OAM, snapshot.OAM.size());
    std::memcpy(snapshot.DisplayFIFO.data(), gpu.DispFIFOBuffer,
        sizeof(snapshot.DisplayFIFO));
    snapshot.DispCnt = {gpu.GPU2D_A.DispCnt, gpu.GPU2D_B.DispCnt};
    snapshot.LayerEnable = {gpu.GPU2D_A.LayerEnable, gpu.GPU2D_B.LayerEnable};
    snapshot.OBJEnable = {gpu.GPU2D_A.OBJEnable, gpu.GPU2D_B.OBJEnable};
    snapshot.ForcedBlank = {gpu.GPU2D_A.ForcedBlank, gpu.GPU2D_B.ForcedBlank};
    snapshot.CaptureCnt = gpu.CaptureCnt;
    snapshot.CaptureEnable = gpu.CaptureEnable;
    snapshot.ScreenSwap = gpu.ScreenSwap;
    snapshot.ScreensEnabled = gpu.ScreensEnabled;
    snapshot.VCount = gpu.VCount;
    snapshot.LCDCMap = gpu.VRAMMap_LCDC;
    snapshot.PaletteDirty = gpu.PaletteDirty;
    snapshot.OAMDirty = gpu.OAMDirty;
    snapshot.RenderXPos = gpu.GPU3D.GetRenderXPos();
    return snapshot;
}

bool EqualState(const GPUStateSnapshot& lhs, const GPUStateSnapshot& rhs)
{
    if (lhs.VRAM != rhs.VRAM || lhs.VRAMDirty != rhs.VRAMDirty
        || lhs.ABGMapping != rhs.ABGMapping
        || lhs.AOBJMapping != rhs.AOBJMapping
        || lhs.BBGMapping != rhs.BBGMapping
        || lhs.BOBJMapping != rhs.BOBJMapping
        || lhs.ABGExtPalMapping != rhs.ABGExtPalMapping
        || lhs.BBGExtPalMapping != rhs.BBGExtPalMapping
        || lhs.AOBJExtPalMapping != rhs.AOBJExtPalMapping
        || lhs.BOBJExtPalMapping != rhs.BOBJExtPalMapping
        || lhs.Palette != rhs.Palette || lhs.OAM != rhs.OAM
        || lhs.DisplayFIFO != rhs.DisplayFIFO || lhs.DispCnt != rhs.DispCnt
        || lhs.LayerEnable != rhs.LayerEnable || lhs.OBJEnable != rhs.OBJEnable
        || lhs.ForcedBlank != rhs.ForcedBlank
        || lhs.CaptureCnt != rhs.CaptureCnt
        || lhs.CaptureEnable != rhs.CaptureEnable
        || lhs.ScreenSwap != rhs.ScreenSwap
        || lhs.ScreensEnabled != rhs.ScreensEnabled
        || lhs.VCount != rhs.VCount || lhs.LCDCMap != rhs.LCDCMap
        || lhs.PaletteDirty != rhs.PaletteDirty
        || lhs.OAMDirty != rhs.OAMDirty
        || lhs.RenderXPos != rhs.RenderXPos)
        return false;
    return true;
}

void SeedSharedState(GPU& gpu)
{
    gpu.VRAMMap_ABG[0] = 1u << 0u;
    gpu.VRAMMap_AOBJ[0] = 1u << 0u;
    gpu.VRAMMap_BBG[0] = 1u << 2u;
    gpu.VRAMMap_BOBJ[0] = 1u << 3u;
    gpu.VRAMMap_ABGExtPal[0] = 1u << 4u;
    gpu.VRAMMap_BBGExtPal[0] = 1u << 7u;
    gpu.VRAMMap_AOBJExtPal = 1u << 5u;
    gpu.VRAMMap_BOBJExtPal = 1u << 8u;
    gpu.VRAMMap_Texture[0] = 1u << 0u;

    gpu.VRAMDirty_ABG.Mapping[0] = gpu.VRAMMap_ABG[0];
    gpu.VRAMDirty_AOBJ.Mapping[0] = gpu.VRAMMap_AOBJ[0];
    gpu.VRAMDirty_BBG.Mapping[0] = gpu.VRAMMap_BBG[0];
    gpu.VRAMDirty_BOBJ.Mapping[0] = gpu.VRAMMap_BOBJ[0];
    gpu.VRAMDirty_ABGExtPal.Mapping[0] = gpu.VRAMMap_ABGExtPal[0];
    gpu.VRAMDirty_BBGExtPal.Mapping[0] = gpu.VRAMMap_BBGExtPal[0];
    gpu.VRAMDirty_AOBJExtPal.Mapping[0] = gpu.VRAMMap_AOBJExtPal;
    gpu.VRAMDirty_BOBJExtPal.Mapping[0] = gpu.VRAMMap_BOBJExtPal;
    gpu.VRAMDirty_Texture.Mapping[0] = gpu.VRAMMap_Texture[0];

    gpu.VRAM[0][0x10] = 0x11;
    gpu.VRAM[2][0x20] = 0x22;
    gpu.VRAM[3][0x30] = 0x33;
    gpu.VRAM[4][0x40] = 0x44;
    gpu.VRAM[5][0x50] = 0x55;
    gpu.VRAM[7][0x60] = 0x66;
    gpu.VRAM[8][0x70] = 0x77;
    gpu.VRAMDirty[0][0] = true;
    gpu.VRAMDirty[2][0] = true;
    gpu.VRAMDirty[3][0] = true;
    gpu.Palette[37] = 0xA5;
    gpu.OAM[513] = 0x5A;
    gpu.DispFIFOBuffer[7] = 0x1357;
    gpu.PaletteDirty = 0x5F;
    gpu.OAMDirty = 0x3;
    gpu.CaptureCnt = 0xA5A5A5A5u;
    gpu.CaptureEnable = true;
    gpu.ScreenSwap = true;
    gpu.ScreensEnabled = true;
    gpu.GPU2D_A.DispCnt = 0x1234;
    gpu.GPU2D_B.DispCnt = 0x5678;
}

void RunRecorder(FrameRecorder& recorder, GPU& gpu)
{
    recorder.BeginFrame(1u);
    recorder.CaptureSpriteLatchForLine(0u);
    for (u32 line = 0; line < ScreenHeight; ++line)
    {
        if (line != 0u)
            recorder.CaptureSpriteLatchForLine(line);
        recorder.CaptureMemoryForLine(line);
        recorder.CaptureLine(0u, gpu.GPU2D_A, line, gpu.ScreenSwap);
        recorder.CaptureLine(1u, gpu.GPU2D_B, line, gpu.ScreenSwap);
    }
    recorder.FinalizeMemory();
}

bool RunRecorderPurity()
{
    const auto nds = std::make_unique<NDS>();
    GPU& gpu = nds->GPU;
    SeedSharedState(gpu);
    const GPUStateSnapshot before = CaptureState(gpu);

    const auto recorder = std::make_unique<FrameRecorder>(gpu);
    RunRecorder(*recorder, gpu);
    const GPUStateSnapshot after = CaptureState(gpu);

    return Require(EqualState(before, after),
        "FrameRecorder changed shared GPU state");
}

bool RunMultiConsumerOrder()
{
    bool passed = true;
    {
        const auto nds = std::make_unique<NDS>();
        GPU& gpu = nds->GPU;
        SeedSharedState(gpu);
        const auto recorder = std::make_unique<FrameRecorder>(gpu);
        RunRecorder(*recorder, gpu);
        const auto textureDirty = gpu.VRAMDirty_Texture.DeriveState(
            gpu.VRAMMap_Texture, gpu);
        passed &= Require((textureDirty.Data[0] & 1u) != 0u,
            "3D texture consumer missed dirty VRAM after GPU2D observer");
    }
    {
        const auto nds = std::make_unique<NDS>();
        GPU& gpu = nds->GPU;
        SeedSharedState(gpu);
        const auto textureDirty = gpu.VRAMDirty_Texture.DeriveState(
            gpu.VRAMMap_Texture, gpu);
        passed &= Require((textureDirty.Data[0] & 1u) != 0u,
            "3D texture consumer did not observe seeded dirty VRAM");
        const auto recorder = std::make_unique<FrameRecorder>(gpu);
        RunRecorder(*recorder, gpu);
        passed &= Require(
            recorder->GetFrame().Engine[0].BGVRAM[0x10] == gpu.VRAM[0][0x10],
            "GPU2D observer missed VRAM after 3D consumer");
    }
    return passed;
}

u32 TimelineValue(const FrameInput& input, u32 line, u32 block)
{
    return input.TimelineIndex[
        static_cast<std::size_t>(line) * TimelineBlockCount + block];
}

u32 SpriteTimelineValue(const FrameInput& input, u32 line, u32 block)
{
    return input.SpriteTimelineIndex[
        static_cast<std::size_t>(line) * SpriteTimelineBlockCount + block];
}

bool RunRecorderTimeline()
{
    const auto nds = std::make_unique<NDS>();
    GPU& gpu = nds->GPU;
    SeedSharedState(gpu);

    const auto recorder = std::make_unique<FrameRecorder>(gpu);
    recorder->BeginFrame(1u);
    recorder->CaptureSpriteLatchForLine(0u);
    recorder->CaptureMemoryForLine(0u);
    recorder->CaptureLine(0u, gpu.GPU2D_A, 0u, gpu.ScreenSwap);
    recorder->CaptureLine(1u, gpu.GPU2D_B, 0u, gpu.ScreenSwap);

    for (u32 line = 1u; line < ScreenHeight; ++line)
    {
        if (line == 50u)
            gpu.VRAM[0][0x10] = 0xC1u;
        if (line == 96u)
            gpu.Palette[37u] = 0xD6u;
        if (line == 128u)
            gpu.OAM[513u] = 0xE7u;
        if (line == 64u)
            gpu.VRAMMap_LCDC = 1u << 2u;
        // FIFO is sampled for every line. Writing immediately before the
        // line latch models a line-by-line FIFO pattern without touching the
        // renderer's destructive dirty ownership.
        gpu.DispFIFOBuffer[7u] = static_cast<u16>(0x1000u + line);

        recorder->CaptureSpriteLatchForLine(line);
        recorder->CaptureMemoryForLine(line);
        recorder->CaptureLine(0u, gpu.GPU2D_A, line, gpu.ScreenSwap);
        recorder->CaptureLine(1u, gpu.GPU2D_B, line, gpu.ScreenSwap);
    }
    recorder->FinalizeMemory();

    const FrameInput& input = recorder->GetFrame();
    bool passed = Require(recorder->IsValid(),
        "a complete 192-line recorder run was not valid");
    const u32 vramBefore = TimelineValue(input, 49u, TimelineEngineBaseBlock);
    const u32 vramAfter = TimelineValue(input, 50u, TimelineEngineBaseBlock);
    passed &= Require(vramBefore == 0u && vramAfter != 0u,
        "mid-frame VRAM write did not begin at its emulated line");
    passed &= Require(
        input.TimelinePayload[(vramAfter - 1u) * DirtyBlockBytes + 0x10u] == 0xC1u,
        "mid-frame VRAM delta payload was not captured");

    const u32 paletteVersion = TimelineValue(
        input, 96u, TimelinePaletteBaseBlock);
    passed &= Require(
        TimelineValue(input, 95u, TimelinePaletteBaseBlock) == 0u
            && paletteVersion != 0u,
        "mid-frame palette write did not begin at its emulated line");
    passed &= Require(
        input.TimelinePayload[(paletteVersion - 1u) * DirtyBlockBytes + 37u] == 0xD6u,
        "mid-frame palette delta payload was not captured");

    const u32 oamBlock = TimelineOAMBaseBlock + 1u;
    const u32 oamVersion = TimelineValue(input, 128u, oamBlock);
    passed &= Require(
        TimelineValue(input, 127u, oamBlock) == 0u && oamVersion != 0u,
        "OAM timeline did not follow the one-line-ahead latch boundary");
    passed &= Require(
        input.TimelinePayload[(oamVersion - 1u) * DirtyBlockBytes + 1u] == 0xE7u,
        "OAM delta payload was not captured");
    const u32 spriteOAMBlock = 1u;
    const u32 spriteOAMVersion = SpriteTimelineValue(input, 128u, spriteOAMBlock);
    passed &= Require(
        SpriteTimelineValue(input, 127u, spriteOAMBlock) == 0u
            && spriteOAMVersion == oamVersion,
        "private OBJ/OAM timeline did not latch at the DrawSprites boundary");
    const u32 spriteOBJBlock = SpriteTimelineOAMBlocks;
    passed &= Require(
        SpriteTimelineValue(input, 49u, spriteOBJBlock) == 0u
            && SpriteTimelineValue(input, 50u, spriteOBJBlock) != 0u,
        "private OBJ VRAM timeline did not follow the one-line-ahead latch");

    passed &= Require(
        input.Lines[63u].LCDVRAMMap != input.Lines[64u].LCDVRAMMap
            && input.Lines[64u].LCDVRAMMap == (1u << 2u),
        "LCDC VRAM mapping was not captured per visible line");

    const u32 fifoVersion = TimelineValue(input, 190u, TimelineFIFOBaseBlock);
    const std::size_t fifoPayload =
        static_cast<std::size_t>(fifoVersion - 1u) * DirtyBlockBytes + 7u * sizeof(u16);
    u16 fifoValue = 0u;
    std::memcpy(&fifoValue, input.TimelinePayload.data() + fifoPayload, sizeof(fifoValue));
    passed &= Require(fifoVersion != 0u && fifoValue == 0x10BEu,
        "line-by-line display FIFO pattern was not captured");
    passed &= Require(input.TimelineDeltaCount >= 4u,
        "temporal recorder did not retain changed-block generations");
    return passed;
}

bool RunHighChurnTimeline()
{
    const auto nds = std::make_unique<NDS>();
    GPU& gpu = nds->GPU;
    SeedSharedState(gpu);

    // Exercise every mapped class with a DMA-like bitmap flip. The workload
    // emits tens of thousands of changed-block observations in one frame,
    // but it intentionally alternates between two immutable block contents.
    // A write-event counter would overflow here; content deduplication must
    // keep the dense line index lossless and valid.
    for (u32 bank = 0; bank < 9u; ++bank)
        std::memset(gpu.VRAM[bank], 0, gpu.VRAMMask[bank] + 1u);
    std::memset(gpu.Palette, 0, sizeof(gpu.Palette));
    std::memset(gpu.OAM, 0, sizeof(gpu.OAM));
    std::memset(gpu.DispFIFOBuffer, 0, sizeof(gpu.DispFIFOBuffer));
    for (u32 i = 0; i < 32u; ++i)
        gpu.VRAMMap_ABG[i] = 1u << 0u;
    for (u32 i = 0; i < 16u; ++i)
        gpu.VRAMMap_AOBJ[i] = 1u << 0u;
    for (u32 i = 0; i < 8u; ++i)
    {
        gpu.VRAMMap_BBG[i] = 1u << 0u;
        gpu.VRAMMap_BOBJ[i] = 1u << 0u;
    }
    for (u32 i = 0; i < 4u; ++i)
    {
        gpu.VRAMMap_ABGExtPal[i] = 1u << 0u;
        gpu.VRAMMap_BBGExtPal[i] = 1u << 0u;
    }
    gpu.VRAMMap_AOBJExtPal = 1u << 0u;
    gpu.VRAMMap_BOBJExtPal = 1u << 0u;

    const auto recorder = std::make_unique<FrameRecorder>(gpu);
    recorder->BeginFrame(1u);
    recorder->CaptureSpriteLatchForLine(0u);
    recorder->CaptureMemoryForLine(0u);
    recorder->CaptureLine(0u, gpu.GPU2D_A, 0u, gpu.ScreenSwap);
    recorder->CaptureLine(1u, gpu.GPU2D_B, 0u, gpu.ScreenSwap);
    for (u32 line = 1u; line < ScreenHeight; ++line)
    {
        if (line < 8u)
        {
            const u8 value = (line & 1u) != 0u ? 0x5Au : 0xA5u;
            for (u32 bank = 0; bank < 9u; ++bank)
                std::memset(gpu.VRAM[bank], value, gpu.VRAMMask[bank] + 1u);
        }
        recorder->CaptureSpriteLatchForLine(line);
        recorder->CaptureMemoryForLine(line);
        recorder->CaptureLine(0u, gpu.GPU2D_A, line, gpu.ScreenSwap);
        recorder->CaptureLine(1u, gpu.GPU2D_B, line, gpu.ScreenSwap);
    }
    recorder->FinalizeMemory();

    const FrameInput& input = recorder->GetFrame();
    bool passed = Require(recorder->IsValid(),
        "high-churn timeline was rejected despite content deduplication");
    passed &= Require(input.TimelineOverflow == 0u,
        "high-churn bitmap/DMA workload set TimelineOverflow");
    passed &= Require(input.TimelineDeltaCount < MaxMemoryDeltas,
        "content-deduplicated timeline consumed the entire payload budget");
    const u32 first = TimelineValue(input, 1u, TimelineEngineBaseBlock);
    const u32 second = TimelineValue(input, 2u, TimelineEngineBaseBlock);
    const u32 third = TimelineValue(input, 3u, TimelineEngineBaseBlock);
    passed &= Require(first != 0u && second != 0u && first != second
            && third == first,
        "repeated high-churn block contents did not reuse immutable versions");
    return passed;
}

} // namespace

namespace melonDS::Testing
{

int RunGPU2DNativeRecorderPurity()
{
    const bool passed = RunRecorderPurity() && RunMultiConsumerOrder()
        && RunRecorderTimeline() && RunHighChurnTimeline();
    std::fprintf(stdout, "%s: GPU2D native recorder purity\n",
        passed ? "PASS" : "FAIL");
    std::fflush(stdout);
    return passed ? 0 : 1;
}

} // namespace melonDS::Testing
