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
    recorder.CaptureLine(0u, gpu.GPU2D_A, 0u, gpu.ScreenSwap);
    recorder.CaptureLine(1u, gpu.GPU2D_B, 0u, gpu.ScreenSwap);
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

} // namespace

namespace melonDS::Testing
{

int RunGPU2DNativeRecorderPurity()
{
    const bool passed = RunRecorderPurity() && RunMultiConsumerOrder();
    std::fprintf(stdout, "%s: GPU2D native recorder purity\n",
        passed ? "PASS" : "FAIL");
    std::fflush(stdout);
    return passed ? 0 : 1;
}

} // namespace melonDS::Testing
