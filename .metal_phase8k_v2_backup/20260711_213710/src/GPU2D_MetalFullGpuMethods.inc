// Included near the end of GPU2D_Metal.mm.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_HUD_PARITY_V1
// MELONPRIME_METAL_2D_SCANLINE_SNAPSHOT_V1
// MELONPRIME_METAL_2D_SEGMENTED_SHADOW_RENDER_V1

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

void MetalRenderer2D::BeginSegmentSnapshotFrameIfNeeded(
    int line) noexcept
{
    if (!State || line != 0)
        return;

    // DrawSprites(0) and DrawScanline(0) can arrive in either order. Reset only
    // when at least one recorder still points at a line from the previous frame.
    if (State->LayerSnapshotLastLine > 0 ||
        State->SpriteSnapshotLastLine > 0)
    {
        State->LayerSnapshotValid.fill(0);
        State->SpriteSnapshotValid.fill(0);
        State->LayerSnapshotLastLine = -1;
        State->SpriteSnapshotLastLine = -1;
    }
}

void MetalRenderer2D::MaybeReportSegmentSnapshotFrame() noexcept
{
    if (!State || !State->SnapshotBuffersReady)
        return;

    for (int line = 0; line < kScreenH; line++)
    {
        if (!State->LayerSnapshotValid[line] ||
            !State->SpriteSnapshotValid[line])
        {
            return;
        }
    }

    uint32_t layerSegments = 1;
    uint32_t spriteSegments = 1;
    uint32_t combinedSegments = 1;
    uint32_t maxSprites = State->SpriteSnapshotCount[0];
    uint64_t spriteTotal = State->SpriteSnapshotCount[0];

    auto combinedHash = [&](int line) {
        uint64_t hash = State->LayerSnapshotHash[line];
        hash ^= State->SpriteSnapshotHash[line] +
            0x9E3779B97F4A7C15ull + (hash << 6u) + (hash >> 2u);
        return hash;
    };

    uint64_t previousCombined = combinedHash(0);
    for (int line = 1; line < kScreenH; line++)
    {
        if (State->LayerSnapshotHash[line] !=
            State->LayerSnapshotHash[line - 1])
        {
            layerSegments++;
        }
        if (State->SpriteSnapshotHash[line] !=
            State->SpriteSnapshotHash[line - 1])
        {
            spriteSegments++;
        }

        const uint64_t currentCombined = combinedHash(line);
        if (currentCombined != previousCombined)
        {
            combinedSegments++;
            previousCombined = currentCombined;
        }

        maxSprites = std::max(
            maxSprites,
            State->SpriteSnapshotCount[line]);
        spriteTotal += State->SpriteSnapshotCount[line];
    }

    State->SnapshotFrameCount++;
    if (State->SnapshotFrameCount == 1 ||
        (State->SnapshotFrameCount % 600u) == 0u)
    {
        std::fprintf(
            stderr,
            "[MelonPrime] metal segmented 2d snapshot: "
            "engine=%u frame=%llu lines=%d "
            "layerSegments=%u spriteSegments=%u combinedSegments=%u "
            "avgSprites=%.2f maxSprites=%u "
            "gpuReady=%u visiblePath=ownership-gated-software-2d\\n",
            GPU2D.Num,
            static_cast<unsigned long long>(
                State->SnapshotFrameCount),
            kScreenH,
            layerSegments,
            spriteSegments,
            combinedSegments,
            static_cast<double>(spriteTotal) /
                static_cast<double>(kScreenH),
            maxSprites,
            SegmentedSnapshotReady() ? 1u : 0u);
    }
}

