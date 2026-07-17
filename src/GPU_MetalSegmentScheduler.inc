// Included after GPU_MetalCaptureMethods.inc / CaptureExperiment.
// MELONPRIME_METAL_CAPTURE_SEGMENT_SCHEDULER_V1
//
// Minimal PR-4 segment loop: for each merged capture/2D boundary segment,
// render Engine A, Engine B, then encode display capture for that line range.
// Same-queue ordering + Capture→Snapshot blit per segment provides same-frame
// feedback at segment granularity. CaptureCnt Full-GPU exclusion stays until PR-5.

namespace
{

bool MetalCaptureLineConfigEqualForSegment(
    const MetalCaptureLineConfigCpu& a,
    const MetalCaptureLineConfigCpu& b)
{
    // SrcBNativeY changes every line by design; do not force a split.
    return a.Enabled == b.Enabled &&
        a.DispCntA == b.DispCntA &&
        a.CaptureCnt == b.CaptureCnt &&
        a.SrcAXOffset == b.SrcAXOffset &&
        a.SrcBCaptureLayer == b.SrcBCaptureLayer &&
        a.SrcBCaptureSize == b.SrcBCaptureSize;
}

} // namespace

bool MetalRenderer::BuildFullGpuCaptureSegments(
    std::array<MetalScanlineSegment, 192>& segments,
    int& count) const
{
    count = 0;
    segments = {};

    std::array<uint8_t, 192> boundary {};
    boundary[0] = 1;

    if (CaptureState && CaptureState->Ready)
    {
        const auto* lines = static_cast<const MetalCaptureLineConfigCpu*>(
            [CaptureState->LineConfigBuffer contents]);
        for (int line = 1; line < 192; line++)
        {
            if (!MetalCaptureLineConfigEqualForSegment(lines[line - 1], lines[line]))
                boundary[static_cast<size_t>(line)] = 1;
        }
    }

    if (Metal2D_A)
    {
        const auto mask = Metal2D_A->BuildSegmentBoundaryMask();
        for (int line = 0; line < 192; line++)
            boundary[static_cast<size_t>(line)] |= mask[static_cast<size_t>(line)];
    }
    if (Metal2D_B)
    {
        const auto mask = Metal2D_B->BuildSegmentBoundaryMask();
        for (int line = 0; line < 192; line++)
            boundary[static_cast<size_t>(line)] |= mask[static_cast<size_t>(line)];
    }

    const auto* lines = (CaptureState && CaptureState->Ready)
        ? static_cast<const MetalCaptureLineConfigCpu*>(
              [CaptureState->LineConfigBuffer contents])
        : nullptr;

    int start = 0;
    for (int line = 1; line < 192; line++)
    {
        if (!boundary[static_cast<size_t>(line)])
            continue;
        MetalScanlineSegment seg;
        seg.StartLine = start;
        seg.EndLine = line;
        seg.CaptureEnabled =
            lines && lines[start].Enabled != 0;
        segments[static_cast<size_t>(count++)] = seg;
        start = line;
    }

    MetalScanlineSegment last;
    last.StartLine = start;
    last.EndLine = 192;
    last.CaptureEnabled =
        lines && lines[start].Enabled != 0;
    segments[static_cast<size_t>(count++)] = last;
    return count > 0;
}

bool MetalRenderer::RenderMetalFullGpuFrameSegmented(
    const char** rejectionStage)
{
    void* high3D = Metal3DColorTarget(Rend3D.get());
    void* capture128 = GetMetalCapture128Texture();
    void* capture256 = GetMetalCapture256Texture();

    if (!Metal2D_A || !Metal2D_B)
    {
        *rejectionStage = "2d-renderers-missing";
        return false;
    }
    if (!capture128 || !capture256)
    {
        *rejectionStage = "capture-textures-missing";
        return false;
    }
    if (!Metal2D_A->SegmentedFrameComplete())
    {
        *rejectionStage = "segment-snapshot-A-incomplete";
        return false;
    }
    if (!Metal2D_B->SegmentedFrameComplete())
    {
        *rejectionStage = "segment-snapshot-B-incomplete";
        return false;
    }

    std::array<MetalScanlineSegment, 192> segments {};
    int segmentCount = 0;
    if (!BuildFullGpuCaptureSegments(segments, segmentCount) ||
        segmentCount <= 0)
    {
        *rejectionStage = "segment-build-failed";
        return false;
    }

    for (int i = 0; i < segmentCount; i++)
    {
        const MetalScanlineSegment& seg =
            segments[static_cast<size_t>(i)];
        const bool allowCaptureTextures = !seg.CaptureEnabled;

        if (!Metal2D_A->RenderSegmentedGpuFrame(
                high3D,
                capture128,
                capture256,
                allowCaptureTextures,
                seg.StartLine,
                seg.EndLine))
        {
            *rejectionStage = "render-segmented-A";
            return false;
        }
        if (!Metal2D_B->RenderSegmentedGpuFrame(
                high3D,
                capture128,
                capture256,
                allowCaptureTextures,
                seg.StartLine,
                seg.EndLine))
        {
            *rejectionStage = "render-segmented-B";
            return false;
        }
        if (!EncodeMetalDisplayCapture(
                Metal2D_A->GetOutputTexture(),
                high3D,
                seg.StartLine,
                seg.EndLine))
        {
            *rejectionStage = "encode-display-capture";
            return false;
        }
    }
    return true;
}
