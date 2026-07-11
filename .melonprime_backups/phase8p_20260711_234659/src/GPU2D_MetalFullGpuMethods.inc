// Included near the end of GPU2D_Metal.mm.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_HUD_PARITY_V1
// MELONPRIME_METAL_2D_SCANLINE_SNAPSHOT_V1
// MELONPRIME_METAL_2D_SEGMENTED_SHADOW_RENDER_V1
// MELONPRIME_METAL_2D_DIRECT_SEGMENTED_CUTOVER_V2
// MELONPRIME_METAL_2D_SEGMENTED_SUBMIT_OPT_V1
// MELONPRIME_METAL_2D_HOT_PATH_CLEANUP_V1
// MELONPRIME_METAL_2D_PERSISTENT_UPLOAD_RING_V1
// MELONPRIME_METAL_2D_DIRECT_SNAPSHOT_RING_V1

bool MetalRenderer2D::BuildFullGpuPipelines() noexcept
{
    if (!State || !State->Device)
        return false;
    Metal2DState& state = *State;
    if (state.FullGpuLibrary && state.SpritePipeline &&
        state.SpriteMosaicFlagsPipeline && state.SpriteWindowPipeline &&
        state.CompositorPipeline && state.SpriteDepthState &&
        state.RepeatSampler && state.DummyColorTexture)
    {
        return true;
    }

    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetal2DFullGpuShaderSource];
    state.FullGpuLibrary = [state.Device newLibraryWithSource:source options:nil error:&error];
    if (!state.FullGpuLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal full-gpu 2d: shader compile failed: %s\n", message);
        return false;
    }

    id<MTLFunction> spriteVS = [state.FullGpuLibrary newFunctionWithName:@"mp2d_sprite_vs"];
    id<MTLFunction> spriteFS = [state.FullGpuLibrary newFunctionWithName:@"mp2d_sprite_fs"];
    id<MTLFunction> compositeVS = [state.FullGpuLibrary newFunctionWithName:@"mp2d_composite_vs"];
    id<MTLFunction> compositeFS = [state.FullGpuLibrary newFunctionWithName:@"mp2d_composite_fs"];
    if (!spriteVS || !spriteFS || !compositeVS || !compositeFS)
        return false;

    auto buildSpritePipeline = [&](NSString* label,
                                   MTLColorWriteMask colorMask,
                                   MTLColorWriteMask flagMask)
        -> id<MTLRenderPipelineState> {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.label = label;
        desc.vertexFunction = spriteVS;
        desc.fragmentFunction = spriteFS;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.colorAttachments[0].writeMask = colorMask;
        desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.colorAttachments[1].writeMask = flagMask;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline =
            [state.Device newRenderPipelineStateWithDescriptor:desc error:&pipelineError];
        if (!pipeline)
        {
            const char* message = pipelineError
                ? [[pipelineError localizedDescription] UTF8String]
                : "unknown error";
            std::fprintf(stderr,
                "[MelonPrime] metal full-gpu 2d: pipeline %s failed: %s\n",
                [label UTF8String], message);
        }
        return pipeline;
    };

    state.SpriteMosaicFlagsPipeline = buildSpritePipeline(
        @"MelonPrime Metal 2D Sprite Mosaic Flags",
        MTLColorWriteMaskNone,
        MTLColorWriteMaskGreen | MTLColorWriteMaskAlpha);
    state.SpriteWindowPipeline = buildSpritePipeline(
        @"MelonPrime Metal 2D Sprite Window",
        MTLColorWriteMaskNone,
        MTLColorWriteMaskBlue);
    state.SpritePipeline = buildSpritePipeline(
        @"MelonPrime Metal 2D Sprite",
        MTLColorWriteMaskAll,
        MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskAlpha);

    MTLRenderPipelineDescriptor* compositorDesc = [[MTLRenderPipelineDescriptor alloc] init];
    compositorDesc.label = @"MelonPrime Metal 2D Full GPU Compositor";
    compositorDesc.vertexFunction = compositeVS;
    compositorDesc.fragmentFunction = compositeFS;
    compositorDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    state.CompositorPipeline =
        [state.Device newRenderPipelineStateWithDescriptor:compositorDesc error:&error];
    if (!state.CompositorPipeline)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu 2d: compositor pipeline failed: %s\n", message);
        return false;
    }

    MTLDepthStencilDescriptor* depthDesc = [[MTLDepthStencilDescriptor alloc] init];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;
    state.SpriteDepthState = [state.Device newDepthStencilStateWithDescriptor:depthDesc];

    MTLSamplerDescriptor* repeatDesc = [[MTLSamplerDescriptor alloc] init];
    repeatDesc.minFilter = MTLSamplerMinMagFilterNearest;
    repeatDesc.magFilter = MTLSamplerMinMagFilterNearest;
    repeatDesc.sAddressMode = MTLSamplerAddressModeRepeat;
    repeatDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    state.RepeatSampler = [state.Device newSamplerStateWithDescriptor:repeatDesc];

    MTLTextureDescriptor* dummyDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    dummyDesc.usage = MTLTextureUsageShaderRead;
    dummyDesc.storageMode = MTLStorageModeShared;
    state.DummyColorTexture = [state.Device newTextureWithDescriptor:dummyDesc];
    const uint32_t zero = 0;
    [state.DummyColorTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                               mipmapLevel:0
                                 withBytes:&zero
                               bytesPerRow:sizeof(zero)];

    state.FullGpuReady =
        state.SpritePipeline && state.SpriteMosaicFlagsPipeline &&
        state.SpriteWindowPipeline && state.CompositorPipeline &&
        state.SpriteDepthState && state.RepeatSampler &&
        state.DummyColorTexture;
    return state.FullGpuReady;
}

