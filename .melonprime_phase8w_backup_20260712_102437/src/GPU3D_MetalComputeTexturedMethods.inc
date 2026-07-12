// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1

void MetalComputeRenderer3D::SetCaptureTextures(
    void* capture128Texture,
    void* capture256Texture) noexcept
{
    if (!State)
        return;

    id<MTLTexture> capture128 =
        capture128Texture ? (__bridge id<MTLTexture>)capture128Texture : nil;
    id<MTLTexture> capture256 =
        capture256Texture ? (__bridge id<MTLTexture>)capture256Texture : nil;

    if (capture128 && capture128.device == State->Device &&
        capture128.textureType == MTLTextureType2DArray)
    {
        State->Capture128Texture = capture128;
    }
    else
    {
        State->Capture128Texture = State->DummyCapture128Texture;
    }

    if (capture256 && capture256.device == State->Device &&
        capture256.textureType == MTLTextureType2DArray)
    {
        State->Capture256Texture = capture256;
    }
    else
    {
        State->Capture256Texture = State->DummyCapture256Texture;
    }
}

bool MetalComputeRenderer3D::RunTextureVariantTileSelfTest()
{
    if (!State || !State->Device || !State->Queue ||
        !State->TextureRasterPipeline ||
        !State->DummyCapture128Texture ||
        !State->DummyCapture256Texture)
    {
        return false;
    }

    constexpr uint32_t maxWorkTiles = 1;
    constexpr uint32_t lineCount = 8;
    constexpr uint32_t tileWidth = 8;
    constexpr uint32_t tileArea = tileWidth * lineCount;
    const SpanBinConfig spanConfig {
        1, 1, lineCount,
        tileWidth, lineCount, tileWidth, 1, 1,
        tileWidth, 1, 64, tileWidth,
        maxWorkTiles, kBinStride, kCoarseBinStride, 1,
        0, 0, maxWorkTiles, 0,
    };

    id<MTLBuffer> header =
        [State->Device newBufferWithLength:kBinHeaderWords * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> polygons =
        [State->Device newBufferWithLength:sizeof(RenderPolygon)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> xSpans =
        [State->Device newBufferWithLength:lineCount * sizeof(SpanSetupX)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> work =
        [State->Device newBufferWithLength:maxWorkTiles * 2u * sizeof(WorkDesc)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> variants =
        [State->Device newBufferWithLength:sizeof(VariantMeta)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> colors =
        [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> depths =
        [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> attrs =
        [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    const uint16_t directRed = 0x801Fu;
    id<MTLBuffer> textureMemory =
        [State->Device newBufferWithBytes:&directRed
                                   length:sizeof(directRed)
                                  options:MTLResourceStorageModeShared];
    const uint16_t zeroPalette = 0;
    id<MTLBuffer> texturePalette =
        [State->Device newBufferWithBytes:&zeroPalette
                                   length:sizeof(zeroPalette)
                                  options:MTLResourceStorageModeShared];
    std::array<uint32_t, 32> toonTable {};
    id<MTLBuffer> toon =
        [State->Device newBufferWithBytes:toonTable.data()
                                   length:sizeof(toonTable)
                                  options:MTLResourceStorageModeShared];

    if (!header || !polygons || !xSpans || !work || !variants ||
        !colors || !depths || !attrs ||
        !textureMemory || !texturePalette || !toon)
    {
        return false;
    }

    std::memset([header contents], 0, header.length);
    std::memset([polygons contents], 0, polygons.length);
    std::memset([xSpans contents], 0, xSpans.length);
    std::memset([work contents], 0, work.length);
    std::memset([variants contents], 0, variants.length);
    std::memset([colors contents], 0, colors.length);
    std::memset([depths contents], 0, depths.length);
    std::memset([attrs contents], 0, attrs.length);

    auto* headerWords = static_cast<uint32_t*>([header contents]);
    headerWords[3] = 1;
    auto* polygon = static_cast<RenderPolygon*>([polygons contents]);
    polygon[0] = {
        0, 0, 8, 0, 7, 0, 0,
        0,
        31u << 16u,
        0.0f,
    };

    auto* lines = static_cast<SpanSetupX*>([xSpans contents]);
    for (uint32_t y = 0; y < lineCount; y++)
    {
        lines[y] = {};
        lines[y].X0 = 0;
        lines[y].X1 = 8;
        lines[y].Flags =
            (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u);
        lines[y].Z0 = lines[y].Z1 = 0x123456;
        lines[y].W0 = lines[y].W1 = 0x1000;
        lines[y].ColorR0 = lines[y].ColorR1 = 63 << 3;
        lines[y].ColorG0 = lines[y].ColorG1 = 63 << 3;
        lines[y].ColorB0 = lines[y].ColorB1 = 63 << 3;
        lines[y].TexcoordU0 = lines[y].TexcoordU1 = 0;
        lines[y].TexcoordV0 = lines[y].TexcoordV1 = 0;
    }

    auto* workDescs = static_cast<WorkDesc*>([work contents]);
    workDescs[maxWorkTiles] = { 0, 0 };
    auto* variant = static_cast<VariantMeta*>([variants contents]);
    variant[0] = {
        1,
        0,
        7u << 26u,
        0,
        0,
        0,
        0,
        0,
    };

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    if (!command)
        return false;
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder)
        return false;
    [encoder setComputePipelineState:State->TextureRasterPipeline];
    [encoder setBuffer:header offset:0 atIndex:0];
    [encoder setBuffer:polygons offset:0 atIndex:1];
    [encoder setBuffer:xSpans offset:0 atIndex:2];
    [encoder setBuffer:work offset:0 atIndex:3];
    [encoder setBuffer:variants offset:0 atIndex:4];
    [encoder setBuffer:colors offset:0 atIndex:5];
    [encoder setBuffer:depths offset:0 atIndex:6];
    [encoder setBuffer:attrs offset:0 atIndex:7];
    [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:9];
    [encoder setBuffer:textureMemory offset:0 atIndex:10];
    [encoder setBuffer:texturePalette offset:0 atIndex:11];
    [encoder setBuffer:toon offset:0 atIndex:12];
    [encoder setTexture:State->DummyCapture128Texture atIndex:0];
    [encoder setTexture:State->DummyCapture256Texture atIndex:1];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding];

    if (!CompleteCommandBuffer(command, "texture-variant tile self-test"))
        return false;

    const auto* outColor =
        static_cast<const uint32_t*>([colors contents]);
    const auto* outDepth =
        static_cast<const uint32_t*>([depths contents]);
    const auto* outAttr =
        static_cast<const uint32_t*>([attrs contents]);
    const uint32_t expectedColor = 63u | (31u << 24u);
    uint32_t mismatchPixel = tileArea;
    for (uint32_t pixel = 0; pixel < tileArea; pixel++)
    {
        if (outColor[pixel] != expectedColor ||
            outDepth[pixel] != 0x123456u)
        {
            mismatchPixel = pixel;
            break;
        }
    }

    if (mismatchPixel != tileArea ||
        (outAttr[0] & 0x4u) == 0u ||
        (outAttr[(lineCount - 1u) * tileWidth] & 0x8u) == 0u)
    {
        const uint32_t reportPixel =
            mismatchPixel == tileArea ? 0u : mismatchPixel;
        std::fprintf(stderr,
            "[MelonPrime] metal compute textured raster: self-test mismatch "
            "pixel=%u color=%08x depth=%08x topAttr=%08x bottomAttr=%08x\n",
            reportPixel,
            outColor[reportPixel],
            outDepth[reportPixel],
            outAttr[0],
            outAttr[(lineCount - 1u) * tileWidth]);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute textured raster: self-test PASS "
        "formats=1..7 directRed=%08x pixels=%u\n",
        outColor[0], tileArea);
    return true;
}
