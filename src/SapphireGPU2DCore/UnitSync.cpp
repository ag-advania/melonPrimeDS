#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "UnitSync.h"

#include "GPU.h"
#include "GPU2D.h"
#include "SapphireGPU2DCore/SapphireGPU2DRenderer2D.h"

#include <cstring>

namespace melonDS::SapphireGPU2DCore::GPU2D
{

namespace
{

u32 EffectiveDispCnt(const melonDS::GPU2D& gpu2d)
{
    u32 dispCnt = gpu2d.DispCnt;
    dispCnt = (dispCnt & ~0x1F00u) | ((static_cast<u32>(gpu2d.LayerEnable & 0x1F) << 8));
    if (gpu2d.OBJEnable)
        dispCnt |= 0x1000u;
    else
        dispCnt &= ~0x1000u;

    if (gpu2d.ForcedBlank)
        dispCnt |= (1u << 7);
    else
        dispCnt &= ~(1u << 7);

    return dispCnt;
}

} // namespace

void SyncUnitFromGPU2D(Unit& unit, const melonDS::GPU2D& gpu2d, melonDS::GPU& gpu)
{
    unit.Enabled = gpu2d.Enabled;
    unit.DispCnt = EffectiveDispCnt(gpu2d);

    std::memcpy(unit.BGCnt, gpu2d.BGCnt, sizeof(unit.BGCnt));
    std::memcpy(unit.BGXPos, gpu2d.BGXPos, sizeof(unit.BGXPos));
    std::memcpy(unit.BGYPos, gpu2d.BGYPos, sizeof(unit.BGYPos));
    std::memcpy(unit.BGXRef, gpu2d.BGXRef, sizeof(unit.BGXRef));
    std::memcpy(unit.BGYRef, gpu2d.BGYRef, sizeof(unit.BGYRef));
    std::memcpy(unit.BGXRefInternal, gpu2d.BGXRefInternal, sizeof(unit.BGXRefInternal));
    std::memcpy(unit.BGYRefInternal, gpu2d.BGYRefInternal, sizeof(unit.BGYRefInternal));
    std::memcpy(unit.BGRotA, gpu2d.BGRotA, sizeof(unit.BGRotA));
    std::memcpy(unit.BGRotB, gpu2d.BGRotB, sizeof(unit.BGRotB));
    std::memcpy(unit.BGRotC, gpu2d.BGRotC, sizeof(unit.BGRotC));
    std::memcpy(unit.BGRotD, gpu2d.BGRotD, sizeof(unit.BGRotD));

    std::memcpy(unit.Win0Coords, gpu2d.Win0Coords, sizeof(unit.Win0Coords));
    std::memcpy(unit.Win1Coords, gpu2d.Win1Coords, sizeof(unit.Win1Coords));
    std::memcpy(unit.WinCnt, gpu2d.WinCnt, sizeof(unit.WinCnt));
    unit.Win0Active = gpu2d.Win0Active;
    unit.Win1Active = gpu2d.Win1Active;

    unit.BGMosaicSize[0] = gpu2d.BGMosaicSize[0];
    unit.BGMosaicSize[1] = gpu2d.BGMosaicSize[1];
    unit.OBJMosaicSize[0] = gpu2d.OBJMosaicSize[0];
    unit.OBJMosaicSize[1] = gpu2d.OBJMosaicSize[1];
    // Mosaic line counters are owned by Sapphire Unit::UpdateMosaicCounters().

    unit.BlendCnt = gpu2d.BlendCnt;
    unit.BlendAlpha = gpu2d.BlendAlpha;
    unit.EVA = gpu2d.EVA;
    unit.EVB = gpu2d.EVB;
    unit.EVY = gpu2d.EVY;

    if (unit.Num == 0)
    {
        unit.MasterBrightness = gpu.MasterBrightnessA;
        unit.CaptureCnt = gpu.CaptureCnt;
        unit.CaptureLatch = gpu.CaptureEnable;

        std::memcpy(unit.DispFIFO, gpu.DispFIFO, sizeof(unit.DispFIFO));
        unit.DispFIFOReadPtr = gpu.DispFIFOReadPtr;
        unit.DispFIFOWritePtr = gpu.DispFIFOWritePtr;
        std::memcpy(unit.DispFIFOBuffer, gpu.DispFIFOBuffer, sizeof(unit.DispFIFOBuffer));
    }
    else
    {
        unit.MasterBrightness = gpu.MasterBrightnessB;
    }
}

} // namespace melonDS::SapphireGPU2DCore::GPU2D

#endif