template <typename StateT>
uint8_t* MetalSegmentedCaptureBase(StateT& state) noexcept
{
    const std::shared_ptr<MetalSegmentedUploadRing>& ring =
        state.SegmentedCaptureRing;
    const int32_t slot = state.SegmentedCaptureSlot;
    if (!ring || !ring->Ready() || slot < 0 ||
        slot >= static_cast<int32_t>(
            MetalSegmentedUploadRing::SlotCount))
    {
        return nullptr;
    }

    id<MTLBuffer> buffer =
        ring->Buffers[static_cast<uint32_t>(slot)];
    if (!buffer || buffer.length < ring->FrameLength)
        return nullptr;
    return static_cast<uint8_t*>([buffer contents]);
}

void MetalRenderer2D::BeginSegmentSnapshotFrameIfNeeded(
    int line) noexcept
{
    if (!State || line != 0)
        return;

    // DrawSprites(0) and DrawScanline(0) can arrive in either order. The first
    // recorder entering a new frame releases an unsubmitted prior reservation,
    // resets validity, and reserves one immutable ring slot for direct writes.
    if (State->LayerSnapshotLastLine > 0 ||
        State->SpriteSnapshotLastLine > 0)
    {
        MetalReleaseSegmentedUploadSlot(
            State->SegmentedCaptureRing,
            State->SegmentedCaptureSlot);
        State->SegmentedCaptureRing.reset();
        State->SegmentedCaptureSlot = -1;
        State->SegmentedCaptureAttempted = false;
        State->LayerSnapshotValid.fill(0);
        State->SpriteSnapshotValid.fill(0);
        State->LayerSnapshotLastLine = -1;
        State->SpriteSnapshotLastLine = -1;
    }

    if (!State->SegmentedCaptureAttempted &&
        State->SnapshotBuffersReady)
    {
        State->SegmentedCaptureAttempted = true;
        std::shared_ptr<MetalSegmentedUploadRing> ring =
            State->SegmentedUploadRing;
        const int32_t slot =
            MetalReserveSegmentedUploadSlot(ring);
        if (slot >= 0)
        {
            State->SegmentedCaptureRing =
                std::move(ring);
            State->SegmentedCaptureSlot = slot;
        }
    }
}

void MetalRenderer2D::MaybeReportSegmentSnapshotFrame() noexcept
{
}

void MetalRenderer2D::CaptureScanlineState(int line) noexcept
{
    if (!State || line < 0 || line >= kScreenH)
        return;

    BeginSegmentSnapshotFrameIfNeeded(line);
    if (!RefreshScanlineConfig(line) ||
        !State->SnapshotBuffersReady)
    {
        return;
    }

    uint8_t* frameUploadBase =
        MetalSegmentedCaptureBase(*State);
    if (!frameUploadBase)
        return;

    const MetalSegmentedUploadLayout uploadLayout =
        MetalMakeSegmentedUploadLayout(
            State->SpriteSnapshotStride);
    auto* sourceScanlines =
        reinterpret_cast<const ScanlineConfigCpu*>(
            [State->ScanlineConfigBuffer contents]);
    auto* capturedScanlines =
        reinterpret_cast<ScanlineConfigCpu*>(
            frameUploadBase +
            uploadLayout.ScanlineFrameOffset);
    capturedScanlines->scanline[line] =
        sourceScanlines->scanline[line];

    // RefreshLayerConfig builds the normalized BG contract consumed by the
    // segmented compositor. It remains compact and is retained separately for
    // segment hashing and BG target selection.
    if (RefreshLayerConfig())
    {
        auto* frame =
            reinterpret_cast<LayerScanlineSnapshotCpu*>(
                [State->LayerScanlineSnapshotBuffer contents]);
        frame->line[line] = State->LayerConfig;
        State->LayerSnapshotHash[line] =
            MetalSnapshotHash(
                &frame->line[line],
                sizeof(LayerConfigCpu));
        State->LayerSnapshotValid[line] = 1;
        State->LayerSnapshotLastLine = line;
    }
}

