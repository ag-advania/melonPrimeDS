#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "UnitSync.h"

#include "GPU.h"
#include "GPU2D.h"
#include "SapphireGPU2DCore/SapphireGPU2DRenderer2D.h"

namespace melonDS::SapphireGPU2DCore::GPU2D
{

void SyncUnitFromGPU2D(Unit& unit, const melonDS::GPU2D& gpu2d, melonDS::GPU& gpu)
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

} // namespace melonDS::SapphireGPU2DCore::GPU2D

#endif
