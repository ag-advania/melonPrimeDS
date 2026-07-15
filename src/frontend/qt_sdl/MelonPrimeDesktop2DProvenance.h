#pragma once

#include <array>
#include <cstring>
#include <cstdint>

#include "MelonPrimeDesktop2DBlackContract.h"
#include "MelonPrimeDesktopSapphireFrameSidecar.h"
#include "VulkanReference/VulkanOutput.h"

namespace MelonDSAndroid
{

inline u32 physicalScreenEngine(
    const DesktopSapphireFrameSidecar& sidecar,
    PhysicalScreen screen) noexcept
{
    return screen == PhysicalScreen::Top
        ? sidecar.physicalTopEngine
        : sidecar.physicalBottomEngine;
}

inline bool captureSourceMatchesTarget(
    const Capture3DSourceSnapshot& source,
    PhysicalScreen targetScreen,
    u32 targetEngine,
    u64 currentRendererGeneration) noexcept
{
    if (!source.valid)
        return false;
    if (source.physicalScreen != targetScreen)
        return false;
    if (source.engine != targetEngine)
        return false;
    if (source.rendererGeneration != currentRendererGeneration)
        return false;
    return true;
}

inline bool canCarryPreviousPhysicalScreen(
    const SoftPackedFrameSnapshot& previous,
    const SoftPackedFrameSnapshot& current,
    const DesktopSapphireFrameSidecar& previousSidecar,
    const DesktopSapphireFrameSidecar& currentSidecar,
    PhysicalScreen screen) noexcept
{
    if (!previous.valid || !current.valid)
        return false;
    if (previousSidecar.rendererGeneration != currentSidecar.rendererGeneration)
        return false;
    if (previousSidecar.emulatedFrameSerial + 1 != currentSidecar.emulatedFrameSerial)
        return false;
    if (previousSidecar.hardwareScreenSwap != currentSidecar.hardwareScreenSwap)
        return false;
    if (physicalScreenEngine(previousSidecar, screen) != physicalScreenEngine(currentSidecar, screen))
        return false;
    return true;
}

template<typename Plane0, typename Plane1, typename Control>
inline bool packedLineHasPresent2D(
    const Plane0& plane0,
    const Plane1& plane1,
    const Control& control,
    int line)
{
    if (line < 0 || line >= static_cast<int>(SoftPackedFrameSnapshot::kLineCount))
        return false;

    const size_t rowBase =
        static_cast<size_t>(line) * SoftPackedFrameSnapshot::kScreenWidth;
    for (int x = 0; x < static_cast<int>(SoftPackedFrameSnapshot::kScreenWidth); x++)
    {
        const size_t index = rowBase + static_cast<size_t>(x);
        if (packedPixelIsPresent2D(plane0[index])
            || packedPixelIsPresent2D(plane1[index])
            || packedControlMarksProtectedBlack2D(control[index]))
        {
            return true;
        }
    }
    return false;
}

inline void copyPackedLineTuple(
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane0,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetPlane1,
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& targetControl,
    std::array<u32, SoftPackedFrameSnapshot::kLineCount>& targetLineMeta,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& sourcePlane0,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& sourcePlane1,
    const std::array<u32, SoftPackedFrameSnapshot::kPixelCount>& sourceControl,
    const std::array<u32, SoftPackedFrameSnapshot::kLineCount>& sourceLineMeta,
    int line)
{
    if (line < 0 || line >= static_cast<int>(SoftPackedFrameSnapshot::kLineCount))
        return;

    const size_t rowBase =
        static_cast<size_t>(line) * SoftPackedFrameSnapshot::kScreenWidth;
    std::memcpy(
        targetPlane0.data() + rowBase,
        sourcePlane0.data() + rowBase,
        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
    std::memcpy(
        targetPlane1.data() + rowBase,
        sourcePlane1.data() + rowBase,
        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
    std::memcpy(
        targetControl.data() + rowBase,
        sourceControl.data() + rowBase,
        SoftPackedFrameSnapshot::kScreenWidth * sizeof(u32));
    targetLineMeta[static_cast<size_t>(line)] = sourceLineMeta[static_cast<size_t>(line)];
}

} // namespace MelonDSAndroid