void MetalRenderer2D::CaptureSpriteScanlineState(int line) noexcept
{
    if (!State || !State->SpriteScanlineConfigBuffer ||
        line < 0 || line >= kScreenH)
    {
        return;
    }

    BeginSegmentSnapshotFrameIfNeeded(line);

    auto* compatibilityConfig =
        reinterpret_cast<SpriteScanlineConfigCpu*>(
            [State->SpriteScanlineConfigBuffer contents]);
    compatibilityConfig->mosaicLine[line] =
        GPU2D.OBJMosaicLine;

    if (!State->SnapshotBuffersReady ||
        !RefreshSpriteConfig(line, line + 1))
    {
        return;
    }

    uint8_t* frameUploadBase =
        MetalSegmentedCaptureBase(*State);
    if (!frameUploadBase)
        return;

    const MetalSegmentedUploadLayout uploadLayout =
        MetalMakeSegmentedUploadLayout(
            State->SpriteSnapshotStride);
    uint8_t* destination =
        frameUploadBase +
        uploadLayout.SpriteFrameOffset +
        State->SpriteSnapshotStride *
            static_cast<size_t>(line);
    std::memcpy(
        destination,
        &State->SpriteConfig,
        sizeof(SpriteConfigCpu));

    auto* capturedSpriteScanlines =
        reinterpret_cast<SpriteScanlineConfigCpu*>(
            frameUploadBase +
            uploadLayout.SpriteScanlineFrameOffset);
    capturedSpriteScanlines->mosaicLine[line] =
        compatibilityConfig->mosaicLine[line];

    auto* meta =
        reinterpret_cast<SpriteScanlineMetaFrameCpu*>(
            [State->SpriteScanlineMetaBuffer contents]);
    meta->line[line].numSprites =
        static_cast<uint32_t>(State->NumSprites);
    meta->line[line].useMosaic =
        State->SpriteUseMosaic ? 1u : 0u;
    meta->line[line].configHash =
        MetalSnapshotHash(
            destination,
            sizeof(SpriteConfigCpu));

    State->SpriteSnapshotHash[line] =
        meta->line[line].configHash ^
        (static_cast<uint64_t>(
            meta->line[line].numSprites) << 32u) ^
        static_cast<uint64_t>(
            meta->line[line].useMosaic);
    State->SpriteSnapshotCount[line] =
        meta->line[line].numSprites;
    State->SpriteSnapshotMosaic[line] =
        static_cast<uint8_t>(
            meta->line[line].useMosaic);
    State->SpriteSnapshotValid[line] = 1;
    State->SpriteSnapshotLastLine = line;
}

bool MetalRenderer2D::SegmentedSnapshotReady() const noexcept
{
    if (!State ||
        !State->SnapshotBuffersReady ||
        !State->LayerScanlineSnapshotBuffer ||
        !State->SpriteScanlineMetaBuffer ||
        State->SpriteSnapshotStride <
            sizeof(SpriteConfigCpu))
    {
        return false;
    }

    const std::shared_ptr<MetalSegmentedUploadRing>& ring =
        State->SegmentedCaptureRing;
    const int32_t slot = State->SegmentedCaptureSlot;
    if (!ring || !ring->Ready() || slot < 0 ||
        slot >= static_cast<int32_t>(
            MetalSegmentedUploadRing::SlotCount))
    {
        return false;
    }

    const uint32_t bit =
        1u << static_cast<uint32_t>(slot);
    if ((ring->BusyMask.load(
             std::memory_order_acquire) & bit) == 0u)
    {
        return false;
    }

    id<MTLBuffer> buffer =
        ring->Buffers[static_cast<uint32_t>(slot)];
    return buffer &&
        buffer.length >= ring->FrameLength &&
        [buffer contents] != nullptr;
}


namespace
{
struct Metal2DSegmentRange
{
    int Start = 0;
    int End = 0;
};

struct Metal2DSegmentList
{
    std::array<Metal2DSegmentRange, kScreenH> Items {};
    uint32_t Count = 0;
};


class MetalSegmentedUploadLease
{
public:
    MetalSegmentedUploadLease() = default;

    MetalSegmentedUploadLease(
        std::shared_ptr<MetalSegmentedUploadRing> ring,
        uint32_t slot) noexcept
        : Ring(std::move(ring)), Slot(slot)
    {
    }

    MetalSegmentedUploadLease(
        const MetalSegmentedUploadLease&) = delete;
    MetalSegmentedUploadLease& operator=(
        const MetalSegmentedUploadLease&) = delete;

    MetalSegmentedUploadLease(
        MetalSegmentedUploadLease&& other) noexcept
        : Ring(std::move(other.Ring)),
          Slot(other.Slot),
          Submitted(other.Submitted)
    {
        other.Submitted = true;
    }

    ~MetalSegmentedUploadLease()
    {
        ReleaseUnsubmitted();
    }

    explicit operator bool() const noexcept
    {
        return Ring &&
            Slot < MetalSegmentedUploadRing::SlotCount &&
            Ring->Buffers[Slot];
    }

    id<MTLBuffer> Buffer() const noexcept
    {
        return *this ? Ring->Buffers[Slot] : nil;
    }

    void AttachToCommandBuffer(
        id<MTLCommandBuffer> command)
    {
        if (!Ring || Submitted || !command)
            return;

        std::shared_ptr<MetalSegmentedUploadRing> ring = Ring;
        const uint32_t bit = 1u << Slot;
        [command addCompletedHandler:^(id<MTLCommandBuffer>) {
            ring->BusyMask.fetch_and(
                ~bit,
                std::memory_order_release);
        }];
        Submitted = true;
    }

private:
    void ReleaseUnsubmitted() noexcept
    {
        if (!Ring || Submitted)
            return;
        Ring->BusyMask.fetch_and(
            ~(1u << Slot),
            std::memory_order_release);
        Submitted = true;
    }

