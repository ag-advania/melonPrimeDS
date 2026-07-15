#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "UnitSync.h"

#include <cstring>

#include "GPU.h"
#include "GPU2D.h"
#include "SapphireGPU2DCore/SapphireGPU2DRenderer2D.h"

namespace melonDS::SapphireGPU2DCore::GPU2D
{

void SyncExternalGpuState(Unit& unit, const melonDS::GPU2D& gpu2d, melonDS::GPU& gpu)
{
    unit.Enabled = gpu2d.Enabled;

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

void SeedCompleteUnitFromNative(Unit& unit, const melonDS::GPU2D& gpu2d, melonDS::GPU& gpu)
{
    unit.Enabled = gpu2d.Enabled;
    unit.DispCnt = gpu2d.DispCnt;
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
    std::memcpy(unit.BGMosaicSize, gpu2d.BGMosaicSize, sizeof(unit.BGMosaicSize));
    std::memcpy(unit.OBJMosaicSize, gpu2d.OBJMosaicSize, sizeof(unit.OBJMosaicSize));
    unit.BGMosaicY = gpu2d.BGMosaicY;
    unit.BGMosaicYMax = gpu2d.BGMosaicYMax;
    unit.OBJMosaicY = gpu2d.OBJMosaicY;
    unit.OBJMosaicYCount = static_cast<u8>(gpu2d.OBJMosaicLine);
    unit.BlendCnt = gpu2d.BlendCnt;
    unit.BlendAlpha = gpu2d.BlendAlpha;
    unit.EVA = gpu2d.EVA;
    unit.EVB = gpu2d.EVB;
    unit.EVY = gpu2d.EVY;

    SyncExternalGpuState(unit, gpu2d, gpu);
}

} // namespace melonDS::SapphireGPU2DCore::GPU2D

#endif
