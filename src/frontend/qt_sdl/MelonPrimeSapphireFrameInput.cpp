#include "MelonPrimeSapphireFrameInput.h"

#include "GPU3D_Vulkan.h"
#include "SapphirePublished2DFrame.h"

namespace MelonDSAndroid
{

DesktopSapphireFrameBuildResult BuildDesktopSapphireFrameInput(
    Frame* frame,
    const SapphirePublished2DFrame& published,
    const melonDS::Vulkan3DFrameView& frame3d,
    u64 activeRendererGeneration,
    int expectedFrontBuffer,
    bool expectedScreenSwap)
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
    if (expectedFrontBuffer < 0 || expectedFrontBuffer > 1)
    {
        result.rejected = true;
        result.rejectReason = "invalidLiveFrontBuffer";
        return result;
    }
    if (published.frontBuffer != expectedFrontBuffer)
    {
        result.rejected = true;
        result.rejectReason = "publishedLiveFrontBufferMismatch";
        return result;
    }
    if (published.renderScreenSwapAt3D != expectedScreenSwap)
    {
        result.rejected = true;
        result.rejectReason = "publishedLiveScreenSwapMismatch";
        return result;
    }
    if (published.emulatedFrameSerial != frame3d.FrameSerial)
    {
        result.rejected = true;
        result.rejectReason = "emulatedSerialMismatch";
        return result;
    }
    if (published.rendererGeneration != frame3d.Generation)
    {
        result.rejected = true;
        result.rejectReason = "publishedGenerationMismatch";
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
    result.input.rendererGeneration = frame3d.Generation;
    result.input.valid = true;

    result.sidecar.emulatedFrameSerial = published.emulatedFrameSerial;
    result.sidecar.rendererGeneration = frame3d.Generation;
    result.sidecar.publishedFrontBuffer = published.frontBuffer;
    result.sidecar.liveFrontBuffer = expectedFrontBuffer;
    result.sidecar.publishedScreenSwap = published.renderScreenSwapAt3D;
    result.sidecar.liveScreenSwap = expectedScreenSwap;
    result.sidecar.packedTopIdentity = static_cast<const void*>(published.top.packed);
    result.sidecar.packedBottomIdentity = static_cast<const void*>(published.bottom.packed);
    return result;
}

} // namespace MelonDSAndroid