    std::shared_ptr<MetalSegmentedUploadRing> Ring;
    uint32_t Slot = 0;
    bool Submitted = false;
};

template <typename StateT>
MetalSegmentedUploadLease MetalTakeSegmentedUploadLease(
    StateT& state,
    NSUInteger requiredLength) noexcept
{
    std::shared_ptr<MetalSegmentedUploadRing> ring =
        state.SegmentedCaptureRing;
    const int32_t slot = state.SegmentedCaptureSlot;
    if (!ring || !ring->Ready() ||
        ring->FrameLength < requiredLength ||
        slot < 0 ||
        slot >= static_cast<int32_t>(
            MetalSegmentedUploadRing::SlotCount))
    {
        return {};
    }

    const uint32_t bit =
        1u << static_cast<uint32_t>(slot);
    if ((ring->BusyMask.load(
             std::memory_order_acquire) & bit) == 0u)
    {
        return {};
    }

    state.SegmentedCaptureRing.reset();
    state.SegmentedCaptureSlot = -1;
    return MetalSegmentedUploadLease(
        std::move(ring),
        static_cast<uint32_t>(slot));
}

template <typename StateT>
uint64_t MetalCombinedSegmentHash(
    const StateT& state,
    int line)
{
    uint64_t hash = state.LayerSnapshotHash[line];
    hash ^= state.SpriteSnapshotHash[line] +
        0x9E3779B97F4A7C15ull +
        (hash << 6u) +
        (hash >> 2u);
    return hash;
}

template <typename StateT>
Metal2DSegmentList MetalBuild2DSegments(
    const StateT& state)
{
    Metal2DSegmentList segments;
    int start = 0;
    uint64_t previous =
        MetalCombinedSegmentHash(state, 0);

    for (int line = 1; line < kScreenH; line++)
    {
        const uint64_t current =
            MetalCombinedSegmentHash(state, line);
        if (current == previous)
            continue;

        segments.Items[segments.Count++] =
            {start, line};
        start = line;
        previous = current;
    }

    segments.Items[segments.Count++] =
        {start, kScreenH};
    return segments;
}

template <typename StateT>
id<MTLTexture> MetalSnapshotLayerTexture(
    StateT& state,
    const LayerConfigCpu::BGConfig& config)
{
    if (config.pad0[0] == 0)
        return nil;

    const uint32_t index = config.pad0[0] - 1u;
    if (index >= static_cast<uint32_t>(kBGLayerCount))
        return nil;
    return state.AllBGLayerTex[index];
}

bool MetalSnapshotUsesCaptureTextures(
    const LayerConfigCpu& layerConfig,
    const SpriteConfigCpu& spriteConfig,
    uint32_t numSprites)
{
    for (const auto& bg : layerConfig.bgConfig)
    {
        if (bg.type >= 7u)
            return true;
    }

    const uint32_t count = std::min(numSprites, 128u);
    for (uint32_t index = 0; index < count; index++)
    {
        if (spriteConfig.oam[index].type >= 3u)
            return true;
    }
    return false;
}
}

bool MetalRenderer2D::SegmentedFrameComplete() const noexcept
{
    if (!SegmentedSnapshotReady())
        return false;

    for (int line = 0; line < kScreenH; line++)
    {
        if (!State->LayerSnapshotValid[line] ||
            !State->SpriteSnapshotValid[line])
        {
            return false;
        }
    }
    return true;
}

bool MetalRenderer2D::SegmentedRenderReady() const noexcept
{
    return State &&
        State->SegmentedRenderReady &&
        SegmentedFrameComplete() &&
        State->LayerPipeline &&
        State->LayerVertexBuffer &&
        State->SpritePipeline &&
        State->CompositorPipeline;
}

