// MelonPrimeDS - Metal compute renderer foundation (Phase 7A/7B)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_MetalCompute.h"
#include "GPU3D_TexcacheMetal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace melonDS
{

namespace
{

constexpr uint32_t kMaxVariants = 256;
constexpr uint32_t kRasteriseChunkSize = 32768;
constexpr uint32_t kVariantWorkCountStart = 0;
constexpr uint32_t kSortedWorkOffsetStart = kVariantWorkCountStart + kMaxVariants * 4;
constexpr uint32_t kVariantWorkRealCountStart = kSortedWorkOffsetStart + kMaxVariants;
constexpr uint32_t kSortWorkCountStart = kVariantWorkRealCountStart + kMaxVariants;
constexpr uint32_t kBinHeaderWords = kSortWorkCountStart + 4;

struct FoundationConfig
{
    uint32_t VariantCount;
    uint32_t MaxWorkTiles;
    uint32_t CoarseTileCount;
    uint32_t RasteriseChunkSize;
};
static_assert(sizeof(FoundationConfig) == 16, "MSL FoundationConfig layout mismatch");

struct WorkDesc
{
    uint32_t Position;
    uint32_t PolygonAndOffset;
};
static_assert(sizeof(WorkDesc) == 8, "MSL uint2 layout mismatch");

static constexpr const char* kMetalComputeFoundationSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

constant uint MaxVariants = 256u;
constant uint VariantWorkCountStart = 0u;
constant uint SortedWorkOffsetStart = VariantWorkCountStart + MaxVariants * 4u;
constant uint VariantWorkRealCountStart = SortedWorkOffsetStart + MaxVariants;
constant uint SortWorkCountStart = VariantWorkRealCountStart + MaxVariants;

struct FoundationConfig
{
    uint variantCount;
    uint maxWorkTiles;
    uint coarseTileCount;
    uint rasteriseChunkSize;
};

kernel void mp_compute_clear_indirect(
    device atomic_uint* header [[buffer(0)]],
    constant FoundationConfig& config [[buffer(1)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.variantCount)
        return;

    const uint base = VariantWorkCountStart + gid * 4u;
    atomic_store_explicit(&header[base + 0u], 1u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 1u], 1u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 2u], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 3u], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[SortedWorkOffsetStart + gid], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[VariantWorkRealCountStart + gid], 0u, memory_order_relaxed);

    if (gid < 4u)
        atomic_store_explicit(&header[SortWorkCountStart + gid], 0u, memory_order_relaxed);
}