void MetalRenderer2D::CaptureScanlineState(int line) noexcept
{
    if (!State || line < 0 || line >= kScreenH)
        return;

    BeginSegmentSnapshotFrameIfNeeded(line);
    RefreshScanlineConfig(line);

    if (State->SnapshotBuffersReady)
    {
        // RefreshLayerConfig builds the same normalized BG contract currently
        // consumed at VBlank. Store it per line instead of losing all but the
        // final register state.
        if (RefreshLayerConfig())
        {
            auto* frame = reinterpret_cast<LayerScanlineSnapshotCpu*>(
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

    MaybeReportSegmentSnapshotFrame();
}

void MetalRenderer2D::CaptureSpriteScanlineState(int line) noexcept
{
    if (!State || !State->SpriteScanlineConfigBuffer ||
        line < 0 || line >= kScreenH)
    {
        return;
    }

    BeginSegmentSnapshotFrameIfNeeded(line);

    auto* config = reinterpret_cast<SpriteScanlineConfigCpu*>(
        [State->SpriteScanlineConfigBuffer contents]);
    config->mosaicLine[line] = GPU2D.OBJMosaicLine;

    if (State->SnapshotBuffersReady &&
        RefreshSpriteConfig(line, line + 1))
    {
        uint8_t* destination =
            static_cast<uint8_t*>(
                [State->SpriteScanlineSnapshotBuffer contents]) +
            State->SpriteSnapshotStride *
                static_cast<size_t>(line);
        std::memcpy(
            destination,
            &State->SpriteConfig,
            sizeof(SpriteConfigCpu));

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

    MaybeReportSegmentSnapshotFrame();
}

bool MetalRenderer2D::SegmentedSnapshotReady() const noexcept
{
    return State &&
        State->SnapshotBuffersReady &&
        State->LayerScanlineSnapshotBuffer &&
        State->SpriteScanlineSnapshotBuffer &&
        State->SpriteScanlineMetaBuffer &&
        State->SpriteSnapshotStride >=
            sizeof(SpriteConfigCpu);
}


namespace
{
struct Metal2DSegmentRange
{
    int Start = 0;
    int End = 0;
};

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
std::vector<Metal2DSegmentRange> MetalBuild2DSegments(
    const StateT& state)
{
    std::vector<Metal2DSegmentRange> segments;
    int start = 0;
    uint64_t previous = MetalCombinedSegmentHash(state, 0);

    for (int line = 1; line < kScreenH; line++)
    {
        const uint64_t current =
            MetalCombinedSegmentHash(state, line);
        if (current == previous)
            continue;

        segments.push_back({start, line});
        start = line;
        previous = current;
    }

    segments.push_back({start, kScreenH});
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
    uint8_t* spriteBase =
        static_cast<uint8_t*>(
            [State->SpriteScanlineSnapshotBuffer contents]);

    // Snapshot the shared CPU-written buffers into command-owned resources.
    // The next emulated frame may begin updating the 192 scanline slots before
    // this GPU command completes; immutable per-submit buffers avoid that race.
    const NSUInteger spriteFrameLength =
        State->SpriteSnapshotStride *
        static_cast<NSUInteger>(kScreenH);
    id<MTLBuffer> spriteFrameBuffer =
        [State->Device newBufferWithBytes:spriteBase
                                  length:spriteFrameLength
                                 options:MTLResourceStorageModeShared];
    id<MTLBuffer> scanlineFrameBuffer =
        [State->Device newBufferWithBytes:
            [State->ScanlineConfigBuffer contents]
                                  length:sizeof(ScanlineConfigCpu)
                                 options:MTLResourceStorageModeShared];
    id<MTLBuffer> spriteScanlineFrameBuffer =
        [State->Device newBufferWithBytes:
            [State->SpriteScanlineConfigBuffer contents]
                                  length:sizeof(SpriteScanlineConfigCpu)
                                 options:MTLResourceStorageModeShared];
    if (!spriteFrameBuffer || !scanlineFrameBuffer ||
        !spriteScanlineFrameBuffer)
    {
        return false;
    }

    const std::vector<Metal2DSegmentRange> segments =
        MetalBuild2DSegments(*State);
    if (segments.empty())
        return false;

    if (!allowCaptureTextures)
    {
        for (const Metal2DSegmentRange& segment : segments)
        {
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

    const bool visibleSegmentedPath =
        MetalSegmented2DVisibleEnabled();
    id<MTLCommandBuffer> command =
        [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label = visibleSegmentedPath
        ? @"MelonPrime Metal Phase 8J Segmented 2D Visible"
        : @"MelonPrime Metal Phase 8J Segmented 2D Shadow";

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
    uint64_t bgDraws = 0;
    uint64_t spriteDraws = 0;

    for (const Metal2DSegmentRange& segment : segments)
    {
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
            bgDraws++;
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
                    spriteFrameBuffer
                                  offset:spriteOffset
                                 atIndex:0];
                [encoder setVertexBytes:&numSprites
                                  length:sizeof(numSprites)
                                 atIndex:1];
                [encoder setFragmentBuffer:
                    spriteFrameBuffer
                                    offset:spriteOffset
                                   atIndex:0];
                [encoder setFragmentBuffer:
                    spriteScanlineFrameBuffer
                                    offset:0
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
                spriteDraws++;
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
            scanlineFrameBuffer
                            offset:0
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

    [command commit];

    State->SegmentedRenderFrames++;
    State->SegmentedRenderSegments += segments.size();
    State->SegmentedRenderBgDraws += bgDraws;
    State->SegmentedRenderSpriteDraws += spriteDraws;

    if (!State->LoggedSegmentedRender ||
        (State->SegmentedRenderFrames % 600u) == 0u)
    {
        State->LoggedSegmentedRender = true;
        std::fprintf(
            stderr,
            "[MelonPrime] metal segmented 2d render: "
            "engine=%u frame=%llu segments=%zu "
            "bgDraws=%llu spriteDraws=%llu "
            "scale=%u visible=%u shadow=%u "
            "resourceModel=frame-final\n",
            GPU2D.Num,
            static_cast<unsigned long long>(
                State->SegmentedRenderFrames),
            segments.size(),
            static_cast<unsigned long long>(bgDraws),
            static_cast<unsigned long long>(spriteDraws),
            scale,
            visibleSegmentedPath ? 1u : 0u,
            visibleSegmentedPath ? 0u : 1u);
    }

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