bool MetalRenderer2D::RenderSegmentedGpuFrame(
    void* high3DTexture,
    void* capture128Texture,
    void* capture256Texture,
    bool allowCaptureTextures) noexcept
{
    if (!SegmentedRenderReady())
        return false;

    id<MTLTexture> high3D =
        (__bridge id<MTLTexture>)high3DTexture;
    id<MTLTexture> capture128 =
        (__bridge id<MTLTexture>)capture128Texture;
    id<MTLTexture> capture256 =
        (__bridge id<MTLTexture>)capture256Texture;
    if (!high3D || !capture128 || !capture256 ||
        high3D.device != State->Device ||
        capture128.device != State->Device ||
        capture256.device != State->Device)
    {
        return false;
    }

    if (!UploadRawVRAMInputs() ||
        !UploadPaletteInputs() ||
        !RefreshCompositorConfig())
    {
        return false;
    }

    auto* layerFrame =
        reinterpret_cast<LayerScanlineSnapshotCpu*>(
            [State->LayerScanlineSnapshotBuffer contents]);
    auto* spriteMeta =
        reinterpret_cast<SpriteScanlineMetaFrameCpu*>(
            [State->SpriteScanlineMetaBuffer contents]);

    const Metal2DSegmentList segments =
        MetalBuild2DSegments(*State);
    if (segments.Count == 0)
        return false;

    const MetalSegmentedUploadLayout uploadLayout =
        MetalMakeSegmentedUploadLayout(
            State->SpriteSnapshotStride);
    MetalSegmentedUploadLease uploadLease =
        MetalTakeSegmentedUploadLease(
            *State,
            uploadLayout.FrameLength);
    if (!uploadLease)
        return false;

    id<MTLBuffer> frameUploadBuffer =
        uploadLease.Buffer();
    if (!frameUploadBuffer)
        return false;

    const NSUInteger spriteFrameOffset =
        uploadLayout.SpriteFrameOffset;
    const NSUInteger scanlineFrameOffset =
        uploadLayout.ScanlineFrameOffset;
    const NSUInteger spriteScanlineFrameOffset =
        uploadLayout.SpriteScanlineFrameOffset;

    uint8_t* frameUploadBase =
        static_cast<uint8_t*>(
            [frameUploadBuffer contents]);
    if (!frameUploadBase)
        return false;
    uint8_t* spriteBase =
        frameUploadBase + spriteFrameOffset;

    if (!allowCaptureTextures)
    {
        for (uint32_t segmentIndex = 0;
             segmentIndex < segments.Count;
             segmentIndex++)
        {
            const Metal2DSegmentRange& segment =
                segments.Items[segmentIndex];
            const int line = segment.Start;
            const auto* spriteConfig =
                reinterpret_cast<const SpriteConfigCpu*>(
                    spriteBase +
                    State->SpriteSnapshotStride *
                        static_cast<size_t>(line));
            if (MetalSnapshotUsesCaptureTextures(
                    layerFrame->line[line],
                    *spriteConfig,
                    spriteMeta->line[line].numSprites))
            {
                return false;
            }
        }
    }

    id<MTLCommandBuffer> command =
        [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label =
        @"MelonPrime Metal Phase 8O Segmented 2D";

    {
        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture =
            State->OutputTex;
        pass.colorAttachments[0].loadAction =
            MTLLoadActionClear;
        pass.colorAttachments[0].storeAction =
            MTLStoreActionStore;
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(0, 0, 0, 1);
        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder)
            return false;
        [encoder endEncoding];
    }

    auto makeOBJPass = [&](MTLLoadAction loadAction) {
        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture =
            State->OBJLayerTex;
        pass.colorAttachments[0].slice = 0;
        pass.colorAttachments[0].loadAction = loadAction;
        pass.colorAttachments[0].storeAction =
            MTLStoreActionStore;
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(0, 0, 0, 0);
        pass.colorAttachments[1].texture =
            State->OBJLayerTex;
        pass.colorAttachments[1].slice = 1;
        pass.colorAttachments[1].loadAction = loadAction;
        pass.colorAttachments[1].storeAction =
            MTLStoreActionStore;
        pass.colorAttachments[1].clearColor =
            MTLClearColorMake(0, 0, 0, 0);
        pass.depthAttachment.texture =
            State->OBJDepthTex;
        pass.depthAttachment.loadAction = loadAction;
        pass.depthAttachment.storeAction =
            MTLStoreActionStore;
        pass.depthAttachment.clearDepth = 1.0;
        return pass;
    };

    {
        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:
                makeOBJPass(MTLLoadActionClear)];
        if (!encoder)
            return false;
        [encoder endEncoding];
    }

    const uint32_t scale =
        static_cast<uint32_t>(std::max(1, State->Scale));

    // BG textures depend on the normalized layer config and the
    // frame-final VRAM/palette uploads, not on OBJ/window-only segment
    // changes. Reuse a target until another config overwrites it.
    std::array<uint64_t, kBGLayerCount>
        bgTargetSignature {};
    std::array<uint8_t, kBGLayerCount>
        bgTargetValid {};

    for (uint32_t segmentIndex = 0;
         segmentIndex < segments.Count;
         segmentIndex++)
    {
        const Metal2DSegmentRange& segment =
            segments.Items[segmentIndex];
        const int line = segment.Start;
        const LayerConfigCpu& layerConfig =
            layerFrame->line[line];
        const SpriteScanlineMetaCpu& meta =
            spriteMeta->line[line];
        const size_t spriteOffset =
            State->SpriteSnapshotStride *
            static_cast<size_t>(line);
        id<MTLTexture> layerTextures[4] = {};

        for (uint32_t layer = 0; layer < 4; layer++)
        {
            const auto& cfg =
                layerConfig.bgConfig[layer];
            id<MTLTexture> target =
                MetalSnapshotLayerTexture(*State, cfg);
            layerTextures[layer] = target;

            if (!target || cfg.type >= 6u ||
                cfg.size[0] == 0 ||
                cfg.size[1] == 0)
            {
                continue;
            }

            const uint32_t targetIndex =
                cfg.pad0[0] - 1u;
            const uint64_t desiredSignature =
                State->LayerSnapshotHash[line] ^
                (0x9E3779B97F4A7C15ull *
                 static_cast<uint64_t>(layer + 1u));
            if (targetIndex <
                    static_cast<uint32_t>(kBGLayerCount) &&
                bgTargetValid[targetIndex] &&
                bgTargetSignature[targetIndex] ==
                    desiredSignature)
            {
                continue;
            }

            MTLRenderPassDescriptor* pass =
                [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction =
                MTLLoadActionClear;
            pass.colorAttachments[0].storeAction =
                MTLStoreActionStore;
            pass.colorAttachments[0].clearColor =
                MTLClearColorMake(0, 0, 0, 0);

            id<MTLRenderCommandEncoder> encoder =
                [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder)
                return false;

            [encoder setRenderPipelineState:
                State->LayerPipeline];
            [encoder setVertexBuffer:
                State->LayerVertexBuffer
                              offset:0
                             atIndex:0];
            [encoder setFragmentBytes:&layerConfig
                                length:sizeof(layerConfig)
                               atIndex:0];
            [encoder setFragmentBytes:&layer
                                length:sizeof(layer)
                               atIndex:1];
            [encoder setFragmentTexture:
                State->VRAMTexBG
                                  atIndex:0];
            [encoder setFragmentTexture:
                State->PalTexBG
                                  atIndex:1];
            [encoder setViewport:(MTLViewport){
                0.0,
                0.0,
                static_cast<double>(cfg.size[0]),
                static_cast<double>(cfg.size[1]),
                0.0,
                1.0 }];
            [encoder drawPrimitives:
                MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6];
            [encoder endEncoding];

            if (targetIndex <
                    static_cast<uint32_t>(kBGLayerCount))
            {
                bgTargetSignature[targetIndex] =
                    desiredSignature;
                bgTargetValid[targetIndex] = 1u;
            }
        }

        const NSUInteger scissorY =
            static_cast<NSUInteger>(segment.Start) * scale;
        const NSUInteger scissorHeight =
            static_cast<NSUInteger>(
                segment.End - segment.Start) * scale;
        const MTLScissorRect scissor = {
            0,
            scissorY,
            State->OutputTex.width,
            scissorHeight,
        };

        const uint32_t numSprites =
            std::min(meta.numSprites, 128u);
        if (numSprites > 0)
        {
            auto encodeSpritePass =
                [&](id<MTLRenderPipelineState> pipeline,
                    uint32_t passMode,
                    bool depthWrite) -> bool {
                id<MTLRenderCommandEncoder> encoder =
                    [command renderCommandEncoderWithDescriptor:
                        makeOBJPass(MTLLoadActionLoad)];
                if (!encoder)
                    return false;

                [encoder setRenderPipelineState:pipeline];
                if (depthWrite)
                {
                    [encoder setDepthStencilState:
                        State->SpriteDepthState];
                }
                [encoder setVertexBuffer:
                    frameUploadBuffer
                                  offset:
                                      spriteFrameOffset +
                                      spriteOffset
                                 atIndex:0];
                [encoder setVertexBytes:&numSprites
                                  length:sizeof(numSprites)
                                 atIndex:1];
                [encoder setFragmentBuffer:
                    frameUploadBuffer
                                    offset:
                                        spriteFrameOffset +
                                        spriteOffset
                                   atIndex:0];
                [encoder setFragmentBuffer:
                    frameUploadBuffer
                                    offset:
                                        spriteScanlineFrameOffset
                                   atIndex:1];
                [encoder setFragmentBytes:&passMode
                                    length:sizeof(passMode)
                                   atIndex:2];
                [encoder setFragmentTexture:
                    State->VRAMTexOBJ
                                      atIndex:0];
                [encoder setFragmentTexture:
                    State->PalTexOBJ
                                      atIndex:1];
                [encoder setFragmentTexture:capture128
                                      atIndex:2];
                [encoder setFragmentTexture:capture256
                                      atIndex:3];
                [encoder setFragmentSamplerState:
                    State->RepeatSampler
                                           atIndex:0];
                [encoder setViewport:(MTLViewport){
                    0.0,
                    0.0,
                    static_cast<double>(State->OutputTex.width),
                    static_cast<double>(State->OutputTex.height),
                    0.0,
                    1.0 }];
                [encoder setScissorRect:scissor];
                [encoder drawPrimitives:
                    MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:6
                          instanceCount:numSprites * 2u];
                [encoder endEncoding];
                return true;
            };

            if (meta.useMosaic != 0u &&
                !encodeSpritePass(
                    State->SpriteMosaicFlagsPipeline,
                    1u,
                    false))
                return false;
            if (!encodeSpritePass(
                    State->SpriteWindowPipeline,
                    2u,
                    false))
                return false;
            if (!encodeSpritePass(
                    State->SpritePipeline,
                    0u,
                    true))
                return false;
        }

        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture =
            State->OutputTex;
        pass.colorAttachments[0].loadAction =
            MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction =
            MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder)
            return false;

        [encoder setRenderPipelineState:
            State->CompositorPipeline];
        [encoder setFragmentBytes:&layerConfig
                            length:sizeof(layerConfig)
                           atIndex:0];
        [encoder setFragmentBuffer:
            frameUploadBuffer
                            offset:scanlineFrameOffset
                           atIndex:1];
        [encoder setFragmentBytes:
            &State->CompositorConfig
                            length:sizeof(State->CompositorConfig)
                           atIndex:2];
        [encoder setFragmentBytes:&scale
                            length:sizeof(scale)
                           atIndex:3];

        for (int layer = 0; layer < 4; layer++)
        {
            id<MTLTexture> texture =
                layerTextures[layer]
                    ? layerTextures[layer]
                    : State->DummyColorTexture;
            [encoder setFragmentTexture:texture
                                atIndex:layer];
        }
        [encoder setFragmentTexture:high3D atIndex:4];
        [encoder setFragmentTexture:
            State->OBJLayerTex atIndex:5];
        [encoder setFragmentTexture:
            State->MosaicTex atIndex:6];
        [encoder setFragmentTexture:capture128 atIndex:7];
        [encoder setFragmentTexture:capture256 atIndex:8];
        [encoder setFragmentSamplerState:
            State->RepeatSampler atIndex:0];
        [encoder setViewport:(MTLViewport){
            0.0,
            0.0,
            static_cast<double>(State->OutputTex.width),
            static_cast<double>(State->OutputTex.height),
            0.0,
            1.0 }];
        [encoder setScissorRect:scissor];
        [encoder drawPrimitives:
            MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:3];
        [encoder endEncoding];
    }

    uploadLease.AttachToCommandBuffer(command);
    [command commit];
    return true;
}