kernel void mp_compute_clear_coarse_mask(
    device uint* coarseMask [[buffer(0)]],
    constant FoundationConfig& config [[buffer(1)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.coarseTileCount)
        return;

    // GL's CoarseBinStride is two uint words for 2048 polygons.
    coarseMask[gid * 2u + 0u] = 0u;
    coarseMask[gid * 2u + 1u] = 0u;
}

kernel void mp_compute_calc_offsets(
    device atomic_uint* header [[buffer(0)]],
    constant FoundationConfig& config [[buffer(1)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.variantCount)
        return;

    const uint variantBase = VariantWorkCountStart + gid * 4u;
    const uint realCount = atomic_load_explicit(&header[variantBase + 2u], memory_order_relaxed);

    if (gid == 0u)
    {
        const uint globalCount = atomic_load_explicit(
            &header[VariantWorkCountStart + 3u], memory_order_relaxed);
        const uint clampedCount = min(globalCount, config.maxWorkTiles);
        atomic_store_explicit(&header[SortWorkCountStart + 0u],
                              (clampedCount + 31u) / 32u,
                              memory_order_relaxed);
        atomic_store_explicit(&header[SortWorkCountStart + 1u], 1u, memory_order_relaxed);
        atomic_store_explicit(&header[SortWorkCountStart + 2u], 1u, memory_order_relaxed);
        atomic_store_explicit(&header[SortWorkCountStart + 3u], 0u, memory_order_relaxed);
    }

    // VariantWorkCount[1].w is retained as the shared sorted-offset allocator,
    // matching the GL compute renderer's BinResultHeader contract.
    const uint sortedOffset = atomic_fetch_add_explicit(
        &header[VariantWorkCountStart + 1u * 4u + 3u],
        realCount,
        memory_order_relaxed);
    atomic_store_explicit(&header[SortedWorkOffsetStart + gid], sortedOffset, memory_order_relaxed);
    atomic_store_explicit(&header[VariantWorkRealCountStart + gid], realCount, memory_order_relaxed);

    // Fork Fix D: preserve the real work count, and materialize a bounded Y/Z
    // dispatch pair. The future raster kernel reconstructs a linear index from
    // these two dimensions and rejects the over-dispatched tail.
    const uint chunk = max(config.rasteriseChunkSize, 1u);
    atomic_store_explicit(&header[variantBase + 1u],
                          (realCount + chunk - 1u) / chunk,
                          memory_order_relaxed);
    atomic_store_explicit(&header[variantBase + 2u],
                          min(realCount, chunk),
                          memory_order_relaxed);
}

kernel void mp_compute_sort_work(
    device atomic_uint* header [[buffer(0)]],
    device const uint* polygonVariants [[buffer(1)]],
    device uint2* workDescs [[buffer(2)]],
    constant FoundationConfig& config [[buffer(3)]],
    uint gid [[thread_position_in_grid]])
{
    const uint globalCount = atomic_load_explicit(
        &header[VariantWorkCountStart + 3u], memory_order_relaxed);

    // Fork Fix E: never read or write beyond the allocated work-tile budget.
    if (gid >= min(globalCount, config.maxWorkTiles))
        return;

    const uint2 workDesc = workDescs[gid];
    const uint inVariantOffset = workDesc.y >> 11u;
    const uint polygonIndex = workDesc.y & 0x7FFu;
    const uint variantIndex = polygonVariants[polygonIndex];
    const uint sortedOffset = atomic_load_explicit(
        &header[SortedWorkOffsetStart + variantIndex], memory_order_relaxed);
    const uint sortedIndex = sortedOffset + inVariantOffset;

    workDescs[config.maxWorkTiles + sortedIndex] =
        uint2(workDesc.x, polygonIndex | (gid << 11u));
}
)MSL";

id<MTLComputePipelineState> BuildComputePipeline(
    id<MTLDevice> device,
    id<MTLLibrary> library,
    NSString* functionName)
{
    id<MTLFunction> function = [library newFunctionWithName:functionName];
    if (!function)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: missing function %s\n",
            [functionName UTF8String]);
        return nil;
    }

    NSError* error = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: pipeline %s failed: %s\n",
            [functionName UTF8String], message);
    }
    return pipeline;
}

bool CommandBufferCompleted(id<MTLCommandBuffer> commandBuffer, const char* stage)
{
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status == MTLCommandBufferStatusCompleted)
        return true;

    const char* message = commandBuffer.error
        ? [[commandBuffer.error localizedDescription] UTF8String]
        : "unknown command-buffer failure";
    std::fprintf(stderr,
        "[MelonPrime] metal compute foundation: %s failed: %s\n",
        stage, message);
    return false;
}

} // namespace

struct MetalComputeRenderer3D::MetalComputeState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLComputePipelineState> ClearIndirectPipeline = nil;
    id<MTLComputePipelineState> ClearCoarseMaskPipeline = nil;
    id<MTLComputePipelineState> CalcOffsetsPipeline = nil;
    id<MTLComputePipelineState> SortWorkPipeline = nil;
    bool Ready = false;
};

MetalComputeRenderer3D::MetalComputeRenderer3D(
    melonDS::GPU3D& gpu3D,
    SoftRenderer& parent) noexcept
    : Renderer3D(gpu3D),
      RasterReference(gpu3D, parent),
      State(std::make_unique<MetalComputeState>())
{
}

