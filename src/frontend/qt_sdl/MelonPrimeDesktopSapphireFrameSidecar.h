#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeDesktopSapphireFrameSidecar requires the Vulkan build gate"
#endif

#include <array>
#include <cstdint>

#include "VulkanReference/VulkanStructuredControlAbi.h"
#include "types.h"

namespace MelonDSAndroid
{

enum class PhysicalScreen : u8
{
    Top,
    Bottom,
};

struct Capture3DSourceSnapshot
{
    static constexpr size_t kPixelCount =
        melonDS::VulkanStructuredControlAbi::NativeScreenWidth
        * melonDS::VulkanStructuredControlAbi::NativeScreenHeight;

    std::array<u32, kPixelCount> pixels{};

    bool valid = false;
    PhysicalScreen physicalScreen = PhysicalScreen::Top;
    u32 engine = UINT32_MAX;
    u64 frameSerial = 0;
    u64 rendererGeneration = 0;
    bool hardwareScreenSwap = false;
    bool renderScreenSwapAt3D = false;
    u32 captureMode = 0;
    u32 sourceA = 0;
    u32 sourceB = 0;

    void clear() noexcept
    {
        pixels.fill(0);
        valid = false;
        physicalScreen = PhysicalScreen::Top;
        engine = UINT32_MAX;
        frameSerial = 0;
        rendererGeneration = 0;
        hardwareScreenSwap = false;
        renderScreenSwapAt3D = false;
        captureMode = 0;
        sourceA = 0;
        sourceB = 0;
    }
};

struct DesktopSapphireFrameSidecar
{
    u64 emulatedFrameSerial = 0;
    u64 rendererGeneration = 0;
    bool hardwareScreenSwap = false;
    u32 physicalTopEngine = UINT32_MAX;
    u32 physicalBottomEngine = UINT32_MAX;
    Capture3DSourceSnapshot topCapture3dSource{};
    Capture3DSourceSnapshot bottomCapture3dSource{};

    void clear() noexcept
    {
        emulatedFrameSerial = 0;
        rendererGeneration = 0;
        hardwareScreenSwap = false;
        physicalTopEngine = UINT32_MAX;
        physicalBottomEngine = UINT32_MAX;
        topCapture3dSource.clear();
        bottomCapture3dSource.clear();
    }
};

} // namespace MelonDSAndroid
