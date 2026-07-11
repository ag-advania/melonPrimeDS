// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1

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
    constexpr uint32_t tileArea = 64;
    const SpanBinConfig spanConfig {
        1, 1, lineCount,
        8, 8, 8, 1, 1,
        8, 1, 64, 8,
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
    id<MTLBuffer> summary =
        [State->Device newBufferWithLength:sizeof(TileRasterSummary)
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
        !colors || !depths || !attrs || !summary ||
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
    std::memset([summary contents], 0, summary.length);

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
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        if (!encoder)
            return false;
        [encoder setComputePipelineState:State->ClearTileSummaryPipeline];
        [encoder setBuffer:summary offset:0 atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
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
        [encoder setBuffer:summary offset:0 atIndex:8];
        [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:9];
        [encoder setBuffer:textureMemory offset:0 atIndex:10];
        [encoder setBuffer:texturePalette offset:0 atIndex:11];
        [encoder setBuffer:toon offset:0 atIndex:12];
        [encoder setTexture:State->DummyCapture128Texture atIndex:0];
        [encoder setTexture:State->DummyCapture256Texture atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [encoder endEncoding];
    }

    if (!CompleteCommandBuffer(command, "texture-variant tile self-test"))
        return false;

    const auto* result =
        static_cast<const TileRasterSummary*>([summary contents]);
    const auto* outColor =
        static_cast<const uint32_t*>([colors contents]);
    const auto* outDepth =
        static_cast<const uint32_t*>([depths contents]);
    const uint32_t expectedColor = 63u | (31u << 24u);

    if (result->RasterisedWorkItems != 1 ||
        result->TexturedWorkItems != 1 ||
        result->CoveredPixels != 64 ||
        result->DepthMin != 0x123456 ||
        result->DepthMax != 0x123456 ||
        result->ColorHash == 0 ||
        outColor[0] != expectedColor ||
        outDepth[0] != 0x123456)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute textured raster: self-test mismatch "
            "work=%u textured=%u covered=%u hash=0x%08x "
            "depth=%08x..%08x color=%08x\n",
            result->RasterisedWorkItems,
            result->TexturedWorkItems,
            result->CoveredPixels,
            result->ColorHash,
            result->DepthMin,
            result->DepthMax,
            outColor[0]);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute textured raster: self-test PASS "
        "formats=1..7 directRed=%08x work=%u textured=%u covered=%u\n",
        outColor[0],
        result->RasterisedWorkItems,
        result->TexturedWorkItems,
        result->CoveredPixels);
    return true;
}
