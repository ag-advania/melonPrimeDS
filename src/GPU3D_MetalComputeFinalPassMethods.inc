// MELONPRIME_METAL_COMPUTE_FINAL_PASS_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_DIAGNOSTICS_CLEANUP_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1
// MELONPRIME_METAL_COMPUTE_LEGACY_SUMMARY_RETIREMENT_V1

namespace
{
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

struct NativeResolveConfig
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t Scale;
    uint32_t Reserved;
};
static_assert(sizeof(NativeResolveConfig) == 16,
              "NativeResolveConfig must match MSL");

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

void* MetalComputeRenderer3D::GetComputeNativeResolveTexture() const noexcept
{
    if (!State || !State->FinalPassReady)
        return nullptr;
    const int slot = State->SubmittedFinalSlot.load(std::memory_order_acquire);
    if (slot < 0 || slot >= static_cast<int>(kFrameSlotCount) ||
        !State->Slots[slot].NativeTexture)
    {
        return nullptr;
    }
    return (__bridge void*)State->Slots[slot].NativeTexture;
}

bool MetalComputeRenderer3D::ResolveComputeNativeLines()
{
    if (!State)
        return false;
    if (State->NativeLineReady)
        return true;

    const int slotIndex = State->NativeLineSlot;
    if (slotIndex < 0 || slotIndex >= static_cast<int>(kFrameSlotCount))
        return false;

    auto& slot = State->Slots[slotIndex];
    if (!slot.NativeColorBuffer)
        return false;

    // One wait per visible compute frame. The native resolve is encoded in the
    // same command buffer as the final pass, so no extra submission is needed.
    if (slot.LastCommand)
    {
        [slot.LastCommand waitUntilCompleted];
        if (slot.LastCommand.status != MTLCommandBufferStatusCompleted)
            return false;
    }

    std::memcpy(State->NativeLineBuffer.data(),
                [slot.NativeColorBuffer contents],
                State->NativeLineBuffer.size() * sizeof(uint32_t));
    State->NativeLineReady = true;
    return true;
}

bool MetalComputeRenderer3D::RunFinalPassSelfTest()
{
    if (!State || !State->Device || !State->Queue ||
        !State->FinalPassPipeline || !State->NativeResolvePipeline)
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
    id<MTLBuffer> finalColorBuffer = [State->Device
        newBufferWithLength:sizeof(uint32_t)
        options:MTLResourceStorageModeShared];

    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModeShared;
    id<MTLTexture> finalTexture = [State->Device newTextureWithDescriptor:descriptor];
    id<MTLTexture> nativeTexture = [State->Device newTextureWithDescriptor:descriptor];
    id<MTLBuffer> nativeColorBuffer = [State->Device
        newBufferWithLength:256u * 192u * sizeof(uint32_t)
        options:MTLResourceStorageModeShared];
    if (!colorBuffer || !depthBuffer || !attrBuffer ||
        !tableBuffer || !finalColorBuffer || !finalTexture ||
        !nativeTexture || !nativeColorBuffer)
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
    [encoder setBuffer:finalColorBuffer offset:0 atIndex:6];
    [encoder setTexture:finalTexture atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
    [encoder endEncoding];

    // Scale 1: the native resolve must reproduce the final colour exactly.
    const NativeResolveConfig resolveConfig { 1u, 1u, 1u, 0u };
    id<MTLComputeCommandEncoder> resolveEncoder = [command computeCommandEncoder];
    if (!resolveEncoder)
        return false;
    [resolveEncoder setComputePipelineState:State->NativeResolvePipeline];
    [resolveEncoder setBuffer:finalColorBuffer offset:0 atIndex:0];
    [resolveEncoder setBuffer:nativeColorBuffer offset:0 atIndex:1];
    [resolveEncoder setBytes:&resolveConfig length:sizeof(resolveConfig) atIndex:2];
    [resolveEncoder setTexture:nativeTexture atIndex:0];
    [resolveEncoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(256u * 192u, 64u), 1, 1)
                  threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [resolveEncoder endEncoding];

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

    const uint32_t expectedRgb6a5 = 63u | (31u << 24u);
    const uint32_t finalRgb6a5 =
        *static_cast<const uint32_t*>([finalColorBuffer contents]);
    const uint32_t nativeRgb6a5 =
        *static_cast<const uint32_t*>([nativeColorBuffer contents]);
    if (finalRgb6a5 != expectedRgb6a5 || nativeRgb6a5 != expectedRgb6a5)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute final pass: native-resolve self-test "
            "mismatch final=%08x native=%08x expected=%08x\n",
            finalRgb6a5, nativeRgb6a5, expectedRgb6a5);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute final pass: self-test PASS "
        "bgra=%u,%u,%u,%u nativeResolve=%08x\n",
        pixel[0], pixel[1], pixel[2], pixel[3], nativeRgb6a5);
    return true;
}