bool MetalRenderer2D::FrameUsesCaptureTextures() const noexcept
{
    if (!State)
        return true;
    for (const auto& bg : State->LayerConfig.bgConfig)
    {
        if (bg.type >= 7u)
            return true;
    }
    for (int i = 0; i < State->NumSprites; i++)
    {
        if (State->SpriteConfig.oam[i].type >= 3u)
            return true;
    }
    return false;
}

bool MetalRenderer2D::EncodeFullGpuSprites(
    void* capture128Texture,
    void* capture256Texture) noexcept
{
    if (!State || !State->Queue || !State->OBJLayerTex ||
        !State->OBJDepthTex || !State->SpriteConfigBuffer ||
        !State->SpriteScanlineConfigBuffer || !State->VRAMTexOBJ ||
        !State->PalTexOBJ || !State->FullGpuReady)
    {
        return false;
    }

    id<MTLTexture> capture128 =
        (__bridge id<MTLTexture>)capture128Texture;
    id<MTLTexture> capture256 =
        (__bridge id<MTLTexture>)capture256Texture;
    if (!capture128 || !capture256 ||
        capture128.device != State->Device ||
        capture256.device != State->Device)
    {
        return false;
    }

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal Full GPU Sprite Frame";

    auto makePass = [&](MTLLoadAction loadAction) {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = State->OBJLayerTex;
        pass.colorAttachments[0].slice = 0;
        pass.colorAttachments[0].loadAction = loadAction;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.colorAttachments[1].texture = State->OBJLayerTex;
        pass.colorAttachments[1].slice = 1;
        pass.colorAttachments[1].loadAction = loadAction;
        pass.colorAttachments[1].storeAction = MTLStoreActionStore;
        pass.colorAttachments[1].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.depthAttachment.texture = State->OBJDepthTex;
        pass.depthAttachment.loadAction = loadAction;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.clearDepth = 1.0;
        return pass;
    };

    {
        id<MTLRenderCommandEncoder> clearEncoder =
            [command renderCommandEncoderWithDescriptor:makePass(MTLLoadActionClear)];
        if (!clearEncoder)
            return false;
        [clearEncoder endEncoding];
    }

    const uint32_t numSprites = static_cast<uint32_t>(State->NumSprites);
    if (numSprites > 0)
    {
        auto encodePass = [&](id<MTLRenderPipelineState> pipeline,
                              uint32_t passMode,
                              bool depthWrite) -> bool {
            id<MTLRenderCommandEncoder> encoder =
                [command renderCommandEncoderWithDescriptor:makePass(MTLLoadActionLoad)];
            if (!encoder)
                return false;
            [encoder setRenderPipelineState:pipeline];
            if (depthWrite)
                [encoder setDepthStencilState:State->SpriteDepthState];
            [encoder setVertexBuffer:State->SpriteConfigBuffer offset:0 atIndex:0];
            [encoder setVertexBytes:&numSprites length:sizeof(numSprites) atIndex:1];
            [encoder setFragmentBuffer:State->SpriteConfigBuffer offset:0 atIndex:0];
            [encoder setFragmentBuffer:State->SpriteScanlineConfigBuffer offset:0 atIndex:1];
            [encoder setFragmentBytes:&passMode length:sizeof(passMode) atIndex:2];
            [encoder setFragmentTexture:State->VRAMTexOBJ atIndex:0];
            [encoder setFragmentTexture:State->PalTexOBJ atIndex:1];
            [encoder setFragmentTexture:capture128 atIndex:2];
            [encoder setFragmentTexture:capture256 atIndex:3];
            [encoder setFragmentSamplerState:State->RepeatSampler atIndex:0];
            [encoder setViewport:(MTLViewport){
                0.0, 0.0,
                static_cast<double>(State->OutputTex.width),
                static_cast<double>(State->OutputTex.height),
                0.0, 1.0 }];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:numSprites * 2u];
            [encoder endEncoding];
            return true;
        };

        if (State->SpriteUseMosaic &&
            !encodePass(State->SpriteMosaicFlagsPipeline, 1u, false))
            return false;
        if (!encodePass(State->SpriteWindowPipeline, 2u, false))
            return false;
        if (!encodePass(State->SpritePipeline, 0u, true))
            return false;
    }

    [command commit];
    return true;
}

