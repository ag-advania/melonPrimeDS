#include "MelonPrimeSapphireFrameInput.h"

#include "GPU.h"
#include "GPU3D_Vulkan.h"

namespace MelonDSAndroid
{

CompletedSapphireFrameTuple BuildCompletedSapphireFrameTuple(
    const melonDS::GPU& gpu,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration)
{
    CompletedSapphireFrameTuple tuple{};
    tuple.published2D = gpu.GetPublished2DFrame();
    tuple.frame3d = frame3d;
    if (!tuple.published2D.valid || !frame3d.Valid)
        return tuple;
    if (frame3d.FrameSerial == 0 || frame3d.Generation == 0 || activeRendererGeneration == 0)
        return tuple;
    if (tuple.published2D.emulatedFrameSerial != frame3d.FrameSerial)
        return tuple;
    if (tuple.published2D.rendererGeneration != 0
        && tuple.published2D.rendererGeneration != frame3d.Generation)
    {
        return tuple;
    }
    if (frame3d.Generation != activeRendererGeneration)
        return tuple;

    tuple.frameSerial = frame3d.FrameSerial;
    tuple.rendererGeneration = frame3d.Generation;
    tuple.frontBuffer = tuple.published2D.frontBuffer;
    tuple.screenSwap = tuple.published2D.renderScreenSwapAt3D;
    tuple.valid = tuple.frontBuffer >= 0
        && tuple.frontBuffer <= 1
        && tuple.published2D.top.packed != nullptr
        && tuple.published2D.bottom.packed != nullptr;
    return tuple;
}

DesktopSapphireFrameBuildResult BuildDesktopSapphireFrameInput(
    Frame* frame,
    const CompletedSapphireFrameTuple& tuple,
    u64 activeRendererGeneration)
{
    DesktopSapphireFrameBuildResult result{};
    if (frame == nullptr || !tuple.valid)
    {
        result.rejected = true;
        result.rejectReason = "invalidCompletedTuple";
        return result;
    }

    const SapphirePublished2DFrame& published = tuple.published2D;
    const melonDS::Vulkan3DFrameView& frame3d = tuple.frame3d;

    if (!frame3d.Valid)
    {
        result.rejected = true;
        result.rejectReason = "invalidFrame3dView";
        return result;
    }
    if (frame3d.FrameSerial == 0)
    {
        result.rejected = true;
        result.rejectReason = "zeroFrameSerial";
        return result;
    }
    if (frame3d.Generation == 0 || activeRendererGeneration == 0)
    {
        result.rejected = true;
        result.rejectReason = "zeroGeneration";
        return result;
    }
    if (published.frontBuffer < 0 || published.frontBuffer > 1)
    {
        result.rejected = true;
        result.rejectReason = "invalidFrontBuffer";
        return result;
    }
    if (tuple.frameSerial != frame3d.FrameSerial)
    {
        result.rejected = true;
        result.rejectReason = "emulatedSerialMismatch";
        return result;
    }
    if (frame->rendererGeneration != frame3d.Generation)
    {
        result.rejected = true;
        result.rejectReason = "frameRendererGenerationMismatch";
        return result;
    }
    if (frame3d.Generation != activeRendererGeneration)
    {
        result.rejected = true;
        result.rejectReason = "activeGenerationMismatch";
        return result;
    }
    if (published.top.packed == nullptr || published.bottom.packed == nullptr)
    {
        result.rejected = true;
        result.rejectReason = "missingPackedPointers";
        return result;
    }

    result.input.frame = frame;
    result.input.frontBuffer = tuple.frontBuffer;
    result.input.preparedFrameScreenSwap = tuple.screenSwap;
    result.input.emulatedFrameSerial = tuple.frameSerial;
    result.input.rendererGeneration = frame3d.Generation;
    result.input.packedTop = published.top.packed;
    result.input.packedBottom = published.bottom.packed;
    result.input.valid = true;

    result.sidecar.emulatedFrameSerial = tuple.frameSerial;
    result.sidecar.rendererGeneration = frame3d.Generation;
    result.sidecar.publishedFrontBuffer = tuple.frontBuffer;
    result.sidecar.liveFrontBuffer = tuple.frontBuffer;
    result.sidecar.publishedScreenSwap = tuple.screenSwap;
    result.sidecar.liveScreenSwap = tuple.screenSwap;
    result.sidecar.packedTopIdentity = static_cast<const void*>(published.top.packed);
    result.sidecar.packedBottomIdentity = static_cast<const void*>(published.bottom.packed);
    return result;
}

} // namespace MelonDSAndroid
