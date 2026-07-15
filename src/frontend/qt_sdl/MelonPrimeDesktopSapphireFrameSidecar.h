#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeDesktopSapphireFrameSidecar requires the Vulkan build gate"
#endif

#include "types.h"

namespace MelonDSAndroid
{

using melonDS::u64;

struct DesktopSapphireFrameSidecar
{
    u64 emulatedFrameSerial = 0;
    u64 rendererGeneration = 0;

    int publishedFrontBuffer = -1;
    int liveFrontBuffer = -1;

    bool publishedScreenSwap = false;
    bool liveScreenSwap = false;

    const void* packedTopIdentity = nullptr;
    const void* packedBottomIdentity = nullptr;

    void clear() noexcept
    {
        emulatedFrameSerial = 0;
        rendererGeneration = 0;
        publishedFrontBuffer = -1;
        liveFrontBuffer = -1;
        publishedScreenSwap = false;
        liveScreenSwap = false;
        packedTopIdentity = nullptr;
        packedBottomIdentity = nullptr;
    }
};

} // namespace MelonDSAndroid
