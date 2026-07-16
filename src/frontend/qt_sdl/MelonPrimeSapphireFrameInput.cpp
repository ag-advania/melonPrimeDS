#include "MelonPrimeSapphireFrameInput.h"

#include "GPU.h"
#include "GPU3D_Vulkan.h"
#include "SapphireGPU2DSoftAccess.h"
#include "SapphirePublished2DFrame.h"

namespace MelonDSAndroid
{

namespace
{

bool PointerMatchesLivePacked(
    const melonDS::u32* published,
    const melonDS::u32* live)
{
    return published != nullptr && live != nullptr && published == live;
}

bool PointerMatchesLiveStructured(
    const melonDS::u32* published,
    const melonDS::u32* live)
{
    if (published == nullptr && live == nullptr)
        return true;
    return published != nullptr && live != nullptr && published == live;
}

} // namespace

DesktopSapphireFrameBuildResult BuildDesktopSapphireFrameInput(
    Frame* frame,
    const melonDS::GPU& gpu,
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

    const int frontBuffer = published.frontBuffer;
    if (!PointerMatchesLivePacked(published.top.packed, gpu.Framebuffer[frontBuffer][0])
        || !PointerMatchesLivePacked(published.bottom.packed, gpu.Framebuffer[frontBuffer][1]))
    {
        result.rejected = true;
        result.rejectReason = "publishedLivePackedPointerMismatch";
        return result;
    }

    const auto* renderer2D = gpu.TryGetSapphireRenderer2D();
    if (published.top.structuredPlane0 != nullptr
        || published.top.structuredPlane1 != nullptr
        || published.top.structuredControl != nullptr
        || published.bottom.structuredPlane0 != nullptr
        || published.bottom.structuredPlane1 != nullptr
        || published.bottom.structuredControl != nullptr)
    {
        if (renderer2D == nullptr)
        {
            result.rejected = true;
            result.rejectReason = "structuredPointersWithoutRenderer2D";
            return result;
        }

        const melonDS::u32* liveTopPlane0 = renderer2D->GetStructuredVulkan2DPlane(true, 0);
        const melonDS::u32* liveTopPlane1 = renderer2D->GetStructuredVulkan2DPlane(true, 1);
        const melonDS::u32* liveTopControl = renderer2D->GetStructuredVulkan2DPlane(true, 2);
        const melonDS::u32* liveBottomPlane0 = renderer2D->GetStructuredVulkan2DPlane(false, 0);
        const melonDS::u32* liveBottomPlane1 = renderer2D->GetStructuredVulkan2DPlane(false, 1);
        const melonDS::u32* liveBottomControl = renderer2D->GetStructuredVulkan2DPlane(false, 2);

        if (!PointerMatchesLiveStructured(published.top.structuredPlane0, liveTopPlane0)
            || !PointerMatchesLiveStructured(published.top.structuredPlane1, liveTopPlane1)
            || !PointerMatchesLiveStructured(published.top.structuredControl, liveTopControl)
            || !PointerMatchesLiveStructured(published.bottom.structuredPlane0, liveBottomPlane0)
            || !PointerMatchesLiveStructured(published.bottom.structuredPlane1, liveBottomPlane1)
            || !PointerMatchesLiveStructured(published.bottom.structuredControl, liveBottomControl))
        {
            result.rejected = true;
            result.rejectReason = "publishedLiveStructuredPointerMismatch";
            return result;
        }
    }

    result.input.frame = frame;
    result.input.frontBuffer = published.frontBuffer;
    result.input.preparedFrameScreenSwap = published.renderScreenSwapAt3D;
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
