#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireFrameInput requires the Vulkan build gate"
#endif

#include "MelonPrimeDesktopSapphireFrameSidecar.h"
#include "VulkanReference/FrameQueue.h"
#include "types.h"

namespace melonDS
{
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

    const u32* packedTop = nullptr;
    const u32* packedBottom = nullptr;

    const u32* structuredTopPlane0 = nullptr;
    const u32* structuredTopPlane1 = nullptr;
    const u32* structuredTopControl = nullptr;
    const u32* structuredBottomPlane0 = nullptr;
    const u32* structuredBottomPlane1 = nullptr;
    const u32* structuredBottomControl = nullptr;

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
    const SapphirePublished2DFrame& published,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration,
    int expectedFrontBuffer,
    bool expectedScreenSwap);

} // namespace MelonDSAndroid
