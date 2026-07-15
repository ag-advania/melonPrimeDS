#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "SapphirePublished2DFrame requires the Vulkan build gate"
#endif

#include "types.h"

enum class SapphirePhysicalScreen : melonDS::u8
{
    Top = 0,
    Bottom = 1,
};

struct SapphirePublished2DScreen
{
    const melonDS::u32* packed = nullptr;
    const melonDS::u32* structuredPlane0 = nullptr;
    const melonDS::u32* structuredPlane1 = nullptr;
    const melonDS::u32* structuredControl = nullptr;

    melonDS::u32 engine = 0;
    SapphirePhysicalScreen physicalScreen = SapphirePhysicalScreen::Top;
};

struct SapphirePhysical2DScreenView
{
    const melonDS::u32* packed = nullptr;
    const melonDS::u32* plane0 = nullptr;
    const melonDS::u32* plane1 = nullptr;
    const melonDS::u32* control = nullptr;
    melonDS::u32 engine = 0;
};

struct SapphirePublished2DFrame
{
    SapphirePublished2DScreen top;
    SapphirePublished2DScreen bottom;

    int frontBuffer = 0;
    bool hardwareScreenSwap = false;
    bool renderScreenSwapAt3D = false;

    melonDS::u64 emulatedFrameSerial = 0;
    melonDS::u64 publicationGeneration = 0;
    melonDS::u64 rendererGeneration = 0;
    melonDS::u64 frameSerial = 0;
    bool valid = false;
};
