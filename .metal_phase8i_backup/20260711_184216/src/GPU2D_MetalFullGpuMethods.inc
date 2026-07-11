// Included near the end of GPU2D_Metal.mm.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_HUD_PARITY_V1

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

void MetalRenderer2D::CaptureScanlineState(int line) noexcept
{
    if (!State || line < 0 || line >= kScreenH)
        return;
    RefreshScanlineConfig(line);
}

void MetalRenderer2D::CaptureSpriteScanlineState(int line) noexcept
{
    if (!State || !State->SpriteScanlineConfigBuffer ||
        line < 0 || line >= kScreenH)
        return;
    auto* config = reinterpret_cast<SpriteScanlineConfigCpu*>(
        [State->SpriteScanlineConfigBuffer contents]);
    config->mosaicLine[line] = GPU2D.OBJMosaicLine;
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
