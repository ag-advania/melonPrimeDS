#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeSapphireFrameInput requires the Vulkan build gate"
#endif

#include "MelonPrimeDesktopSapphireFrameSidecar.h"
#include "SapphirePublished2DFrame.h"
#include "GPU3D_Vulkan.h"
#include "VulkanReference/FrameQueue.h"
#include "types.h"

namespace melonDS
{
class GPU;
class VulkanRenderer3D;
}

namespace MelonDSAndroid
{

struct CompletedSapphireFrameTuple
{
    u64 frameSerial = 0;
    u64 rendererGeneration = 0;
    int frontBuffer = -1;
    bool screenSwap = false;
    SapphirePublished2DFrame published2D{};
    melonDS::Vulkan3DFrameView frame3d{};
    bool valid = false;
};

CompletedSapphireFrameTuple BuildCompletedSapphireFrameTuple(
    const melonDS::GPU& gpu,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration);

struct SapphireFrameInput
{
    Frame* frame = nullptr;
    int frontBuffer = -1;
    bool preparedFrameScreenSwap = false;

    u64 emulatedFrameSerial = 0;
    u64 rendererGeneration = 0;

    const u32* packedTop = nullptr;
    const u32* packedBottom = nullptr;

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
    const CompletedSapphireFrameTuple& tuple,
    u64 activeRendererGeneration);

} // namespace MelonDSAndroid
