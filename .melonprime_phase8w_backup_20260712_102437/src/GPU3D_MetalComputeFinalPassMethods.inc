// MELONPRIME_METAL_COMPUTE_FINAL_PASS_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_DIAGNOSTICS_CLEANUP_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1

namespace
{
constexpr uint32_t kFinalPassSummaryWords = 8u;
constexpr uint32_t kFinalTableWords = 8u + 34u;

struct FinalPassConfig
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t DispCnt;
    uint32_t ClearDepth;
    uint32_t ClearAttr;
    uint32_t FogOffset;
    uint32_t FogShift;
    uint32_t FogColor;
};
static_assert(sizeof(FinalPassConfig) == 32, "FinalPassConfig must match MSL");

struct FinalPassSummary
{
    uint32_t Pixels;
    uint32_t EdgePixels;
    uint32_t FogTopPixels;
    uint32_t FogSecondPixels;
    uint32_t AAPixels;
    uint32_t NonzeroAlpha;
    uint32_t ColorHash;
    uint32_t Reserved;
};
static_assert(sizeof(FinalPassSummary) == kFinalPassSummaryWords * sizeof(uint32_t));

uint32_t ConvertRGB555ToRGB6(uint32_t color)
{
    uint32_t r = (color << 1u) & 0x3Eu; if (r) r++;
    uint32_t g = (color >> 4u) & 0x3Eu; if (g) g++;
    uint32_t b = (color >> 9u) & 0x3Eu; if (b) b++;
    return r | (g << 8u) | (b << 16u);
}

uint32_t ConvertFogColorToRGB6A5(uint32_t color)
{
    return ConvertRGB555ToRGB6(color) |
           (((color >> 16u) & 0x1Fu) << 24u);
}
} // namespace

void* MetalComputeRenderer3D::GetComputeFinalTexture() const noexcept
{
    if (!State || !State->FinalPassReady)
        return nullptr;
    const int slot = State->SubmittedFinalSlot.load(std::memory_order_acquire);
    if (slot < 0 || slot >= static_cast<int>(kFrameSlotCount) ||
        !State->Slots[slot].FinalTexture)
    {
        return nullptr;
    }
    return (__bridge void*)State->Slots[slot].FinalTexture;
}

uint64_t MetalComputeRenderer3D::GetComputeFinalSerial() const noexcept
{
    return State
        ? State->SubmittedFinalSerial.load(std::memory_order_acquire)
        : 0u;
}

bool MetalComputeRenderer3D::ComputeFinalReady() const noexcept
{
    return State && State->FinalPassReady &&
           State->SubmittedFinalSlot.load(std::memory_order_acquire) >= 0;
}

bool MetalComputeRenderer3D::RunFinalPassSelfTest()
{
    if (!State || !State->Device || !State->Queue ||
        !State->FinalPassPipeline)
    {
        return false;
    }

    const uint32_t colors[2] = {
        63u | (31u << 24u),
        0u,
    };
    const uint32_t depths[2] = { 100u, 0xFFFFFFu };
    const uint32_t attrs[2] = { 0u, 0u };
    uint32_t tables[kFinalTableWords] = {};
    const FinalPassConfig config {
        1u, 1u, 0u, 0xFFFFFFu, 0u,
        0u, 0u, 0u,
    };

    id<MTLBuffer> colorBuffer = [State->Device
        newBufferWithBytes:colors length:sizeof(colors)
        options:MTLResourceStorageModeShared];
    id<MTLBuffer> depthBuffer = [State->Device
        newBufferWithBytes:depths length:sizeof(depths)
        options:MTLResourceStorageModeShared];
    id<MTLBuffer> attrBuffer = [State->Device
        newBufferWithBytes:attrs length:sizeof(attrs)
        options:MTLResourceStorageModeShared];
    id<MTLBuffer> tableBuffer = [State->Device
        newBufferWithBytes:tables length:sizeof(tables)
        options:MTLResourceStorageModeShared];

    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> finalTexture = [State->Device newTextureWithDescriptor:descriptor];
    if (!colorBuffer || !depthBuffer || !attrBuffer ||
        !tableBuffer || !finalTexture)
    {
        return false;
    }

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal Compute FinalPass Self-Test";

    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder)
        return false;
    [encoder setComputePipelineState:State->FinalPassPipeline];
    [encoder setBuffer:colorBuffer offset:0 atIndex:0];
    [encoder setBuffer:depthBuffer offset:0 atIndex:1];
    [encoder setBuffer:attrBuffer offset:0 atIndex:2];
    [encoder setBuffer:tableBuffer offset:0 atIndex:4];
    [encoder setBytes:&config length:sizeof(config) atIndex:5];
    [encoder setTexture:finalTexture atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
    [encoder endEncoding];

    if (!CompleteCommandBuffer(command, "final-pass self-test"))
        return false;

    uint8_t pixel[4] = {};
    [finalTexture getBytes:pixel
               bytesPerRow:sizeof(pixel)
                fromRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0];
    // BGRA8 raw storage: shader red is byte 2.
    if (pixel[2] < 248u || pixel[0] > 4u || pixel[1] > 4u ||
        pixel[3] < 248u)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute final pass: self-test mismatch "
            "bgra=%u,%u,%u,%u\n",
            pixel[0], pixel[1], pixel[2], pixel[3]);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute final pass: self-test PASS "
        "bgra=%u,%u,%u,%u\n",
        pixel[0], pixel[1], pixel[2], pixel[3]);
    return true;
}
