#include "MelonPrimeSapphireFrameInput.h"

#include "GPU3D_Vulkan.h"
#include "SapphirePublished2DFrame.h"

namespace MelonDSAndroid
{

DesktopSapphireFrameBuildResult BuildDesktopSapphireFrameInput(
    Frame* frame,
    const SapphirePublished2DFrame& published,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration)
{
    DesktopSapphireFrameBuildResult result{};
    if (frame == nullptr || !published.valid)
    {
        result.rejected = true;
        result.rejectReason = "invalidPublishedFrame";
        return result;
    }
    if (!frame3d.Valid)
    {
        result.rejected = true;
        result.rejectReason = "invalidFrame3dView";
        return result;
    }
    if (published.frontBuffer < 0 || published.frontBuffer > 1)
    {
        result.rejected = true;
        result.rejectReason = "invalidFrontBuffer";
        return result;
    }
    if (published.emulatedFrameSerial != 0
        && frame3d.FrameSerial != 0
        && published.emulatedFrameSerial != frame3d.FrameSerial)
    {
        result.rejected = true;
        result.rejectReason = "emulatedSerialMismatch";
        return result;
    }
    if (published.rendererGeneration != 0
        && frame3d.Generation != 0
        && published.rendererGeneration != frame3d.Generation)
    {
        result.rejected = true;
        result.rejectReason = "publishedGenerationMismatch";
        return result;
    }
    if (frame->rendererGeneration != 0
        && frame3d.Generation != 0
        && frame->rendererGeneration != frame3d.Generation)
    {
        result.rejected = true;
        result.rejectReason = "frameRendererGenerationMismatch";
        return result;
    }
    if (activeRendererGeneration != 0
        && frame3d.Generation != activeRendererGeneration)
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
    result.input.frontBuffer = published.frontBuffer;
    result.input.preparedFrameScreenSwap = published.renderScreenSwapAt3D;
    result.input.packedTop = published.top.packed;
    result.input.packedBottom = published.bottom.packed;
    result.input.structuredTopPlane0 = published.top.structuredPlane0;
    result.input.structuredTopPlane1 = published.top.structuredPlane1;
    result.input.structuredTopControl = published.top.structuredControl;
    result.input.structuredBottomPlane0 = published.bottom.structuredPlane0;
    result.input.structuredBottomPlane1 = published.bottom.structuredPlane1;
    result.input.structuredBottomControl = published.bottom.structuredControl;
    result.input.emulatedFrameSerial = published.emulatedFrameSerial;
    result.input.rendererGeneration =
        frame3d.Generation != 0
            ? frame3d.Generation
            : published.rendererGeneration;
    result.input.valid = true;

    result.sidecar.emulatedFrameSerial = published.emulatedFrameSerial;
    result.sidecar.rendererGeneration = result.input.rendererGeneration;
    result.sidecar.hardwareScreenSwap = published.hardwareScreenSwap;
    result.sidecar.physicalTopEngine = published.top.engine;
    result.sidecar.physicalBottomEngine = published.bottom.engine;
    return result;
}

} // namespace MelonDSAndroid