MetalComputeRenderer3D::~MetalComputeRenderer3D() = default;

bool MetalComputeRenderer3D::Init()
{
    if (!RasterReference.Init())
        return false;
    if (!CreateComputeFoundation())
        return false;
    if (!RunFoundationSelfTest())
        return false;

    State->Ready = true;
    return true;
}

bool MetalComputeRenderer3D::CreateComputeFoundation()
{
    if (!State)
        return false;

    id<MTLTexture> rasterTarget =
        (__bridge id<MTLTexture>)RasterReference.GetColorTargetTexture();
    State->Device = rasterTarget ? rasterTarget.device : MTLCreateSystemDefaultDevice();
    if (!State->Device)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: MTLCreateSystemDefaultDevice returned nil\n");
        return false;
    }

    State->Queue = [State->Device newCommandQueue];
    if (!State->Queue)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: newCommandQueue failed\n");
        return false;
    }

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kMetalComputeFoundationSource];
    State->Library = [State->Device newLibraryWithSource:source
                                                 options:nil
                                                   error:&error];
    if (!State->Library)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: MSL compile failed: %s\n",
            message);
        return false;
    }

    State->ClearIndirectPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_clear_indirect");
    State->ClearCoarseMaskPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_clear_coarse_mask");
    State->CalcOffsetsPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_calc_offsets");
    State->SortWorkPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_sort_work");

    if (!State->ClearIndirectPipeline || !State->ClearCoarseMaskPipeline ||
        !State->CalcOffsetsPipeline || !State->SortWorkPipeline)
    {
        return false;
    }

    const NSUInteger minMaxThreads = std::min({
        State->ClearIndirectPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearCoarseMaskPipeline.maxTotalThreadsPerThreadgroup,
        State->CalcOffsetsPipeline.maxTotalThreadsPerThreadgroup,
        State->SortWorkPipeline.maxTotalThreadsPerThreadgroup,
    });
    if (minMaxThreads < 32)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: device supports only %zu threads per group; 32 required\n",
            static_cast<size_t>(minMaxThreads));
        return false;
    }

    return true;
}