bool MetalRenderer2D::EncodeFullGpuCompositor(
    void* high3DTexture,
    void* capture128Texture,
    void* capture256Texture) noexcept
{
    if (!State || !State->Queue || !State->OutputTex ||
        !State->CompositorPipeline || !State->LayerConfigBuffer ||
        !State->ScanlineConfigBuffer || !State->CompositorConfigBuffer ||
        !State->OBJLayerTex || !State->MosaicTex || !State->RepeatSampler)
    {
        return false;
    }

    id<MTLTexture> high3D = (__bridge id<MTLTexture>)high3DTexture;
    id<MTLTexture> capture128 =
        (__bridge id<MTLTexture>)capture128Texture;
    id<MTLTexture> capture256 =
        (__bridge id<MTLTexture>)capture256Texture;
    if (!high3D || !capture128 || !capture256 ||
        high3D.device != State->Device ||
        capture128.device != State->Device ||
        capture256.device != State->Device)
    {
        return false;
    }

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal Full GPU 2D Compositor";

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->OutputTex;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return false;

    [encoder setRenderPipelineState:State->CompositorPipeline];
    [encoder setFragmentBuffer:State->LayerConfigBuffer offset:0 atIndex:0];
    [encoder setFragmentBuffer:State->ScanlineConfigBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:State->CompositorConfigBuffer offset:0 atIndex:2];
    const uint32_t scale = static_cast<uint32_t>(std::max(1, State->Scale));
    [encoder setFragmentBytes:&scale length:sizeof(scale) atIndex:3];

    for (int layer = 0; layer < 4; layer++)
    {
        id<MTLTexture> texture = State->BGLayerTex[layer]
            ? State->BGLayerTex[layer]
            : State->DummyColorTexture;
        [encoder setFragmentTexture:texture atIndex:layer];
    }
    [encoder setFragmentTexture:high3D atIndex:4];
    [encoder setFragmentTexture:State->OBJLayerTex atIndex:5];
    [encoder setFragmentTexture:State->MosaicTex atIndex:6];
    [encoder setFragmentTexture:capture128 atIndex:7];
    [encoder setFragmentTexture:capture256 atIndex:8];
    [encoder setFragmentSamplerState:State->RepeatSampler atIndex:0];
    [encoder setViewport:(MTLViewport){
        0.0, 0.0,
        static_cast<double>(State->OutputTex.width),
        static_cast<double>(State->OutputTex.height),
        0.0, 1.0 }];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    [command commit];
    return true;
}

