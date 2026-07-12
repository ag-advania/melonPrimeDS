// MELONPRIME_METAL_COMPUTE_COMPLETE_DEPTH_BLEND_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1
// MELONPRIME_METAL_COMPUTE_LEGACY_SUMMARY_RETIREMENT_V1

namespace
{


struct CompleteDepthBlendConfig
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t TileSize;
    uint32_t TilesPerLine;

    uint32_t BinStride;
    uint32_t PolygonGroups;
    uint32_t MaxWorkTiles;
    uint32_t TileWorkCapacity;

    uint32_t NumPolygons;
    uint32_t ClearColor;
    uint32_t ClearDepth;
    uint32_t ClearAttr;

    uint32_t DispCnt;
    uint32_t ClearBitmapX;
    uint32_t ClearBitmapY;
    uint32_t WBuffer;
};
static_assert(
    sizeof(CompleteDepthBlendConfig) == 64,
    "CompleteDepthBlendConfig must match MSL");

} // namespace

bool MetalComputeRenderer3D::RunCompleteDepthBlendSelfTest()
{
    if (!State || !State->Device || !State->Queue ||
        !State->CompleteDepthBlendPipeline)
    {
        return false;
    }

    constexpr uint32_t polygonCount = 3;
    constexpr uint32_t maxWorkTiles = 3;
    constexpr uint32_t tileCapacity = 3;
    constexpr uint32_t binStride = 64;

    std::array<uint32_t, binStride> fineMask {};
    std::array<uint32_t, binStride> workOffsets {};
    std::array<RenderPolygon, polygonCount> polygons {};
    std::array<uint32_t, maxWorkTiles> colorTiles {};
    std::array<uint32_t, maxWorkTiles> depthTiles {};
    std::array<uint32_t, maxWorkTiles> attrTiles {};

    polygons[0].Attr =
        (1u << 24u) |
        (31u << 16u);
    colorTiles[0] =
        63u |
        (31u << 24u);
    depthTiles[0] = 100u;

    polygons[1].Attr = 0x30u;
    colorTiles[1] = 0xFFFFFFFFu;
    depthTiles[1] = 200u;

    polygons[2].Attr =
        (2u << 24u) |
        (15u << 16u) |
        0x30u;
    colorTiles[2] =
        15u << 24u;
    depthTiles[2] = 50u;

    fineMask[0] = 0x7u;
    workOffsets[0] = 0u;

    const CompleteDepthBlendConfig config {
        1, 1, 1, 1,
        binStride, 1, maxWorkTiles, tileCapacity,
        polygonCount,
        0, 0xFFFFFFu, 0,
        0, 0, 0, 0,
    };

    id<MTLBuffer> fine =
        [State->Device
            newBufferWithBytes:fineMask.data()
                        length:sizeof(fineMask)
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> offsets =
        [State->Device
            newBufferWithBytes:workOffsets.data()
                        length:sizeof(workOffsets)
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> polygonBuffer =
        [State->Device
            newBufferWithBytes:polygons.data()
                        length:sizeof(polygons)
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> colors =
        [State->Device
            newBufferWithBytes:colorTiles.data()
                        length:sizeof(colorTiles)
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> depths =
        [State->Device
            newBufferWithBytes:depthTiles.data()
                        length:sizeof(depthTiles)
                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> attrs =
        [State->Device
            newBufferWithBytes:attrTiles.data()
                        length:sizeof(attrTiles)
                       options:MTLResourceStorageModeShared];

    id<MTLBuffer> resultColor =
        [State->Device
            newBufferWithLength:2u * sizeof(uint32_t)
                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> resultDepth =
        [State->Device
            newBufferWithLength:2u * sizeof(uint32_t)
                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> resultAttr =
        [State->Device
            newBufferWithLength:2u * sizeof(uint32_t)
                        options:MTLResourceStorageModeShared];

    const uint16_t zeroTexture = 0;
    id<MTLBuffer> textureMemory =
        [State->Device
            newBufferWithBytes:&zeroTexture
                        length:sizeof(zeroTexture)
                       options:MTLResourceStorageModeShared];

    if (!fine || !offsets || !polygonBuffer ||
        !colors || !depths || !attrs ||
        !resultColor || !resultDepth || !resultAttr ||
        !textureMemory)
    {
        return false;
    }

    std::memset([resultColor contents], 0, resultColor.length);
    std::memset([resultDepth contents], 0, resultDepth.length);
    std::memset([resultAttr contents], 0, resultAttr.length);

    id<MTLCommandBuffer> command =
        [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label =
        @"MelonPrime Metal Compute Complete DepthBlend Self-Test";

    id<MTLComputeCommandEncoder> encoder =
        [command computeCommandEncoder];
    if (!encoder)
        return false;
    [encoder
        setComputePipelineState:
            State->CompleteDepthBlendPipeline];
    [encoder setBuffer:fine offset:0 atIndex:0];
    [encoder setBuffer:offsets offset:0 atIndex:1];
    [encoder setBuffer:polygonBuffer offset:0 atIndex:2];
    [encoder setBuffer:colors offset:0 atIndex:3];
    [encoder setBuffer:depths offset:0 atIndex:4];
    [encoder setBuffer:attrs offset:0 atIndex:5];
    [encoder setBuffer:resultColor offset:0 atIndex:6];
    [encoder setBuffer:resultDepth offset:0 atIndex:7];
    [encoder setBuffer:resultAttr offset:0 atIndex:8];
    [encoder
        setBytes:&config
           length:sizeof(config)
          atIndex:10];
    [encoder setBuffer:textureMemory offset:0 atIndex:11];
    [encoder
        dispatchThreadgroups:MTLSizeMake(1, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
    [encoder endEncoding];

    if (!CompleteCommandBuffer(
            command,
            "complete depth-blend self-test"))
    {
        return false;
    }

    const auto* outputColor =
        static_cast<const uint32_t*>([resultColor contents]);
    const auto* outputDepth =
        static_cast<const uint32_t*>([resultDepth contents]);
    const auto* outputAttr =
        static_cast<const uint32_t*>([resultAttr contents]);

    const uint32_t expectedColor =
        31u |
        (31u << 24u);

    if (outputColor[0] != expectedColor ||
        outputDepth[0] != 100u ||
        (outputAttr[0] & (1u << 22u)) == 0u ||
        outputColor[1] != 0u ||
        outputDepth[1] != 0u ||
        outputAttr[1] != 0u)
    {
        std::fprintf(
            stderr,
            "[MelonPrime] metal compute complete depth blend: "
            "self-test mismatch top=%08x/%08x/%08x "
            "second=%08x/%08x/%08x\n",
            outputColor[0], outputDepth[0], outputAttr[0],
            outputColor[1], outputDepth[1], outputAttr[1]);
        return false;
    }

    std::fprintf(
        stderr,
        "[MelonPrime] metal compute complete depth blend: "
        "self-test PASS color=%08x depth=%08x attr=%08x\n",
        outputColor[0], outputDepth[0], outputAttr[0]);
    return true;
}