bool MetalComputeRenderer3D::RunFoundationSelfTest()
{
    if (!State || !State->Device || !State->Queue)
        return false;

    constexpr uint32_t variantCount = 3;
    constexpr uint32_t maxWorkTiles = 8;
    constexpr uint32_t coarseTileCount = 5;
    constexpr uint32_t polygonCount = 4;

    const FoundationConfig config {
        variantCount,
        maxWorkTiles,
        coarseTileCount,
        kRasteriseChunkSize,
    };

    id<MTLBuffer> headerBuffer =
        [State->Device newBufferWithLength:kBinHeaderWords * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> coarseMaskBuffer =
        [State->Device newBufferWithLength:coarseTileCount * 2 * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> polygonVariantBuffer =
        [State->Device newBufferWithLength:polygonCount * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> workDescBuffer =
        [State->Device newBufferWithLength:maxWorkTiles * 2 * sizeof(WorkDesc)
                                   options:MTLResourceStorageModeShared];

    if (!headerBuffer || !coarseMaskBuffer || !polygonVariantBuffer || !workDescBuffer)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: self-test buffer allocation failed\n");
        return false;
    }

    std::memset([headerBuffer contents], 0xCD, headerBuffer.length);
    std::memset([coarseMaskBuffer contents], 0xA5, coarseMaskBuffer.length);
    std::memset([polygonVariantBuffer contents], 0, polygonVariantBuffer.length);
    std::memset([workDescBuffer contents], 0, workDescBuffer.length);

    id<MTLCommandBuffer> clearCommand = [State->Queue commandBuffer];
    if (!clearCommand)
        return false;

    {
        id<MTLComputeCommandEncoder> encoder = [clearCommand computeCommandEncoder];
        [encoder setComputePipelineState:State->ClearIndirectPipeline];
        [encoder setBuffer:headerBuffer offset:0 atIndex:0];
        [encoder setBytes:&config length:sizeof(config) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [clearCommand computeCommandEncoder];
        [encoder setComputePipelineState:State->ClearCoarseMaskPipeline];
        [encoder setBuffer:coarseMaskBuffer offset:0 atIndex:0];
        [encoder setBytes:&config length:sizeof(config) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    if (!CommandBufferCompleted(clearCommand, "clear self-test"))
        return false;

    auto* header = static_cast<uint32_t*>([headerBuffer contents]);
    auto* coarseMask = static_cast<uint32_t*>([coarseMaskBuffer contents]);
    for (uint32_t i = 0; i < variantCount; i++)
    {
        const uint32_t base = i * 4;
        if (header[base + 0] != 1 || header[base + 1] != 1 ||
            header[base + 2] != 0 || header[base + 3] != 0)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute foundation: clear-indirect self-test mismatch variant=%u\n",
                i);
            return false;
        }
    }
    for (uint32_t i = 0; i < coarseTileCount * 2; i++)
    {
        if (coarseMask[i] != 0)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute foundation: coarse-mask self-test mismatch word=%u\n",
                i);
            return false;
        }
    }

    // Seed the post-binning state expected by CalcOffsets and SortWork.
    header[0 * 4 + 2] = 2;
    header[1 * 4 + 2] = 1;
    header[2 * 4 + 2] = 1;
    header[0 * 4 + 3] = polygonCount;
    header[1 * 4 + 3] = 0;

    auto* polygonVariants = static_cast<uint32_t*>([polygonVariantBuffer contents]);
    polygonVariants[0] = 2;
    polygonVariants[1] = 0;
    polygonVariants[2] = 1;
    polygonVariants[3] = 0;

    auto* workDescs = static_cast<WorkDesc*>([workDescBuffer contents]);
    workDescs[0] = { 0x00100020u, 0u | (0u << 11) };
    workDescs[1] = { 0x00300040u, 1u | (0u << 11) };
    workDescs[2] = { 0x00500060u, 2u | (0u << 11) };
    workDescs[3] = { 0x00700080u, 3u | (1u << 11) };

    id<MTLCommandBuffer> workCommand = [State->Queue commandBuffer];
    if (!workCommand)
        return false;

    // Separate encoders are intentional: an encoder boundary represents the
    // global visibility point that glMemoryBarrier supplied in the GL backend.
    {
        id<MTLComputeCommandEncoder> encoder = [workCommand computeCommandEncoder];
        [encoder setComputePipelineState:State->CalcOffsetsPipeline];
        [encoder setBuffer:headerBuffer offset:0 atIndex:0];
        [encoder setBytes:&config length:sizeof(config) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [workCommand computeCommandEncoder];
        [encoder setComputePipelineState:State->SortWorkPipeline];
        [encoder setBuffer:headerBuffer offset:0 atIndex:0];
        [encoder setBuffer:polygonVariantBuffer offset:0 atIndex:1];
        [encoder setBuffer:workDescBuffer offset:0 atIndex:2];
        [encoder setBytes:&config length:sizeof(config) atIndex:3];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    if (!CommandBufferCompleted(workCommand, "offset/sort self-test"))
        return false;

    if (header[kSortWorkCountStart + 0] != 1 ||
        header[kSortWorkCountStart + 1] != 1 ||
        header[kSortWorkCountStart + 2] != 1)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: sort dispatch count mismatch %u,%u,%u\n",
            header[kSortWorkCountStart + 0],
            header[kSortWorkCountStart + 1],
            header[kSortWorkCountStart + 2]);
        return false;
    }

    for (uint32_t variant = 0; variant < variantCount; variant++)
    {
        const uint32_t expectedRealCount = variant == 0 ? 2u : 1u;
        if (header[kVariantWorkRealCountStart + variant] != expectedRealCount)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute foundation: real-count mismatch variant=%u value=%u\n",
                variant, header[kVariantWorkRealCountStart + variant]);
            return false;
        }
    }

    const std::array<uint32_t, polygonCount> inVariantOffsets { 0, 0, 0, 1 };
    for (uint32_t sourceIndex = 0; sourceIndex < polygonCount; sourceIndex++)
    {
        const uint32_t variant = polygonVariants[sourceIndex];
        const uint32_t sortedOffset = header[kSortedWorkOffsetStart + variant];
        const uint32_t destination = maxWorkTiles + sortedOffset + inVariantOffsets[sourceIndex];
        const WorkDesc& sorted = workDescs[destination];
        if (sorted.Position != workDescs[sourceIndex].Position ||
            (sorted.PolygonAndOffset & 0x7FFu) != sourceIndex ||
            (sorted.PolygonAndOffset >> 11u) != sourceIndex)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute foundation: sorted work mismatch source=%u destination=%u\n",
                sourceIndex, destination);
            return false;
        }
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute foundation: self-test PASS device=%s "
        "threadWidth=%zu maxThreads=%zu fixDChunk=%u fixEMaxWorkTiles=%u\n",
        [[State->Device name] UTF8String],
        static_cast<size_t>(State->SortWorkPipeline.threadExecutionWidth),
        static_cast<size_t>(State->SortWorkPipeline.maxTotalThreadsPerThreadgroup),
        kRasteriseChunkSize,
        maxWorkTiles);
    return true;
}

