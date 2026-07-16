#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireFrameInput requires the Vulkan build gate"
#endif

#include "MelonPrimeDesktopSapphireFrameSidecar.h"
#include "VulkanReference/FrameQueue.h"
#include "types.h"

namespace melonDS
{
class GPU;
class VulkanRenderer3D;
struct Vulkan3DFrameView;
}

struct SapphirePublished2DFrame;

namespace MelonDSAndroid
{

struct SapphireFrameInput
{
    Frame* frame = nullptr;
    int frontBuffer = -1;
    bool preparedFrameScreenSwap = false;

    u64 emulatedFrameSerial = 0;
    u64 rendererGeneration = 0;
    bool valid = false;
};

struct DesktopSapphireFrameBuildResult
{
    SapphireFrameInput input{};
    DesktopSapphireFrameSidecar sidecar{};
    bool rejected = false;
    const char* rejectReason = nullptr;
};

DesktopSapphireFrameBuildResult BuildDesktopSapphireFrameInput(
    Frame* frame,
    const melonDS::GPU& gpu,
    const SapphirePublished2DFrame& published,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration,
    int expectedFrontBuffer,
    bool expectedScreenSwap);

} // namespace MelonDSAndroid