bool MetalRenderer2D::RenderFullGpuFrame(
    void* high3DTexture,
    void* capture128Texture,
    void* capture256Texture,
    bool allowCaptureTextures) noexcept
{
    if (!FullGpuReady())
        return false;

    if (!UploadRawVRAMInputs() ||
        !UploadPaletteInputs() ||
        !RefreshLayerConfig() ||
        !RefreshSpriteConfig(0, kScreenH) ||
        !RefreshCompositorConfig())
    {
        return false;
    }

    if (!State->LoggedHudParity &&
        GPU2D.Num == 0 &&
        (GPU2D.DispCnt & (1u << 3)))
    {
        State->LoggedHudParity = true;
        std::fprintf(
            stderr,
            "[MelonPrime] metal 2d HUD parity: engine=%u "
            "layerEnable=0x%02x sprites=%d "
            "bgUpload=%zu objUpload=%zu "
            "bgPalUpload=%zu objPalUpload=%zu "
            "totalBG=%llu totalOBJ=%llu\n",
            GPU2D.Num,
            static_cast<unsigned>(GPU2D.LayerEnable),
            State->NumSprites,
            State->LastBGUploadBytes,
            State->LastOBJUploadBytes,
            State->LastBGPaletteUploadBytes,
            State->LastOBJPaletteUploadBytes,
            static_cast<unsigned long long>(State->TotalBGUploadBytes),
            static_cast<unsigned long long>(State->TotalOBJUploadBytes));
    }

    // Same-frame feedback from a destination capture into a BG/OBJ layer
    // requires scanline-segment ordering. Phase 8B keeps that rare case on the
    // stable CPU path, while capture-only and capture-sampling-only frames are
    // fully GPU-resident.
    if (!allowCaptureTextures && FrameUsesCaptureTextures())
        return false;

    if (!PrerenderConfiguredLayers())
        return false;
    if (!EncodeFullGpuSprites(capture128Texture, capture256Texture))
        return false;
    return EncodeFullGpuCompositor(
        high3DTexture, capture128Texture, capture256Texture);
}

bool MetalRenderer2D::FullGpuReady() const noexcept
{
    return State && State->FullGpuReady && State->Queue &&
           State->OutputTex && State->OBJLayerTex &&
           State->OBJDepthTex && State->CompositorPipeline;
}