void MetalComputeRenderer3D::Reset()
{
    RasterReference.Reset();
}

void MetalComputeRenderer3D::SetThreaded(bool threaded) noexcept
{
    RasterReference.SetThreaded(threaded);
}

bool MetalComputeRenderer3D::IsThreaded() const noexcept
{
    return RasterReference.IsThreaded();
}

void MetalComputeRenderer3D::SetScaleFactor(int scale) noexcept
{
    RasterReference.SetScaleFactor(scale);
}

void MetalComputeRenderer3D::SetBetterPolygons(bool betterPolygons) noexcept
{
    RasterReference.SetBetterPolygons(betterPolygons);
}

void MetalComputeRenderer3D::RenderFrame()
{
    // Phase 7A keeps the validated raster renderer as the visible reference.
    // Subsequent slices replace this call stage-by-stage with compute output.
    RasterReference.RenderFrame();
}

void MetalComputeRenderer3D::FinishRendering()
{
    RasterReference.FinishRendering();
}

void MetalComputeRenderer3D::RestartFrame()
{
    RasterReference.RestartFrame();
}

u32* MetalComputeRenderer3D::GetLine(int line)
{
    return RasterReference.GetLine(line);
}

void* MetalComputeRenderer3D::GetColorTargetTexture() const noexcept
{
    return RasterReference.GetColorTargetTexture();
}

void* MetalComputeRenderer3D::GetNativeResolveTexture() const noexcept
{
    return RasterReference.GetNativeResolveTexture();
}

void* MetalComputeRenderer3D::GetCommandQueue() const noexcept
{
    return RasterReference.GetCommandQueue();
}

int MetalComputeRenderer3D::GetTargetWidth() const noexcept
{
    return RasterReference.GetTargetWidth();
}

int MetalComputeRenderer3D::GetTargetHeight() const noexcept
{
    return RasterReference.GetTargetHeight();
}

int MetalComputeRenderer3D::GetScaleFactor() const noexcept
{
    return RasterReference.GetScaleFactor();
}

Metal3DDiagnostics MetalComputeRenderer3D::GetLastDiagnostics() const noexcept
{
    return RasterReference.GetLastDiagnostics();
}

void MetalComputeRenderer3D::SetupRenderThread()
{
    RasterReference.SetupRenderThread();
}

void MetalComputeRenderer3D::EnableRenderThread()
{
    RasterReference.EnableRenderThread();
}

bool MetalComputeRenderer3D::FoundationReady() const noexcept
{
    return State && State->Ready;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
