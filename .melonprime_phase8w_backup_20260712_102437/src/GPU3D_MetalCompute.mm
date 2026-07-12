// MelonPrimeDS - Metal compute renderer texture-variant contract stage (Phase 7F)
// MELONPRIME_METAL_COMPUTE_TILE_MEMORY_V4
// MELONPRIME_METAL_COMPUTE_DEPTH_BLEND_V5
// MELONPRIME_METAL_COMPUTE_TEXTURE_VARIANTS_V6
// MELONPRIME_METAL_COMPUTE_HIRES_LATCH_V1
// MELONPRIME_METAL_COMPUTE_MSL_ADDRESS_SPACE_FIX_V1
// MELONPRIME_METAL_COMPUTE_GRACEFUL_DEGRADATION_V1
// MELONPRIME_METAL_COMPUTE_SCALE_SYNC_V1
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_HIGH_PERFORMANCE_V1
// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1
// MELONPRIME_METAL_COMPUTE_COMPLETE_DEPTH_BLEND_V1
// MELONPRIME_METAL_COMPUTE_FINAL_PASS_V1
// MELONPRIME_METAL_COMPUTE_VISIBLE_CUTOVER_V1
// MELONPRIME_METAL_COMPUTE_DEFAULT_VISIBLE_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_DIAGNOSTICS_CLEANUP_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_DIAGNOSTICS_CLEANUP_FIX_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_TELEMETRY_CLEANUP_V1
// MELONPRIME_METAL_COMPUTE_PRODUCTION_TELEMETRY_CLEANUP_FIX_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_MetalCompute.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace melonDS
{

namespace
{

constexpr uint32_t kMaxVariants = 256;
constexpr uint32_t kMaxPolygons = 2048;
constexpr uint32_t kMaxYSpanSetups = 6144 * 2;
constexpr uint32_t kRasteriseChunkSize = 32768;
constexpr uint32_t kBinStride = 2048 / 32;
constexpr uint32_t kCoarseBinStride = kBinStride / 32;
constexpr uint32_t kCoarseTileCountX = 8;
constexpr uint32_t kFrameSlotCount = 3;
constexpr size_t kTileMemoryBudgetBytes = 192u * 1024u * 1024u;
constexpr uint32_t kTileSummaryWords = 8;
constexpr uint32_t kDepthBlendSummaryWords = 8;



bool MetalComputeVisibleEnabled()
{
    // Metal Compute Shader is an explicitly selected renderer. Its completed
    // compute final texture is therefore the production visible source by
    // default. Keep runtime kill switches for emergency comparison/fallback.
    static const bool enabled = []() {
        const char* disable =
            std::getenv("MELONPRIME_METAL_COMPUTE_DISABLE_VISIBLE");
        if (disable && disable[0] == '1')
            return false;

        // Preserve the old variable as a compatibility override: unset/1 means
        // normal default-visible operation, while 0 forces RasterReference.
        const char* legacy =
            std::getenv("MELONPRIME_METAL_COMPUTE_VISIBLE");
        return !legacy || legacy[0] != '0';
    }();
    return enabled;
}


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

struct SpanBinConfig
{
    uint32_t NumPolygons;
    uint32_t NumVariants;
    uint32_t NumSetupIndices;
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t TileSize;
    uint32_t TilesPerLine;
    uint32_t TileLines;
    uint32_t CoarseTileCountX;
    uint32_t CoarseTileCountY;
    uint32_t CoarseTileW;
    uint32_t CoarseTileH;
    uint32_t MaxWorkTiles;
    uint32_t BinStride;
    uint32_t CoarseBinStride;
    uint32_t PolygonGroups;
    uint32_t AlphaRef;
    uint32_t DispCnt;
    uint32_t TileWorkCapacity;
    uint32_t Reserved;
};
static_assert(sizeof(SpanBinConfig) == 80, "MSL SpanBinConfig layout mismatch");

struct SpanSetupY
{
    int32_t Z0, Z1, W0, W1;
    int32_t ColorR0, ColorG0, ColorB0;
    int32_t ColorR1, ColorG1, ColorB1;
    int32_t TexcoordU0, TexcoordV0;
    int32_t TexcoordU1, TexcoordV1;

    int32_t I0, I1;
    int32_t Linear;
    int32_t IRecip;
    int32_t W0n, W0d, W1d;

    int32_t Increment;

    int32_t X0, X1, Y0, Y1;
    int32_t XMin, XMax;
    int32_t DxInitial;

    int32_t XCovIncr;
    uint32_t IsDummy;
};
static_assert(sizeof(SpanSetupY) == 124, "MSL SpanSetupY layout mismatch");

struct SpanSetupX
{
    int32_t X0, X1;
    int32_t EdgeLenL, EdgeLenR, EdgeCovL, EdgeCovR;
    int32_t XRecip;
    uint32_t Flags;
    int32_t Z0, Z1, W0, W1;
    int32_t ColorR0, ColorG0, ColorB0;
    int32_t ColorR1, ColorG1, ColorB1;
    int32_t TexcoordU0, TexcoordV0;
    int32_t TexcoordU1, TexcoordV1;
    int32_t CovLInitial, CovRInitial;
};
static_assert(sizeof(SpanSetupX) == 96, "MSL SpanSetupX layout mismatch");

struct SetupIndices
{
    uint16_t PolyIdx;
    uint16_t SpanIdxL;
    uint16_t SpanIdxR;
    uint16_t Y;
};
static_assert(sizeof(SetupIndices) == 8, "MSL SetupIndices layout mismatch");

struct RenderPolygon
{
    uint32_t FirstXSpan;
    int32_t YTop, YBot;
    int32_t XMin, XMax;
    int32_t XMinY, XMaxY;
    uint32_t Variant;
    uint32_t Attr;
    float TextureLayer;
};
static_assert(sizeof(RenderPolygon) == 40, "MSL RenderPolygon layout mismatch");

struct WorkDesc
{
    uint32_t Position;
    uint32_t PolygonAndOffset;
};
static_assert(sizeof(WorkDesc) == 8, "MSL uint2 layout mismatch");

struct VariantMeta
{
    uint32_t Textured;
    uint32_t BlendMode;
    uint32_t TexParam;
    uint32_t TexPalette;
    uint32_t CaptureKind;
    uint32_t CaptureLayer;
    uint32_t CaptureYOffset;
    uint32_t Reserved;
};
static_assert(sizeof(VariantMeta) == 32, "MSL VariantMeta layout mismatch");

struct TileRasterSummary
{
    uint32_t RasterisedWorkItems;
    uint32_t TexturedWorkItems;
    uint32_t ShadowMaskWorkItems;
    uint32_t SkippedCapacity;
    uint32_t CoveredPixels;
    uint32_t ColorHash;
    uint32_t DepthMin;
    uint32_t DepthMax;
};
static_assert(sizeof(TileRasterSummary) == kTileSummaryWords * sizeof(uint32_t),
              "MSL tile summary layout mismatch");

struct DepthBlendConfig
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    uint32_t TileSize;
    uint32_t TilesPerLine;
    uint32_t BinStride;
    uint32_t PolygonGroups;
    uint32_t MaxWorkTiles;
    uint32_t TileWorkCapacity;
    uint32_t ClearColor;
    uint32_t ClearDepth;
    uint32_t ClearAttr;
    uint32_t DispCnt;
};
static_assert(sizeof(DepthBlendConfig) == 48, "MSL DepthBlendConfig layout mismatch");

struct DepthBlendSummary
{
    uint32_t Pixels;
    uint32_t LayersTested;
    uint32_t LayersAccepted;
    uint32_t TranslucentLayers;
    uint32_t ColorHash;
    uint32_t DepthMin;
    uint32_t DepthMax;
    uint32_t Reserved;
};
static_assert(sizeof(DepthBlendSummary) == kDepthBlendSummaryWords * sizeof(uint32_t),
              "MSL DepthBlendSummary layout mismatch");

struct VariantKey
{
    uint32_t TexParam = 0;
    uint32_t TexPalette = 0;
    uint32_t BlendMode = 0;
    uint32_t Textured = 0;
    uint32_t CaptureKind = 0;
    uint32_t CaptureLayer = 0;
    uint32_t CaptureYOffset = 0;

    bool operator==(const VariantKey& other) const noexcept
    {
        return TexParam == other.TexParam &&
               TexPalette == other.TexPalette &&
               BlendMode == other.BlendMode &&
               Textured == other.Textured &&
               CaptureKind == other.CaptureKind &&
               CaptureLayer == other.CaptureLayer &&
               CaptureYOffset == other.CaptureYOffset;
    }
};

static constexpr const char* kMetalComputeSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

constant uint MaxVariants = 256u;
constant uint VariantWorkCountStart = 0u;
constant uint SortedWorkOffsetStart = VariantWorkCountStart + MaxVariants * 4u;
constant uint VariantWorkRealCountStart = SortedWorkOffsetStart + MaxVariants;
constant uint SortWorkCountStart = VariantWorkRealCountStart + MaxVariants;

constant uint XSpanSetup_Linear = 1u << 0u;
constant uint XSpanSetup_FillInside = 1u << 1u;
constant uint XSpanSetup_FillLeft = 1u << 2u;
constant uint XSpanSetup_FillRight = 1u << 3u;

struct FoundationConfig
{
    uint variantCount;
    uint maxWorkTiles;
    uint coarseTileCount;
    uint rasteriseChunkSize;
};

struct SpanBinConfig
{
    uint numPolygons;
    uint numVariants;
    uint numSetupIndices;
    uint screenWidth;
    uint screenHeight;
    uint tileSize;
    uint tilesPerLine;
    uint tileLines;
    uint coarseTileCountX;
    uint coarseTileCountY;
    uint coarseTileW;
    uint coarseTileH;
    uint maxWorkTiles;
    uint binStride;
    uint coarseBinStride;
    uint polygonGroups;
    uint alphaRef;
    uint dispCnt;
    uint tileWorkCapacity;
    uint reserved;
};

struct SpanSetupY
{
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;

    int I0, I1;
    int Linear;
    int IRecip;
    int W0n, W0d, W1d;

    int Increment;

    int X0, X1, Y0, Y1;
    int XMin, XMax;
    int DxInitial;

    int XCovIncr;
    uint IsDummy;
};

struct SpanSetupX
{
    int X0, X1;
    int EdgeLenL, EdgeLenR, EdgeCovL, EdgeCovR;
    int XRecip;
    uint Flags;
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;
    int CovLInitial, CovRInitial;
};

struct SetupIndices
{
    ushort PolyIdx;
    ushort SpanIdxL;
    ushort SpanIdxR;
    ushort Y;
};

struct RenderPolygon
{
    uint FirstXSpan;
    int YTop, YBot;
    int XMin, XMax;
    int XMinY, XMaxY;
    uint Variant;
    uint Attr;
    float TextureLayer;
};

struct VariantMeta
{
    uint Textured;
    uint BlendMode;
    uint TexParam;
    uint TexPalette;
    uint CaptureKind;
    uint CaptureLayer;
    uint CaptureYOffset;
    uint Reserved;
};


constant uint TileSummaryRasterised = 0u;
constant uint TileSummarySkippedTextured = 1u;
constant uint TileSummarySkippedShadow = 2u;
constant uint TileSummarySkippedCapacity = 3u;
constant uint TileSummaryCoveredPixels = 4u;
constant uint TileSummaryColorHash = 5u;
constant uint TileSummaryDepthMin = 6u;
constant uint TileSummaryDepthMax = 7u;


kernel void mp_compute_sort_work_polygons(
    device atomic_uint* header [[buffer(0)]],
    device const RenderPolygon* polygons [[buffer(1)]],
    device uint2* workDescs [[buffer(2)]],
    constant FoundationConfig& config [[buffer(3)]],
    uint gid [[thread_position_in_grid]])
{
    const uint globalCount = atomic_load_explicit(
        &header[VariantWorkCountStart + 3u], memory_order_relaxed);
    if (gid >= min(globalCount, config.maxWorkTiles))
        return;

    const uint2 workDesc = workDescs[gid];
    const uint inVariantOffset = workDesc.y >> 11u;
    const uint polygonIndex = workDesc.y & 0x7FFu;
    const uint variantIndex = polygons[polygonIndex].Variant;
    const uint sortedOffset = atomic_load_explicit(
        &header[SortedWorkOffsetStart + variantIndex], memory_order_relaxed);
    const uint sortedIndex = sortedOffset + inVariantOffset;
    workDescs[config.maxWorkTiles + sortedIndex] =
        uint2(workDesc.x, polygonIndex | (gid << 11u));
}

kernel void mp_compute_clear_indirect(
    device atomic_uint* header [[buffer(0)]],
    constant FoundationConfig& config [[buffer(1)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid < 4u)
        atomic_store_explicit(&header[SortWorkCountStart + gid], 0u, memory_order_relaxed);

    // VariantWorkCount[1].w is the global sorted-offset allocator even when a
    // frame has only one active variant, so always clear at least slots 0/1.
    if (gid >= max(config.variantCount, 2u))
        return;

    const uint base = VariantWorkCountStart + gid * 4u;
    atomic_store_explicit(&header[base + 0u], 1u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 1u], 1u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 2u], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[base + 3u], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[SortedWorkOffsetStart + gid], 0u, memory_order_relaxed);
    atomic_store_explicit(&header[VariantWorkRealCountStart + gid], 0u, memory_order_relaxed);
}

kernel void mp_compute_clear_coarse_mask(
    device uint* coarseMask [[buffer(0)]],
    constant FoundationConfig& config [[buffer(1)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.coarseTileCount)
        return;

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

    const uint sortedOffset = atomic_fetch_add_explicit(
        &header[VariantWorkCountStart + 1u * 4u + 3u],
        realCount,
        memory_order_relaxed);
    atomic_store_explicit(&header[SortedWorkOffsetStart + gid], sortedOffset, memory_order_relaxed);
    atomic_store_explicit(&header[VariantWorkRealCountStart + gid], realCount, memory_order_relaxed);

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

static inline int CalculateX(int dx, thread const SpanSetupY& span)
{
    int x = span.X0;
    if (span.X1 < span.X0)
        x -= dx >> 18;
    else
        x += dx >> 18;
    return clamp(x, span.XMin, span.XMax);
}

kernel void mp_compute_interp_spans_geometry(
    device const SetupIndices* setupIndices [[buffer(0)]],
    device const SpanSetupY* ySpans [[buffer(1)]],
    device SpanSetupX* xSpans [[buffer(2)]],
    constant SpanBinConfig& config [[buffer(3)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.numSetupIndices)
        return;

    const SetupIndices setup = setupIndices[gid];
    SpanSetupY spanL = ySpans[setup.SpanIdxL];
    SpanSetupY spanR = ySpans[setup.SpanIdxR];
    const int y = int(setup.Y);

    const int dxL = spanL.DxInitial + (y - spanL.Y0) * spanL.Increment;
    const int dxR = spanR.DxInitial + (y - spanR.Y0) * spanR.Increment;
    int xL = CalculateX(dxL, spanL);
    int xR = CalculateX(dxR, spanR);

    if (xL > xR)
    {
        const int tmpX = xL;
        xL = xR;
        xR = tmpX;
        const SpanSetupY tmpSpan = spanL;
        spanL = spanR;
        spanR = tmpSpan;
    }

    SpanSetupX out = {};
    out.X0 = xL;
    out.X1 = xR + 1;
    out.EdgeLenL = 1;
    out.EdgeLenR = 1;
    out.Flags = XSpanSetup_FillInside | XSpanSetup_FillLeft | XSpanSetup_FillRight;
    if (out.X1 != out.X0)
        out.XRecip = int((1u << 30u) / uint(out.X1 - out.X0));

    // Attribute fields are seeded from the two active edge spans. Exact DS
    // perspective interpolation is completed in Phase 7C before Rasterise is
    // allowed to become visible; BinCombined only consumes X0/X1 here.
    out.Z0 = spanL.Z0;
    out.W0 = spanL.W0;
    out.ColorR0 = spanL.ColorR0;
    out.ColorG0 = spanL.ColorG0;
    out.ColorB0 = spanL.ColorB0;
    out.TexcoordU0 = spanL.TexcoordU0;
    out.TexcoordV0 = spanL.TexcoordV0;

    out.Z1 = spanR.Z0;
    out.W1 = spanR.W0;
    out.ColorR1 = spanR.ColorR0;
    out.ColorG1 = spanR.ColorG0;
    out.ColorB1 = spanR.ColorB0;
    out.TexcoordU1 = spanR.TexcoordU0;
    out.TexcoordV1 = spanR.TexcoordV0;

    if (out.W0 == out.W1 && ((out.W0 | out.W1) & 0x7F) == 0)
        out.Flags |= XSpanSetup_Linear;

    xSpans[gid] = out;
}

static inline bool BinPolygon(
    device const RenderPolygon& polygon,
    int2 topLeft,
    int2 botRight,
    device const SpanSetupX* xSpans)
{
    if (polygon.YTop > botRight.y || polygon.YBot <= topLeft.y)
        return false;

    const int polygonHeight = polygon.YBot - polygon.YTop;
    const int maxInner = max(polygonHeight - 1, 0);
    const int polyInnerTopY = clamp(topLeft.y - polygon.YTop, 0, maxInner);
    const int polyInnerBotY = clamp(botRight.y - polygon.YTop, 0, maxInner);

    const SpanSetupX xspanTop = xSpans[polygon.FirstXSpan + uint(polyInnerTopY)];
    const SpanSetupX xspanBot = xSpans[polygon.FirstXSpan + uint(polyInnerBotY)];

    int minXL;
    if (polygon.XMinY >= topLeft.y && polygon.XMinY <= botRight.y)
        minXL = polygon.XMin;
    else
        minXL = min(xspanTop.X0, xspanBot.X0);
    if (minXL > botRight.x)
        return false;

    int maxXR;
    if (polygon.XMaxY >= topLeft.y && polygon.XMaxY <= botRight.y)
        maxXR = polygon.XMax;
    else
        maxXR = max(xspanTop.X1, xspanBot.X1) - 1;
    if (maxXR < topLeft.x)
        return false;

    return true;
}

kernel void mp_compute_bin_combined(
    device atomic_uint* header [[buffer(0)]],
    device const RenderPolygon* polygons [[buffer(1)]],
    device const SpanSetupX* xSpans [[buffer(2)]],
    device atomic_uint* coarseMask [[buffer(3)]],
    device uint* fineMask [[buffer(4)]],
    device uint* workOffsets [[buffer(5)]],
    device uint2* workDescs [[buffer(6)]],
    constant SpanBinConfig& config [[buffer(7)]],
    uint3 groupID [[threadgroup_position_in_grid]],
    uint localIdx [[thread_index_in_threadgroup]])
{
    threadgroup atomic_uint mergedMaskShared;
    if (localIdx == 0u)
        atomic_store_explicit(&mergedMaskShared, 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const uint groupIdx = groupID.x;
    const uint2 coarseTile = uint2(groupID.y, groupID.z);
    const int2 coarseTopLeft = int2(coarseTile) * int2(config.coarseTileW, config.coarseTileH);
    const int2 coarseBotRight = coarseTopLeft + int2(config.coarseTileW - 1u, config.coarseTileH - 1u);

    if (localIdx < 32u)
    {
        const uint polygonIdx = groupIdx * 32u + localIdx;
        if (polygonIdx < config.numPolygons &&
            BinPolygon(polygons[polygonIdx], coarseTopLeft, coarseBotRight, xSpans))
        {
            atomic_fetch_or_explicit(&mergedMaskShared, 1u << localIdx, memory_order_relaxed);
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint mergedMask = atomic_load_explicit(&mergedMaskShared, memory_order_relaxed);
    const uint2 fineTile = uint2(localIdx % config.coarseTileCountX,
                                 localIdx / config.coarseTileCountX);
    if (fineTile.y >= config.coarseTileCountY)
        return;

    const int2 fineTopLeft = coarseTopLeft + int2(fineTile) * int(config.tileSize);
    const int2 fineBotRight = fineTopLeft + int2(int(config.tileSize) - 1);

    uint binnedMask = 0u;
    while (mergedMask != 0u)
    {
        const uint bit = ctz(mergedMask);
        mergedMask &= ~(1u << bit);
        const uint polygonIdx = groupIdx * 32u + bit;
        if (polygonIdx < config.numPolygons &&
            BinPolygon(polygons[polygonIdx], fineTopLeft, fineBotRight, xSpans))
        {
            binnedMask |= 1u << bit;
        }
    }

    const uint linearTile = fineTile.x + fineTile.y * config.tilesPerLine +
        coarseTile.x * config.coarseTileCountX +
        coarseTile.y * config.tilesPerLine * config.coarseTileCountY;
    const uint maskIndex = linearTile * config.binStride + groupIdx;

    uint workOffset = 0u;
    if (binnedMask != 0u)
    {
        const uint requested = popcount(binnedMask);
        workOffset = atomic_fetch_add_explicit(
            &header[VariantWorkCountStart + 3u], requested, memory_order_relaxed);

        // Fork Fix E: preserve buffer bounds before publishing mask/offset.
        if (workOffset >= config.maxWorkTiles)
        {
            binnedMask = 0u;
        }
        else
        {
            const uint keepCount = config.maxWorkTiles - workOffset;
            while (popcount(binnedMask) > keepCount)
            {
                const uint topBit = 31u - clz(binnedMask);
                binnedMask &= ~(1u << topBit);
            }
        }
    }

    fineMask[maskIndex] = binnedMask;
    if (binnedMask == 0u)
        return;

    const uint coarseIndex = linearTile * config.coarseBinStride + (groupIdx >> 5u);
    atomic_fetch_or_explicit(&coarseMask[coarseIndex],
                             1u << (groupIdx & 31u),
                             memory_order_relaxed);
    workOffsets[maskIndex] = workOffset;

    const uint packedTilePosition = uint(fineTopLeft.x) | (uint(fineTopLeft.y) << 16u);
    uint localWork = 0u;
    while (binnedMask != 0u)
    {
        const uint bit = ctz(binnedMask);
        binnedMask &= ~(1u << bit);
        const uint polygonIdx = groupIdx * 32u + bit;
        const uint variantIdx = min(polygons[polygonIdx].Variant, config.numVariants - 1u);
        const uint inVariantOffset = atomic_fetch_add_explicit(
            &header[VariantWorkCountStart + variantIdx * 4u + 2u],
            1u,
            memory_order_relaxed);
        workDescs[workOffset + localWork] =
            uint2(packedTilePosition, polygonIdx | (inVariantOffset << 11u));
        localWork++;
    }
}

kernel void mp_compute_clear_tile_summary(
    device atomic_uint* summary [[buffer(0)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= 8u)
        return;
    const uint value = gid == TileSummaryDepthMin ? 0xFFFFFFFFu : 0u;
    atomic_store_explicit(&summary[gid], value, memory_order_relaxed);
}

kernel void mp_compute_rasterise_no_texture_tiles(
    device atomic_uint* header [[buffer(0)]],
    device const RenderPolygon* polygons [[buffer(1)]],
    device const SpanSetupX* xSpans [[buffer(2)]],
    device const uint2* workDescs [[buffer(3)]],
    device const VariantMeta* variants [[buffer(4)]],
    device uint* colorTiles [[buffer(5)]],
    device uint* depthTiles [[buffer(6)]],
    device uint* attrTiles [[buffer(7)]],
    device atomic_uint* summary [[buffer(8)]],
    constant SpanBinConfig& config [[buffer(9)]],
    uint gid [[thread_position_in_grid]])
{
    const uint rawWorkCount = atomic_load_explicit(
        &header[VariantWorkCountStart + 3u], memory_order_relaxed);
    const uint workCount = min(rawWorkCount, config.maxWorkTiles);
    if (gid >= workCount)
        return;

    const uint2 work = workDescs[config.maxWorkTiles + gid];
    const uint polygonIndex = work.y & 0x7FFu;
    const uint originalWorkIndex = work.y >> 11u;
    if (originalWorkIndex >= config.tileWorkCapacity)
    {
        atomic_fetch_add_explicit(&summary[TileSummarySkippedCapacity], 1u, memory_order_relaxed);
        return;
    }
    if (polygonIndex >= config.numPolygons || config.numVariants == 0u)
        return;

    const RenderPolygon polygon = polygons[polygonIndex];
    const uint variantIndex = min(polygon.Variant, config.numVariants - 1u);
    const VariantMeta variant = variants[variantIndex];
    if (variant.Textured != 0u)
    {
        atomic_fetch_add_explicit(&summary[TileSummarySkippedTextured], 1u, memory_order_relaxed);
        return;
    }
    if (variant.BlendMode == 4u)
    {
        atomic_fetch_add_explicit(&summary[TileSummarySkippedShadow], 1u, memory_order_relaxed);
        return;
    }

    const uint tileArea = config.tileSize * config.tileSize;
    const uint tileBase = originalWorkIndex * tileArea;
    for (uint pixel = 0u; pixel < tileArea; pixel++)
    {
        colorTiles[tileBase + pixel] = 0u;
        depthTiles[tileBase + pixel] = 0u;
        attrTiles[tileBase + pixel] = 0u;
    }

    const uint tileX = work.x & 0xFFFFu;
    const uint tileY = work.x >> 16u;
    uint covered = 0u;
    uint colorHash = 2166136261u;
    uint depthMin = 0xFFFFFFFFu;
    uint depthMax = 0u;
    const uint polyAlpha = (polygon.Attr >> 16u) & 0x1Fu;
    const bool wireframe = polyAlpha == 0u;

    for (uint localY = 0u; localY < config.tileSize; localY++)
    {
        const uint pixelY = tileY + localY;
        if (pixelY >= config.screenHeight || int(pixelY) < polygon.YTop || int(pixelY) >= polygon.YBot)
            continue;

        const int spanOffsetSigned = int(pixelY) - polygon.YTop;
        if (spanOffsetSigned < 0 || polygon.FirstXSpan + uint(spanOffsetSigned) >= config.numSetupIndices)
            continue;

        const SpanSetupX span = xSpans[polygon.FirstXSpan + uint(spanOffsetSigned)];
        const int insideStart = min(span.X0 + max(span.EdgeLenL, 0), span.X1);
        const int insideEnd = min(span.X1 - max(span.EdgeLenR, 0), span.X1);
        const int spanWidth = max(span.X1 - span.X0, 1);

        for (uint localX = 0u; localX < config.tileSize; localX++)
        {
            const uint pixelX = tileX + localX;
            if (pixelX >= config.screenWidth)
                continue;
            const int x = int(pixelX);
            if (x < span.X0 || x >= span.X1)
                continue;

            const bool insideLeft = x < insideStart;
            const bool insideRight = x >= insideEnd;
            const bool insideBody = !insideLeft && !insideRight;
            bool fill = (insideLeft && (span.Flags & XSpanSetup_FillLeft) != 0u) ||
                        (insideRight && (span.Flags & XSpanSetup_FillRight) != 0u) ||
                        (insideBody && (span.Flags & XSpanSetup_FillInside) != 0u);
            if (wireframe && insideBody && int(pixelY) != polygon.YTop && int(pixelY) != polygon.YBot - 1)
                fill = false;
            if (!fill)
                continue;

            uint attr = 0u;
            if (int(pixelY) == polygon.YTop)
                attr |= 0x4u;
            else if (int(pixelY) == polygon.YBot - 1)
                attr |= 0x8u;
            if (insideLeft)
                attr |= 0x1u | (31u << 8u);
            else if (insideRight)
                attr |= 0x2u | (31u << 8u);

            const uint factor = min(uint(max(x - span.X0, 0)) * 256u / uint(spanWidth), 256u);
            const uint inverse = 256u - factor;
            const int vr = (span.ColorR0 * int(inverse) + span.ColorR1 * int(factor)) >> 8;
            const int vg = (span.ColorG0 * int(inverse) + span.ColorG1 * int(factor)) >> 8;
            const int vb = (span.ColorB0 * int(inverse) + span.ColorB1 * int(factor)) >> 8;
            const uint r = min(uint(max(vr >> 3, 0)), 63u);
            const uint g = min(uint(max(vg >> 3, 0)), 63u);
            const uint b = min(uint(max(vb >> 3, 0)), 63u);
            const uint a = polyAlpha == 0u ? 31u : polyAlpha;
            if (a <= config.alphaRef)
                continue;

            const uint z0 = uint(max(span.Z0, 0));
            const uint z1 = uint(max(span.Z1, 0));
            const uint depth = z0 <= z1
                ? z0 + (((z1 - z0) * factor) >> 8u)
                : z1 + (((z0 - z1) * inverse) >> 8u);
            const uint color = r | (g << 8u) | (b << 16u) | (a << 24u);
            const uint tilePixel = tileBase + localY * config.tileSize + localX;
            colorTiles[tilePixel] = color;
            depthTiles[tilePixel] = depth;
            attrTiles[tilePixel] = attr;

            covered++;
            depthMin = min(depthMin, depth);
            depthMax = max(depthMax, depth);
            colorHash ^= color + pixelX * 0x9E3779B9u + pixelY * 0x85EBCA6Bu;
            colorHash *= 16777619u;
        }
    }

    atomic_fetch_add_explicit(&summary[TileSummaryRasterised], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&summary[TileSummaryCoveredPixels], covered, memory_order_relaxed);
    atomic_fetch_xor_explicit(&summary[TileSummaryColorHash], colorHash, memory_order_relaxed);
    if (covered != 0u)
    {
        atomic_fetch_min_explicit(&summary[TileSummaryDepthMin], depthMin, memory_order_relaxed);
        atomic_fetch_max_explicit(&summary[TileSummaryDepthMax], depthMax, memory_order_relaxed);
    }
}

constant uint DepthBlendSummaryPixels = 0u;
constant uint DepthBlendSummaryLayersTested = 1u;
constant uint DepthBlendSummaryLayersAccepted = 2u;
constant uint DepthBlendSummaryTranslucent = 3u;
constant uint DepthBlendSummaryColorHash = 4u;
constant uint DepthBlendSummaryDepthMin = 5u;
constant uint DepthBlendSummaryDepthMax = 6u;

struct DepthBlendConfig
{
    uint ScreenWidth;
    uint ScreenHeight;
    uint TileSize;
    uint TilesPerLine;
    uint BinStride;
    uint PolygonGroups;
    uint MaxWorkTiles;
    uint TileWorkCapacity;
    uint ClearColor;
    uint ClearDepth;
    uint ClearAttr;
    uint DispCnt;
};

kernel void mp_compute_clear_depth_blend_summary(
    device atomic_uint* summary [[buffer(0)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= 8u)
        return;
    atomic_store_explicit(&summary[gid],
        gid == DepthBlendSummaryDepthMin ? 0xFFFFFFFFu : 0u,
        memory_order_relaxed);
}

kernel void mp_compute_depth_blend_no_texture(
    device const uint* fineMask [[buffer(0)]],
    device const uint* workOffsets [[buffer(1)]],
    device const RenderPolygon* polygons [[buffer(2)]],
    device const uint* colorTiles [[buffer(3)]],
    device const uint* depthTiles [[buffer(4)]],
    device const uint* attrTiles [[buffer(5)]],
    device uint* resultColor [[buffer(6)]],
    device uint* resultDepth [[buffer(7)]],
    device uint* resultAttr [[buffer(8)]],
    device atomic_uint* summary [[buffer(9)]],
    constant DepthBlendConfig& config [[buffer(10)]],
    uint gid [[thread_position_in_grid]])
{
    const uint pixelCount = config.ScreenWidth * config.ScreenHeight;
    if (gid >= pixelCount)
        return;

    const uint x = gid % config.ScreenWidth;
    const uint y = gid / config.ScreenWidth;
    const uint tileX = x / config.TileSize;
    const uint tileY = y / config.TileSize;
    const uint linearTile = tileX + tileY * config.TilesPerLine;
    const uint tileInner = (y % config.TileSize) * config.TileSize + (x % config.TileSize);
    const uint tileArea = config.TileSize * config.TileSize;

    uint color = config.ClearColor;
    uint depth = config.ClearDepth;
    uint attr = config.ClearAttr;
    uint layersTested = 0u;
    uint layersAccepted = 0u;
    uint translucentLayers = 0u;

    for (uint groupIdx = 0u; groupIdx < config.PolygonGroups; groupIdx++)
    {
        const uint maskIndex = linearTile * config.BinStride + groupIdx;
        uint mask = fineMask[maskIndex];
        if (mask == 0u)
            continue;

        const uint workBase = workOffsets[maskIndex];
        uint ordinal = 0u;
        while (mask != 0u)
        {
            const uint bit = ctz(mask);
            mask &= ~(1u << bit);
            const uint workIndex = workBase + ordinal++;
            if (workIndex >= min(config.MaxWorkTiles, config.TileWorkCapacity))
                continue;

            const uint polygonIndex = groupIdx * 32u + bit;
            const RenderPolygon polygon = polygons[polygonIndex];
            const uint tilePixel = workIndex * tileArea + tileInner;
            const uint tileColor = colorTiles[tilePixel];
            if (tileColor == 0u)
                continue;

            layersTested++;
            const uint tileDepth = depthTiles[tilePixel];
            const uint tileAttr = attrTiles[tilePixel];
            const bool equalDepth = (polygon.Attr & (1u << 14u)) != 0u;
            const uint depthDiff = tileDepth > depth ? tileDepth - depth : depth - tileDepth;
            const bool depthPass = equalDepth ? depthDiff <= 0x200u : tileDepth < depth;
            if (!depthPass)
                continue;

            const uint sourceAlpha = (tileColor >> 24u) & 0x1Fu;
            if (sourceAlpha < 31u)
            {
                const uint alpha = sourceAlpha + 1u;
                const uint srcRB = tileColor & 0x003F003Fu;
                const uint srcG = tileColor & 0x00003F00u;
                const uint dstRB = color & 0x003F003Fu;
                const uint dstG = color & 0x00003F00u;
                const uint blendedRB = ((srcRB * alpha) + (dstRB * (32u - alpha))) >> 5u;
                const uint blendedG = ((srcG * alpha) + (dstG * (32u - alpha))) >> 5u;
                color = (blendedRB & 0x003F003Fu) |
                        (blendedG & 0x00003F00u) |
                        max(color & 0x1F000000u, tileColor & 0x1F000000u);
                translucentLayers++;
                if ((polygon.Attr & (1u << 11u)) != 0u)
                    depth = tileDepth;
            }
            else
            {
                color = tileColor;
                depth = tileDepth;
            }

            attr = (tileAttr & 0x0000FFFFu) | (polygon.Attr & 0x3F008000u);
            layersAccepted++;
        }
    }

    resultColor[gid] = color;
    resultDepth[gid] = depth;
    resultAttr[gid] = attr;

    atomic_fetch_add_explicit(&summary[DepthBlendSummaryPixels], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&summary[DepthBlendSummaryLayersTested], layersTested, memory_order_relaxed);
    atomic_fetch_add_explicit(&summary[DepthBlendSummaryLayersAccepted], layersAccepted, memory_order_relaxed);
    atomic_fetch_add_explicit(&summary[DepthBlendSummaryTranslucent], translucentLayers, memory_order_relaxed);
    atomic_fetch_xor_explicit(&summary[DepthBlendSummaryColorHash],
        color + gid * 0x9E3779B9u, memory_order_relaxed);
    atomic_fetch_min_explicit(&summary[DepthBlendSummaryDepthMin], depth, memory_order_relaxed);
    atomic_fetch_max_explicit(&summary[DepthBlendSummaryDepthMax], depth, memory_order_relaxed);
}

)MSL";

#include "GPU3D_MetalComputeTexturedShaders.inc"
#include "GPU3D_MetalComputeDepthBlendShaders.inc"
#include "GPU3D_MetalComputeFinalPassShaders.inc"

id<MTLComputePipelineState> BuildComputePipeline(
    id<MTLDevice> device,
    id<MTLLibrary> library,
    NSString* functionName)
{
    id<MTLFunction> function = [library newFunctionWithName:functionName];
    if (!function)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute: missing function %s\n",
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
            "[MelonPrime] metal compute: pipeline %s failed: %s\n",
            [functionName UTF8String], message);
    }
    return pipeline;
}

bool CompleteCommandBuffer(id<MTLCommandBuffer> commandBuffer, const char* stage)
{
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status == MTLCommandBufferStatusCompleted)
        return true;

    const char* message = commandBuffer.error
        ? [[commandBuffer.error localizedDescription] UTF8String]
        : "unknown command-buffer failure";
    std::fprintf(stderr,
        "[MelonPrime] metal compute: %s failed: %s\n",
        stage, message);
    return false;
}

uint32_t DispatchGroups(uint32_t count, uint32_t groupSize)
{
    return count == 0 ? 0 : (count + groupSize - 1) / groupSize;
}

void SetupAttrs(SpanSetupY& span, Polygon* poly, int from, int to)
{
    span.Z0 = poly->FinalZ[from];
    span.W0 = poly->FinalW[from];
    span.Z1 = poly->FinalZ[to];
    span.W1 = poly->FinalW[to];
    span.ColorR0 = poly->Vertices[from]->FinalColor[0];
    span.ColorG0 = poly->Vertices[from]->FinalColor[1];
    span.ColorB0 = poly->Vertices[from]->FinalColor[2];
    span.ColorR1 = poly->Vertices[to]->FinalColor[0];
    span.ColorG1 = poly->Vertices[to]->FinalColor[1];
    span.ColorB1 = poly->Vertices[to]->FinalColor[2];
    span.TexcoordU0 = poly->Vertices[from]->TexCoords[0];
    span.TexcoordV0 = poly->Vertices[from]->TexCoords[1];
    span.TexcoordU1 = poly->Vertices[to]->TexCoords[0];
    span.TexcoordV1 = poly->Vertices[to]->TexCoords[1];
}

void SetupYSpanDummy(
    RenderPolygon& renderPolygon,
    SpanSetupY& span,
    Polygon* poly,
    int vertex,
    int side,
    int32_t positions[10][2])
{
    span = {};
    int32_t x0 = positions[vertex][0];
    if (side)
    {
        span.DxInitial = -0x40000;
        x0--;
    }

    span.X0 = span.X1 = x0;
    span.XMin = span.XMax = x0;
    span.Y0 = span.Y1 = positions[vertex][1];

    if (span.XMin < renderPolygon.XMin)
    {
        renderPolygon.XMin = span.XMin;
        renderPolygon.XMinY = span.Y0;
    }
    if (span.XMax > renderPolygon.XMax)
    {
        renderPolygon.XMax = span.XMax;
        renderPolygon.XMaxY = span.Y0;
    }

    span.Linear = 1;
    span.IsDummy = 1;
    SetupAttrs(span, poly, vertex, vertex);
}

void SetupYSpan(
    RenderPolygon& renderPolygon,
    SpanSetupY& span,
    Polygon* poly,
    int from,
    int to,
    int side,
    int32_t positions[10][2])
{
    span = {};
    span.X0 = positions[from][0];
    span.X1 = positions[to][0];
    span.Y0 = positions[from][1];
    span.Y1 = positions[to][1];
    SetupAttrs(span, poly, from, to);

    int32_t minXY = 0;
    int32_t maxXY = 0;
    bool negative = false;
    if (span.X1 > span.X0)
    {
        span.XMin = span.X0;
        span.XMax = span.X1 - 1;
        minXY = span.Y0;
        maxXY = span.Y1;
    }
    else if (span.X1 < span.X0)
    {
        span.XMin = span.X1;
        span.XMax = span.X0 - 1;
        negative = true;
        minXY = span.Y1;
        maxXY = span.Y0;
    }
    else
    {
        span.XMin = span.X0;
        if (side)
            span.XMin--;
        span.XMax = span.XMin;
        minXY = maxXY = span.Y0;
    }

    if (span.XMin < renderPolygon.XMin)
    {
        renderPolygon.XMin = span.XMin;
        renderPolygon.XMinY = minXY;
    }
    if (span.XMax > renderPolygon.XMax)
    {
        renderPolygon.XMax = span.XMax;
        renderPolygon.XMaxY = maxXY;
    }

    const int32_t xlen = span.XMax + 1 - span.XMin;
    const int32_t ylen = span.Y1 - span.Y0;
    if (ylen == 0)
        span.Increment = 0;
    else if (ylen == xlen)
        span.Increment = 0x40000;
    else
    {
        const int32_t yrecip = (1 << 18) / ylen;
        span.Increment = (span.X1 - span.X0) * yrecip;
        if (span.Increment < 0)
            span.Increment = -span.Increment;
    }

    const bool xMajor = span.Increment > 0x40000;
    if (side)
    {
        if (xMajor)
            span.DxInitial = negative ? (0x20000 + 0x40000) : (span.Increment - 0x20000);
        else if (span.Increment != 0)
            span.DxInitial = negative ? 0x40000 : 0;
        else
            span.DxInitial = -0x40000;
    }
    else
    {
        if (xMajor)
            span.DxInitial = negative ? ((span.Increment - 0x20000) + 0x40000) : 0x20000;
        else if (span.Increment != 0)
            span.DxInitial = negative ? 0x40000 : 0;
    }

    if (xMajor)
    {
        if (side)
        {
            span.I0 = span.X0 - 1;
            span.I1 = span.X1 - 1;
        }
        else
        {
            span.I0 = span.X0;
            span.I1 = span.X1;
        }
        if (xlen != 0)
            span.XCovIncr = (ylen << 10) / xlen;
    }
    else
    {
        span.I0 = span.Y0;
        span.I1 = span.Y1;
    }

    span.IRecip = span.I0 != span.I1 ? (1 << 30) / (span.I1 - span.I0) : 0;
    span.Linear = (span.W0 == span.W1) && !(span.W0 & 0x7E) && !(span.W1 & 0x7E);

    if ((span.W0 & 0x1) && !(span.W1 & 0x1))
    {
        span.W0n = (span.W0 - 1) >> 1;
        span.W0d = (span.W0 + 1) >> 1;
        span.W1d = span.W1 >> 1;
    }
    else
    {
        span.W0n = span.W0 >> 1;
        span.W0d = span.W0 >> 1;
        span.W1d = span.W1 >> 1;
    }
}

} // namespace

struct MetalComputeRenderer3D::MetalComputeState
{
    struct FrameSlot
    {
        id<MTLBuffer> Header = nil;
        id<MTLBuffer> SetupIndices = nil;
        id<MTLBuffer> YSpans = nil;
        id<MTLBuffer> XSpans = nil;
        id<MTLBuffer> Polygons = nil;
        id<MTLBuffer> CoarseMask = nil;
        id<MTLBuffer> FineMask = nil;
        id<MTLBuffer> WorkOffsets = nil;
        id<MTLBuffer> WorkDescs = nil;
        id<MTLBuffer> VariantMetaBuffer = nil;
        id<MTLBuffer> TextureMemoryBuffer = nil;
        id<MTLBuffer> TexturePaletteBuffer = nil;
        id<MTLBuffer> ToonTableBuffer = nil;
        id<MTLBuffer> FinalTablesBuffer = nil;
        id<MTLBuffer> FinalPassSummaryBuffer = nil;
        id<MTLTexture> FinalTexture = nil;
        id<MTLCommandBuffer> LastCommand = nil;
        std::atomic<bool> InFlight { false };
        std::atomic<uint64_t> Generation { 0 };
    };

    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLLibrary> TexturedLibrary = nil;
    id<MTLLibrary> CompleteDepthBlendLibrary = nil;
    id<MTLLibrary> FinalPassLibrary = nil;
    id<MTLComputePipelineState> ClearIndirectPipeline = nil;
    id<MTLComputePipelineState> ClearCoarseMaskPipeline = nil;
    id<MTLComputePipelineState> CalcOffsetsPipeline = nil;
    id<MTLComputePipelineState> SortWorkPipeline = nil;
    id<MTLComputePipelineState> SortWorkPolygonsPipeline = nil;
    id<MTLComputePipelineState> InterpSpansPipeline = nil;
    id<MTLComputePipelineState> BinCombinedPipeline = nil;
    id<MTLComputePipelineState> ClearTileSummaryPipeline = nil;
    id<MTLComputePipelineState> RasteriseNoTextureTilesPipeline = nil;
    id<MTLComputePipelineState> TextureRasterPipeline = nil;
    id<MTLComputePipelineState> ClearCompleteDepthBlendSummaryPipeline = nil;
    id<MTLComputePipelineState> CompleteDepthBlendPipeline = nil;
    id<MTLComputePipelineState> ClearFinalPassSummaryPipeline = nil;
    id<MTLComputePipelineState> FinalPassPipeline = nil;
    id<MTLComputePipelineState> ClearDepthBlendSummaryPipeline = nil;
    id<MTLComputePipelineState> DepthBlendNoTexturePipeline = nil;

    id<MTLBuffer> ColorTiles = nil;
    id<MTLBuffer> DepthTiles = nil;
    id<MTLBuffer> AttrTiles = nil;
    id<MTLBuffer> TileSummary = nil;
    id<MTLBuffer> DepthBlendColor = nil;
    id<MTLBuffer> DepthBlendDepth = nil;
    id<MTLBuffer> DepthBlendAttr = nil;
    id<MTLBuffer> DepthBlendSummaryBuffer = nil;
    id<MTLTexture> DummyCapture128Texture = nil;
    id<MTLTexture> DummyCapture256Texture = nil;
    id<MTLTexture> Capture128Texture = nil;
    id<MTLTexture> Capture256Texture = nil;

    std::array<FrameSlot, kFrameSlotCount> Slots;
    std::vector<SpanSetupY> YSpanData;
    std::vector<SetupIndices> SetupIndexData;
    std::vector<RenderPolygon> PolygonData;
    std::vector<VariantKey> VariantData;
    std::vector<VariantMeta> VariantMetaData;

    uint32_t RequestedScaleFactor = 1;
    uint32_t ScaleFactor = 1;
    uint32_t ScreenWidth = 256;
    uint32_t ScreenHeight = 192;
    uint32_t TileSize = 8;
    uint32_t CoarseTileCountY = 4;
    uint32_t CoarseTileArea = 32;
    uint32_t CoarseTileW = 64;
    uint32_t CoarseTileH = 32;
    uint32_t TilesPerLine = 32;
    uint32_t TileLines = 24;
    uint32_t MaxWorkTiles = 32 * 24 * 16;
    uint32_t MaxSetupIndices = 64 * 2048;
    uint32_t TileWorkCapacity = 0;
    bool HiresCoordinates = false;
    bool Ready = false;
    bool SpanBinReady = false;
    bool TileRasterReady = false;
    bool DepthBlendReady = false;
    bool TextureVariantReady = false;
    bool FinalPassReady = false;
    bool CpuReadbackRequired = true;
    bool LastFrameComputeVisible = false;
    bool LastFrameComputeReused = false;
    bool LastFrameUseHiRes3D = false;
    bool VisibleCutoverDisabled = false;
    bool LoggedVisibleCutover = false;
    bool LoggedVisibleFallback = false;
    uint32_t LastFrameEngineALayer = 1;
    int LastFrameRenderedScale = 1;
    std::atomic<bool> ComputeVisibleFault { false };
    bool LoggedOverflow = false;
    std::atomic<int> SubmittedFinalSlot { -1 };
    std::atomic<int> PublishedFinalSlot { -1 };
    std::atomic<uint64_t> SubmittedFinalSerial { 0 };
    std::atomic<uint64_t> PublishedFinalSerial { 0 };

    std::atomic<uint64_t> SubmittedFrames { 0 };
    std::atomic<uint64_t> SkippedBusyFrames { 0 };
    std::atomic<uint64_t> SkippedTileBusyFrames { 0 };
    std::atomic<bool> TileMemoryInFlight { false };
};

#include "GPU3D_MetalComputeTexturedMethods.inc"
#include "GPU3D_MetalComputeDepthBlendMethods.inc"
#include "GPU3D_MetalComputeFinalPassMethods.inc"

MetalComputeRenderer3D::MetalComputeRenderer3D(
    melonDS::GPU3D& gpu3D,
    SoftRenderer& parent) noexcept
    : Renderer3D(gpu3D),
      RasterReference(gpu3D, parent),
      State(std::make_unique<MetalComputeState>())
{
}

MetalComputeRenderer3D::~MetalComputeRenderer3D()
{
    if (!State)
        return;
    for (auto& slot : State->Slots)
    {
        if (slot.LastCommand && slot.InFlight.load(std::memory_order_acquire))
        {
            [slot.LastCommand waitUntilCompleted];
            while (slot.InFlight.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
    }
}

bool MetalComputeRenderer3D::Init()
{
    const int requestedScale = State
        ? std::max(1, static_cast<int>(State->RequestedScaleFactor))
        : 1;
    RasterReference.SetScaleFactor(requestedScale);
    if (!RasterReference.Init())
        return false;
    // Re-assert after device/targets exist. This closes the pre-init settings
    // ordering gap and makes the visible fallback target deterministic.
    RasterReference.SetScaleFactor(requestedScale);
    if (RasterReference.GetScaleFactor() != requestedScale ||
        RasterReference.GetTargetWidth() != 256 * requestedScale ||
        RasterReference.GetTargetHeight() != 192 * requestedScale)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute scale sync: init mismatch requested=%d actualScale=%d target=%dx%d\n",
            requestedScale, RasterReference.GetScaleFactor(),
            RasterReference.GetTargetWidth(), RasterReference.GetTargetHeight());
        return false;
    }

    const bool visibleCutoverRequested = MetalComputeVisibleEnabled();
    if (!visibleCutoverRequested)
    {
        if (State)
        {
            State->Ready = false;
            State->SpanBinReady = false;
            State->TileRasterReady = false;
            State->DepthBlendReady = false;
            State->TextureVariantReady = false;
            State->FinalPassReady = false;
            State->TileMemoryInFlight.store(false, std::memory_order_release);
        }
        std::fprintf(stderr,
            "[MelonPrime] metal compute visible: disabled by runtime fallback; "
            "using Metal raster renderer only\n");
        return true;
    }

    auto continueWithRasterOnly = [this](const char* failedStage) -> bool {
        if (State)
        {
            State->Ready = false;
            State->SpanBinReady = false;
            State->TileRasterReady = false;
            State->DepthBlendReady = false;
            State->TextureVariantReady = false;
            State->FinalPassReady = false;
            State->TileMemoryInFlight.store(false, std::memory_order_release);
        }
        std::fprintf(stderr,
            "[MelonPrime] metal compute: foundation unavailable at %s; continuing with Metal raster visible source only\n",
            failedStage);
        return true;
    };

    if (!CreateComputeFoundation())
        return continueWithRasterOnly("CreateComputeFoundation");
    if (!ConfigureSpanBinResources(requestedScale))
        return continueWithRasterOnly("ConfigureSpanBinResources");
    if (!RunFoundationSelfTest())
        return continueWithRasterOnly("RunFoundationSelfTest");
    if (!RunSpanBinSelfTest())
        return continueWithRasterOnly("RunSpanBinSelfTest");
    if (!RunNoTextureTileSelfTest())
        return continueWithRasterOnly("RunNoTextureTileSelfTest");
    if (!RunTextureVariantTileSelfTest())
        return continueWithRasterOnly("RunTextureVariantTileSelfTest");
    if (!RunCompleteDepthBlendSelfTest())
        return continueWithRasterOnly("RunCompleteDepthBlendSelfTest");
    if (!RunFinalPassSelfTest())
        return continueWithRasterOnly("RunFinalPassSelfTest");

    State->Ready = true;
    State->SpanBinReady = true;
    State->TileRasterReady = true;
    State->DepthBlendReady = true;
    State->TextureVariantReady = true;
    State->FinalPassReady = true;
    std::fprintf(stderr,
        "[MelonPrime] metal compute: complete pipeline ready scale=%d "
        "target=%dx%d visibleCutover=%u diagnostics=production\n",
        requestedScale,
        RasterReference.GetTargetWidth(),
        RasterReference.GetTargetHeight(),
        visibleCutoverRequested ? 1u : 0u);
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
            "[MelonPrime] metal compute: MTLCreateSystemDefaultDevice returned nil\n");
        return false;
    }

    State->Queue =
        (__bridge id<MTLCommandQueue>)RasterReference.GetCommandQueue();
    if (!State->Queue || State->Queue.device != State->Device)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute: RasterReference command queue unavailable\n");
        return false;
    }

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kMetalComputeSource];
    State->Library = [State->Device newLibraryWithSource:source options:nil error:&error];
    if (!State->Library)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute: MSL compile failed: %s\n",
            message);
        return false;
    }

    NSString* texturedSource =
        [NSString stringWithUTF8String:kMetalComputeTexturedSource];
    State->TexturedLibrary =
        [State->Device newLibraryWithSource:texturedSource options:nil error:&error];
    if (!State->TexturedLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute textured raster: MSL compile failed: %s\n",
            message);
        return false;
    }

    NSString* completeDepthBlendSource =
        [NSString stringWithUTF8String:kMetalComputeCompleteDepthBlendSource];
    State->CompleteDepthBlendLibrary =
        [State->Device
            newLibraryWithSource:completeDepthBlendSource
                         options:nil
                           error:&error];
    if (!State->CompleteDepthBlendLibrary)
    {
        const char* message = error
            ? [[error localizedDescription] UTF8String]
            : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute complete depth blend: "
            "MSL compile failed: %s\n",
            message);
        return false;
    }

    NSString* finalPassSource =
        [NSString stringWithUTF8String:kMetalComputeFinalPassSource];
    State->FinalPassLibrary = [State->Device
        newLibraryWithSource:finalPassSource options:nil error:&error];
    if (!State->FinalPassLibrary)
    {
        const char* message = error
            ? [[error localizedDescription] UTF8String]
            : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute final pass: MSL compile failed: %s\n",
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
    State->SortWorkPolygonsPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_sort_work_polygons");
    State->InterpSpansPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_interp_spans_geometry");
    State->BinCombinedPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_bin_combined");
    State->ClearTileSummaryPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_clear_tile_summary");
    State->RasteriseNoTextureTilesPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_rasterise_no_texture_tiles");
    State->TextureRasterPipeline = BuildComputePipeline(
        State->Device,
        State->TexturedLibrary,
        @"mp_compute_rasterise_texture_variants");
    State->ClearCompleteDepthBlendSummaryPipeline = BuildComputePipeline(
        State->Device,
        State->CompleteDepthBlendLibrary,
        @"mp_compute_clear_complete_depth_blend_summary");
    State->CompleteDepthBlendPipeline = BuildComputePipeline(
        State->Device,
        State->CompleteDepthBlendLibrary,
        @"mp_compute_depth_blend_complete");
    State->ClearFinalPassSummaryPipeline = BuildComputePipeline(
        State->Device, State->FinalPassLibrary,
        @"mp_compute_clear_final_pass_summary");
    State->FinalPassPipeline = BuildComputePipeline(
        State->Device, State->FinalPassLibrary,
        @"mp_compute_final_pass");
    State->ClearDepthBlendSummaryPipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_clear_depth_blend_summary");
    State->DepthBlendNoTexturePipeline = BuildComputePipeline(
        State->Device, State->Library, @"mp_compute_depth_blend_no_texture");

    if (!State->ClearIndirectPipeline || !State->ClearCoarseMaskPipeline ||
        !State->CalcOffsetsPipeline || !State->SortWorkPipeline ||
        !State->SortWorkPolygonsPipeline || !State->InterpSpansPipeline ||
        !State->BinCombinedPipeline || !State->ClearTileSummaryPipeline ||
        !State->RasteriseNoTextureTilesPipeline ||
        !State->TextureRasterPipeline ||
        !State->ClearCompleteDepthBlendSummaryPipeline ||
        !State->CompleteDepthBlendPipeline ||
        !State->ClearFinalPassSummaryPipeline ||
        !State->FinalPassPipeline ||
        !State->ClearDepthBlendSummaryPipeline ||
        !State->DepthBlendNoTexturePipeline)
    {
        return false;
    }

    const NSUInteger minMaxThreads = std::min({
        State->ClearIndirectPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearCoarseMaskPipeline.maxTotalThreadsPerThreadgroup,
        State->CalcOffsetsPipeline.maxTotalThreadsPerThreadgroup,
        State->SortWorkPipeline.maxTotalThreadsPerThreadgroup,
        State->SortWorkPolygonsPipeline.maxTotalThreadsPerThreadgroup,
        State->InterpSpansPipeline.maxTotalThreadsPerThreadgroup,
        State->BinCombinedPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearTileSummaryPipeline.maxTotalThreadsPerThreadgroup,
        State->RasteriseNoTextureTilesPipeline.maxTotalThreadsPerThreadgroup,
        State->TextureRasterPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearCompleteDepthBlendSummaryPipeline.maxTotalThreadsPerThreadgroup,
        State->CompleteDepthBlendPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearFinalPassSummaryPipeline.maxTotalThreadsPerThreadgroup,
        State->FinalPassPipeline.maxTotalThreadsPerThreadgroup,
        State->ClearDepthBlendSummaryPipeline.maxTotalThreadsPerThreadgroup,
        State->DepthBlendNoTexturePipeline.maxTotalThreadsPerThreadgroup,
    });
    if (minMaxThreads < 64)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute: device supports only %zu threads per group; 64 required\n",
            static_cast<size_t>(minMaxThreads));
        return false;
    }

    auto makeDummyCapture = [&](NSUInteger layers) -> id<MTLTexture> {
        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:1
                                            height:1
                                         mipmapped:NO];
        descriptor.textureType = MTLTextureType2DArray;
        descriptor.arrayLength = layers;
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture =
            [State->Device newTextureWithDescriptor:descriptor];
        const uint32_t zero = 0;
        for (NSUInteger layer = 0; texture && layer < layers; layer++)
        {
            [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                       mipmapLevel:0
                             slice:layer
                         withBytes:&zero
                       bytesPerRow:sizeof(zero)
                     bytesPerImage:sizeof(zero)];
        }
        return texture;
    };

    State->DummyCapture128Texture = makeDummyCapture(16);
    State->DummyCapture256Texture = makeDummyCapture(4);
    State->Capture128Texture = State->DummyCapture128Texture;
    State->Capture256Texture = State->DummyCapture256Texture;
    if (!State->DummyCapture128Texture || !State->DummyCapture256Texture)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute textured raster: dummy capture allocation failed\n");
        return false;
    }

    return true;
}

bool MetalComputeRenderer3D::ConfigureSpanBinResources(int scale)
{
    if (!State || !State->Device)
        return false;

    scale = std::max(1, scale);
    for (auto& slot : State->Slots)
    {
        if (slot.LastCommand && slot.InFlight.load(std::memory_order_acquire))
        {
            [slot.LastCommand waitUntilCompleted];
            while (slot.InFlight.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
        slot.Generation.fetch_add(1, std::memory_order_acq_rel);
        slot.InFlight.store(false, std::memory_order_release);
        slot.LastCommand = nil;
    }

    State->ScaleFactor = static_cast<uint32_t>(scale);
    State->ScreenWidth = 256u * State->ScaleFactor;
    State->ScreenHeight = 192u * State->ScaleFactor;

    const uint32_t range = static_cast<uint32_t>((scale >= 5) + (scale >= 9));
    State->TileSize = 8u << range;
    State->CoarseTileCountY = 4u + ((range >> 1u) << 1u);
    State->CoarseTileArea = kCoarseTileCountX * State->CoarseTileCountY;
    State->CoarseTileW = kCoarseTileCountX * State->TileSize;
    State->CoarseTileH = State->CoarseTileCountY * State->TileSize;
    State->TilesPerLine = State->ScreenWidth / State->TileSize;
    State->TileLines = State->ScreenHeight / State->TileSize;
    State->MaxWorkTiles = State->TilesPerLine * State->TileLines * 16u;
    State->MaxSetupIndices = 64u * 2048u * State->ScaleFactor;

    State->YSpanData.resize(kMaxYSpanSetups);
    State->SetupIndexData.resize(State->MaxSetupIndices);
    State->PolygonData.resize(kMaxPolygons);
    State->VariantData.reserve(kMaxVariants);
    State->VariantMetaData.resize(kMaxVariants);

    const size_t tileCount = static_cast<size_t>(State->TilesPerLine) * State->TileLines;
    const size_t headerBytes = kBinHeaderWords * sizeof(uint32_t);
    const size_t setupBytes = static_cast<size_t>(State->MaxSetupIndices) * sizeof(SetupIndices);
    const size_t ySpanBytes = static_cast<size_t>(kMaxYSpanSetups) * sizeof(SpanSetupY);
    const size_t xSpanBytes = static_cast<size_t>(State->MaxSetupIndices) * sizeof(SpanSetupX);
    const size_t polygonBytes = static_cast<size_t>(kMaxPolygons) * sizeof(RenderPolygon);
    const size_t coarseBytes = tileCount * kCoarseBinStride * sizeof(uint32_t);
    const size_t fineBytes = tileCount * kBinStride * sizeof(uint32_t);
    const size_t workOffsetBytes = fineBytes;
    const size_t workDescBytes = static_cast<size_t>(State->MaxWorkTiles) * 2u * sizeof(WorkDesc);
    const size_t variantMetaBytes = static_cast<size_t>(kMaxVariants) * sizeof(VariantMeta);

    for (auto& slot : State->Slots)
    {
        slot.Header = [State->Device newBufferWithLength:headerBytes options:MTLResourceStorageModeShared];
        slot.SetupIndices = [State->Device newBufferWithLength:setupBytes options:MTLResourceStorageModeShared];
        slot.YSpans = [State->Device newBufferWithLength:ySpanBytes options:MTLResourceStorageModeShared];
        slot.XSpans = [State->Device newBufferWithLength:xSpanBytes options:MTLResourceStorageModeShared];
        slot.Polygons = [State->Device newBufferWithLength:polygonBytes options:MTLResourceStorageModeShared];
        slot.CoarseMask = [State->Device newBufferWithLength:coarseBytes options:MTLResourceStorageModeShared];
        slot.FineMask = [State->Device newBufferWithLength:fineBytes options:MTLResourceStorageModeShared];
        slot.WorkOffsets = [State->Device newBufferWithLength:workOffsetBytes options:MTLResourceStorageModeShared];
        slot.WorkDescs = [State->Device newBufferWithLength:workDescBytes options:MTLResourceStorageModeShared];
        slot.VariantMetaBuffer = [State->Device newBufferWithLength:variantMetaBytes options:MTLResourceStorageModeShared];
        slot.TextureMemoryBuffer =
            [State->Device newBufferWithLength:sizeof(GPU.VRAMFlat_Texture)
                                       options:MTLResourceStorageModeShared];
        slot.TexturePaletteBuffer =
            [State->Device newBufferWithLength:sizeof(GPU.VRAMFlat_TexPal)
                                       options:MTLResourceStorageModeShared];
        slot.ToonTableBuffer =
            [State->Device newBufferWithLength:32u * sizeof(uint32_t)
                                       options:MTLResourceStorageModeShared];
        slot.FinalTablesBuffer =
            [State->Device newBufferWithLength:kFinalTableWords * sizeof(uint32_t)
                                       options:MTLResourceStorageModeShared];
        slot.FinalPassSummaryBuffer =
            [State->Device newBufferWithLength:kFinalPassSummaryWords * sizeof(uint32_t)
                                       options:MTLResourceStorageModeShared];
        MTLTextureDescriptor* finalDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:State->ScreenWidth
                                            height:State->ScreenHeight
                                         mipmapped:NO];
        finalDescriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        finalDescriptor.storageMode = MTLStorageModePrivate;
        slot.FinalTexture = [State->Device newTextureWithDescriptor:finalDescriptor];

        if (!slot.Header || !slot.SetupIndices || !slot.YSpans || !slot.XSpans ||
            !slot.Polygons || !slot.CoarseMask || !slot.FineMask ||
            !slot.WorkOffsets || !slot.WorkDescs || !slot.VariantMetaBuffer ||
             !slot.TextureMemoryBuffer ||
            !slot.TexturePaletteBuffer || !slot.ToonTableBuffer ||
            !slot.FinalTablesBuffer || !slot.FinalPassSummaryBuffer ||
            !slot.FinalTexture)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: buffer allocation failed scale=%d\n",
                scale);
            return false;
        }
    }

    const size_t tileArea = static_cast<size_t>(State->TileSize) * State->TileSize;
    const size_t bytesPerWork = tileArea * sizeof(uint32_t) * 3u;
    uint32_t tileCapacity = State->MaxWorkTiles;
    if (bytesPerWork != 0)
    {
        const size_t budgetCapacity = std::max<size_t>(1, kTileMemoryBudgetBytes / bytesPerWork);
        tileCapacity = static_cast<uint32_t>(std::min<size_t>(tileCapacity, budgetCapacity));
    }

    State->ColorTiles = nil;
    State->DepthTiles = nil;
    State->AttrTiles = nil;
    while (tileCapacity > 0)
    {
        const size_t tileBytes = static_cast<size_t>(tileCapacity) * tileArea * sizeof(uint32_t);
        State->ColorTiles = [State->Device newBufferWithLength:tileBytes options:MTLResourceStorageModePrivate];
        State->DepthTiles = [State->Device newBufferWithLength:tileBytes options:MTLResourceStorageModePrivate];
        State->AttrTiles = [State->Device newBufferWithLength:tileBytes options:MTLResourceStorageModePrivate];
        if (State->ColorTiles && State->DepthTiles && State->AttrTiles)
            break;
        State->ColorTiles = nil;
        State->DepthTiles = nil;
        State->AttrTiles = nil;
        tileCapacity >>= 1u;
    }
    State->TileSummary = [State->Device newBufferWithLength:kTileSummaryWords * sizeof(uint32_t)
                                                    options:MTLResourceStorageModeShared];
    const size_t screenPixelBytes =
        static_cast<size_t>(State->ScreenWidth) *
        State->ScreenHeight *
        sizeof(uint32_t);
    const size_t twoLayerPixelBytes =
        screenPixelBytes * 2u;
    State->DepthBlendColor =
        [State->Device
            newBufferWithLength:twoLayerPixelBytes
                        options:MTLResourceStorageModePrivate];
    State->DepthBlendDepth =
        [State->Device
            newBufferWithLength:twoLayerPixelBytes
                        options:MTLResourceStorageModePrivate];
    State->DepthBlendAttr =
        [State->Device
            newBufferWithLength:twoLayerPixelBytes
                        options:MTLResourceStorageModePrivate];
    State->DepthBlendSummaryBuffer =
        [State->Device
            newBufferWithLength:
                kCompleteDepthBlendSummaryWords *
                sizeof(uint32_t)
                        options:MTLResourceStorageModeShared];
    if (tileCapacity == 0 || !State->ColorTiles || !State->DepthTiles ||
        !State->AttrTiles || !State->TileSummary ||
        !State->DepthBlendColor || !State->DepthBlendDepth ||
        !State->DepthBlendAttr || !State->DepthBlendSummaryBuffer)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute tile memory: allocation failed scale=%d maxWorkTiles=%u\n",
            scale, State->MaxWorkTiles);
        return false;
    }
    State->TileWorkCapacity = tileCapacity;
    State->SubmittedFinalSlot.store(-1, std::memory_order_release);
    State->PublishedFinalSlot.store(-1, std::memory_order_release);
    State->SubmittedFinalSerial.store(0, std::memory_order_release);
    State->PublishedFinalSerial.store(0, std::memory_order_release);
    State->TileMemoryInFlight.store(false, std::memory_order_release);

    const size_t allocatedTileBytes = static_cast<size_t>(tileCapacity) * tileArea * sizeof(uint32_t) * 3u;
    std::fprintf(stderr,
        "[MelonPrime] metal compute span/bin: configured scale=%d screen=%ux%u tile=%u grid=%ux%u maxWorkTiles=%u maxXSpans=%u tileCapacity=%u tileMemoryMiB=%.1f fullCoverage=%u slots=%u\n",
        scale,
        State->ScreenWidth,
        State->ScreenHeight,
        State->TileSize,
        State->TilesPerLine,
        State->TileLines,
        State->MaxWorkTiles,
        State->MaxSetupIndices,
        State->TileWorkCapacity,
        static_cast<double>(allocatedTileBytes) / (1024.0 * 1024.0),
        State->TileWorkCapacity == State->MaxWorkTiles ? 1u : 0u,
        kFrameSlotCount);
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
        variantCount, maxWorkTiles, coarseTileCount, kRasteriseChunkSize
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
        return false;

    std::memset([headerBuffer contents], 0, headerBuffer.length);
    std::memset([coarseMaskBuffer contents], 0xA5, coarseMaskBuffer.length);
    std::memset([polygonVariantBuffer contents], 0, polygonVariantBuffer.length);
    std::memset([workDescBuffer contents], 0, workDescBuffer.length);

    id<MTLCommandBuffer> clearCommand = [State->Queue commandBuffer];
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
    if (!CompleteCommandBuffer(clearCommand, "foundation clear self-test"))
        return false;

    auto* header = static_cast<uint32_t*>([headerBuffer contents]);
    for (uint32_t i = 0; i < variantCount; i++)
    {
        const uint32_t base = i * 4;
        if (header[base] != 1 || header[base + 1] != 1 ||
            header[base + 2] != 0 || header[base + 3] != 0)
            return false;
    }

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
    if (!CompleteCommandBuffer(workCommand, "foundation offset/sort self-test"))
        return false;

    if (header[kSortWorkCountStart] != 1 ||
        header[kVariantWorkRealCountStart + 0] != 2 ||
        header[kVariantWorkRealCountStart + 1] != 1 ||
        header[kVariantWorkRealCountStart + 2] != 1)
    {
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute foundation: self-test PASS device=%s threadWidth=%zu maxThreads=%zu fixDChunk=%u fixEMaxWorkTiles=%u\n",
        [[State->Device name] UTF8String],
        static_cast<size_t>(State->SortWorkPipeline.threadExecutionWidth),
        static_cast<size_t>(State->SortWorkPipeline.maxTotalThreadsPerThreadgroup),
        kRasteriseChunkSize,
        maxWorkTiles);
    return true;
}

bool MetalComputeRenderer3D::RunSpanBinSelfTest()
{
    if (!State || !State->Device || !State->Queue)
        return false;

    constexpr uint32_t setupCount = 16;
    constexpr uint32_t maxWorkTiles = 64;
    constexpr uint32_t tileCount = 32;

    std::array<SetupIndices, setupCount> setup = {};
    std::array<SpanSetupY, 2> yspans = {};
    std::array<SpanSetupX, setupCount> xspans = {};
    RenderPolygon polygon = {};

    yspans[0].X0 = yspans[0].X1 = 16;
    yspans[0].XMin = yspans[0].XMax = 16;
    yspans[0].Y0 = yspans[0].Y1 = 8;
    yspans[0].IsDummy = 1;
    yspans[1].X0 = yspans[1].X1 = 47;
    yspans[1].XMin = yspans[1].XMax = 47;
    yspans[1].Y0 = yspans[1].Y1 = 8;
    yspans[1].IsDummy = 1;

    for (uint32_t i = 0; i < setupCount; i++)
        setup[i] = { 0, 0, 1, static_cast<uint16_t>(8 + i) };

    polygon.FirstXSpan = 0;
    polygon.YTop = 8;
    polygon.YBot = 24;
    polygon.XMin = 16;
    polygon.XMax = 47;
    polygon.XMinY = 8;
    polygon.XMaxY = 8;
    polygon.Variant = 0;

    const SpanBinConfig spanConfig {
        1, 1, setupCount,
        64, 32, 8, 8, 4,
        8, 4, 64, 32,
        maxWorkTiles, kBinStride, kCoarseBinStride, 1,
        0, 0, maxWorkTiles, 0
    };
    const FoundationConfig foundationConfig { 1, maxWorkTiles, tileCount, kRasteriseChunkSize };

    id<MTLBuffer> header = [State->Device newBufferWithLength:kBinHeaderWords * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    id<MTLBuffer> setupBuffer = [State->Device newBufferWithBytes:setup.data() length:sizeof(setup) options:MTLResourceStorageModeShared];
    id<MTLBuffer> yBuffer = [State->Device newBufferWithBytes:yspans.data() length:sizeof(yspans) options:MTLResourceStorageModeShared];
    id<MTLBuffer> xBuffer = [State->Device newBufferWithBytes:xspans.data() length:sizeof(xspans) options:MTLResourceStorageModeShared];
    id<MTLBuffer> polygonBuffer = [State->Device newBufferWithBytes:&polygon length:sizeof(polygon) options:MTLResourceStorageModeShared];
    id<MTLBuffer> coarse = [State->Device newBufferWithLength:tileCount * kCoarseBinStride * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    id<MTLBuffer> fine = [State->Device newBufferWithLength:tileCount * kBinStride * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    id<MTLBuffer> offsets = [State->Device newBufferWithLength:tileCount * kBinStride * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    id<MTLBuffer> work = [State->Device newBufferWithLength:maxWorkTiles * 2 * sizeof(WorkDesc) options:MTLResourceStorageModeShared];
    if (!header || !setupBuffer || !yBuffer || !xBuffer || !polygonBuffer ||
        !coarse || !fine || !offsets || !work)
        return false;

    std::memset([header contents], 0, header.length);
    std::memset([coarse contents], 0, coarse.length);
    std::memset([fine contents], 0, fine.length);
    std::memset([offsets contents], 0, offsets.length);
    std::memset([work contents], 0, work.length);

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->ClearIndirectPipeline];
        [encoder setBuffer:header offset:0 atIndex:0];
        [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->ClearCoarseMaskPipeline];
        [encoder setBuffer:coarse offset:0 atIndex:0];
        [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->InterpSpansPipeline];
        [encoder setBuffer:setupBuffer offset:0 atIndex:0];
        [encoder setBuffer:yBuffer offset:0 atIndex:1];
        [encoder setBuffer:xBuffer offset:0 atIndex:2];
        [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:3];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->BinCombinedPipeline];
        [encoder setBuffer:header offset:0 atIndex:0];
        [encoder setBuffer:polygonBuffer offset:0 atIndex:1];
        [encoder setBuffer:xBuffer offset:0 atIndex:2];
        [encoder setBuffer:coarse offset:0 atIndex:3];
        [encoder setBuffer:fine offset:0 atIndex:4];
        [encoder setBuffer:offsets offset:0 atIndex:5];
        [encoder setBuffer:work offset:0 atIndex:6];
        [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:7];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->CalcOffsetsPipeline];
        [encoder setBuffer:header offset:0 atIndex:0];
        [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->SortWorkPolygonsPipeline];
        [encoder setBuffer:header offset:0 atIndex:0];
        [encoder setBuffer:polygonBuffer offset:0 atIndex:1];
        [encoder setBuffer:work offset:0 atIndex:2];
        [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:3];
        [encoder dispatchThreadgroups:MTLSizeMake(2, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    if (!CompleteCommandBuffer(command, "span/bin/offset/sort self-test"))
        return false;

    const auto* outX = static_cast<const SpanSetupX*>([xBuffer contents]);
    for (uint32_t i = 0; i < setupCount; i++)
    {
        if (outX[i].X0 != 16 || outX[i].X1 != 48)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: interp self-test mismatch line=%u x=%d..%d\n",
                i, outX[i].X0, outX[i].X1);
            return false;
        }
    }

    const auto* outHeader = static_cast<const uint32_t*>([header contents]);
    const auto* outFine = static_cast<const uint32_t*>([fine contents]);
    uint32_t nonzeroTiles = 0;
    for (uint32_t tile = 0; tile < tileCount; tile++)
    {
        if (outFine[tile * kBinStride] != 0)
            nonzeroTiles++;
    }
    const auto* outWork = static_cast<const WorkDesc*>([work contents]);
    uint32_t sortedItems = 0;
    for (uint32_t i = 0; i < maxWorkTiles; i++)
    {
        if (outWork[maxWorkTiles + i].Position != 0)
            sortedItems++;
    }
    if (outHeader[3] != 8 ||
        outHeader[kVariantWorkRealCountStart] != 8 ||
        nonzeroTiles != 8 || sortedItems != 8)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute span/bin: self-test mismatch global=%u real=%u tiles=%u sorted=%u\n",
            outHeader[3],
            outHeader[kVariantWorkRealCountStart],
            nonzeroTiles,
            sortedItems);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute span/bin: self-test PASS rectangle=16,8..48,24 workTiles=%u sorted=%u\n",
        outHeader[3], sortedItems);
    return true;
}

bool MetalComputeRenderer3D::RunNoTextureTileSelfTest()
{
    if (!State || !State->Device || !State->Queue ||
        !State->ClearTileSummaryPipeline || !State->RasteriseNoTextureTilesPipeline)
        return false;

    constexpr uint32_t maxWorkTiles = 1;
    constexpr uint32_t lineCount = 8;
    constexpr uint32_t tileArea = 64;
    const SpanBinConfig spanConfig {
        1, 1, lineCount, 8, 8, 8, 1, 1,
        8, 1, 64, 8, maxWorkTiles, kBinStride, kCoarseBinStride, 1,
        0, 0, maxWorkTiles, 0,
    };

    id<MTLBuffer> header = [State->Device newBufferWithLength:kBinHeaderWords * sizeof(uint32_t)
                                                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> polygons = [State->Device newBufferWithLength:sizeof(RenderPolygon)
                                                           options:MTLResourceStorageModeShared];
    id<MTLBuffer> xSpans = [State->Device newBufferWithLength:lineCount * sizeof(SpanSetupX)
                                                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> work = [State->Device newBufferWithLength:maxWorkTiles * 2u * sizeof(WorkDesc)
                                                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> variants = [State->Device newBufferWithLength:sizeof(VariantMeta)
                                                           options:MTLResourceStorageModeShared];
    id<MTLBuffer> colors = [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> depths = [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                                         options:MTLResourceStorageModeShared];
    id<MTLBuffer> attrs = [State->Device newBufferWithLength:tileArea * sizeof(uint32_t)
                                                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> summary = [State->Device newBufferWithLength:sizeof(TileRasterSummary)
                                                          options:MTLResourceStorageModeShared];
    if (!header || !polygons || !xSpans || !work || !variants ||
        !colors || !depths || !attrs || !summary)
        return false;

    std::memset([header contents], 0, header.length);
    std::memset([polygons contents], 0, polygons.length);
    std::memset([xSpans contents], 0, xSpans.length);
    std::memset([work contents], 0, work.length);
    std::memset([variants contents], 0, variants.length);
    std::memset([colors contents], 0xA5, colors.length);
    std::memset([depths contents], 0xA5, depths.length);
    std::memset([attrs contents], 0xA5, attrs.length);
    std::memset([summary contents], 0, summary.length);

    auto* headerWords = static_cast<uint32_t*>([header contents]);
    headerWords[3] = 1;
    auto* polygon = static_cast<RenderPolygon*>([polygons contents]);
    polygon[0] = { 0, 0, 8, 0, 7, 0, 0, 0, 31u << 16u, 0.0f };
    auto* lines = static_cast<SpanSetupX*>([xSpans contents]);
    for (uint32_t y = 0; y < lineCount; y++)
    {
        lines[y] = {};
        lines[y].X0 = 0;
        lines[y].X1 = 8;
        lines[y].EdgeLenL = 0;
        lines[y].EdgeLenR = 0;
        lines[y].Flags = (1u << 1u) | (1u << 2u) | (1u << 3u);
        lines[y].Z0 = lines[y].Z1 = 0x123456;
        lines[y].ColorR0 = lines[y].ColorR1 = 63 << 3;
        lines[y].ColorG0 = lines[y].ColorG1 = 31 << 3;
        lines[y].ColorB0 = lines[y].ColorB1 = 15 << 3;
    }
    auto* workDescs = static_cast<WorkDesc*>([work contents]);
    workDescs[maxWorkTiles] = { 0, 0 };
    auto* variant = static_cast<VariantMeta*>([variants contents]);
    variant[0] = { 0, 0, 0, 0 };

    id<MTLCommandBuffer> command = [State->Queue commandBuffer];
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->ClearTileSummaryPipeline];
        [encoder setBuffer:summary offset:0 atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:State->RasteriseNoTextureTilesPipeline];
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
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                 threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [encoder endEncoding];
    }
    if (!CompleteCommandBuffer(command, "no-texture tile-memory self-test"))
        return false;

    const auto* result = static_cast<const TileRasterSummary*>([summary contents]);
    const auto* outColor = static_cast<const uint32_t*>([colors contents]);
    const auto* outDepth = static_cast<const uint32_t*>([depths contents]);
    const auto* outAttr = static_cast<const uint32_t*>([attrs contents]);
    const uint32_t expectedColor = 63u | (31u << 8u) | (15u << 16u) | (31u << 24u);
    if (result->RasterisedWorkItems != 1 || result->CoveredPixels != 64 ||
        result->DepthMin != 0x123456 || result->DepthMax != 0x123456 ||
        result->ColorHash == 0 || outColor[0] != expectedColor ||
        outDepth[0] != 0x123456 || (outAttr[0] & 0x4u) == 0)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute tile memory: self-test mismatch work=%u covered=%u hash=0x%08x depth=%08x..%08x color=%08x attr=%08x\n",
            result->RasterisedWorkItems, result->CoveredPixels, result->ColorHash,
            result->DepthMin, result->DepthMax, outColor[0], outAttr[0]);
        return false;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute tile memory: self-test PASS work=%u covered=%u color=%08x depth=%08x attr=%08x\n",
        result->RasterisedWorkItems, result->CoveredPixels,
        outColor[0], outDepth[0], outAttr[0]);
    return true;
}

void MetalComputeRenderer3D::Reset()
{
    RasterReference.Reset();
    if (!State)
        return;
    State->LoggedOverflow = false;
    State->LastFrameComputeVisible = false;
    State->LastFrameComputeReused = false;
    State->VisibleCutoverDisabled = false;
    State->LoggedVisibleCutover = false;
    State->LoggedVisibleFallback = false;
    State->ComputeVisibleFault.store(false, std::memory_order_release);
    State->SkippedTileBusyFrames.store(0, std::memory_order_relaxed);
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
    scale = std::max(1, scale);
    if (State)
        State->RequestedScaleFactor = static_cast<uint32_t>(scale);

    // Metal Compute is the only path with a nested visible raster renderer.
    // Verify and force the physical target, not only the cached scale integer.
    if (!RasterReference.ForceScaleFactor(scale))
    {
        if (State)
        {
            State->SpanBinReady = false;
            State->FinalPassReady = false;
            State->LastFrameComputeVisible = false;
        }
        return;
    }

    // Raster-only degraded mode must keep scaling the visible target without
    // accidentally re-enabling partially initialized compute resources.
    if (!State || !State->Ready)
        return;

    const int expectedWidth = 256 * scale;
    const int expectedHeight = 192 * scale;
    if (RasterReference.GetScaleFactor() != scale ||
        RasterReference.GetTargetWidth() != expectedWidth ||
        RasterReference.GetTargetHeight() != expectedHeight)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute scale sync: forced mismatch requested=%d actualScale=%d target=%dx%d expected=%dx%d\n",
            scale, RasterReference.GetScaleFactor(),
            RasterReference.GetTargetWidth(), RasterReference.GetTargetHeight(),
            expectedWidth, expectedHeight);
        State->SpanBinReady = false;
        return;
    }

    if (static_cast<uint32_t>(scale) != State->ScaleFactor)
    {
        if (!ConfigureSpanBinResources(scale))
        {
            State->SpanBinReady = false;
            return;
        }
        State->SpanBinReady = true;
        State->TileRasterReady = true;
        State->DepthBlendReady = true;
        State->TextureVariantReady = true;
        State->FinalPassReady = true;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal compute scale sync: applied forced scale=%d target=%dx%d compute=%ux%u\n",
        scale, RasterReference.GetTargetWidth(), RasterReference.GetTargetHeight(),
        State->ScreenWidth, State->ScreenHeight);
}

void MetalComputeRenderer3D::SetHighResolutionCoordinates(bool enabled) noexcept
{
    if (State)
        State->HiresCoordinates = enabled;

    // MELONPRIME_METAL_RENDER_OPTIONS_V1: the compute mirror and its currently visible Metal raster
    // reference must use the same coordinate mode.
    RasterReference.SetHighResolutionCoordinates(enabled);
}

void MetalComputeRenderer3D::SetBetterPolygons(bool betterPolygons) noexcept
{
    RasterReference.SetBetterPolygons(betterPolygons);
}

void MetalComputeRenderer3D::SetCpuReadbackRequired(bool required) noexcept
{
    if (State)
        State->CpuReadbackRequired = required;
    RasterReference.SetCpuReadbackRequired(required);
}

bool MetalComputeRenderer3D::SubmitRealFrameSpanBin()
{
    if (!State || !State->SpanBinReady)
        return false;

    const uint32_t polygonCount = std::min<uint32_t>(GPU3D.RenderNumPolygons, kMaxPolygons);
    uint32_t numYSpans = 0;
    uint32_t numSetupIndices = 0;
    State->VariantData.clear();
    std::fill(State->VariantMetaData.begin(), State->VariantMetaData.end(), VariantMeta {});

    const bool enableTextureMaps = (GPU3D.RenderDispCnt & (1u << 0)) != 0;
    int captureInfo[16] = {};
    GPU.GetCaptureInfo_Texture(captureInfo);
    auto spanOverflow = [&]() -> bool {
        if (!State->LoggedOverflow)
        {
            State->LoggedOverflow = true;
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: frame input exceeded span budget ySpans=%u/%u xSpans=%u/%u; mirror frame skipped safely\n",
                numYSpans,
                kMaxYSpanSetups,
                numSetupIndices,
                State->MaxSetupIndices);
        }
        return true;
    };

    for (uint32_t polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++)
    {
        Polygon* polygon = GPU3D.RenderPolygonRAM[polygonIndex];
        RenderPolygon& outputPolygon = State->PolygonData[polygonIndex];
        outputPolygon = {};
        if (!polygon || polygon->NumVertices < 2 || polygon->NumVertices > 10)
        {
            outputPolygon.YTop = 1;
            outputPolygon.YBot = 0;
            continue;
        }
        outputPolygon.FirstXSpan = numSetupIndices;
        outputPolygon.Attr = polygon->Attr;

        const uint32_t textype = (polygon->TexParam >> 26) & 0x7u;
        uint32_t captureKind = 0;
        uint32_t captureLayer = 0;
        uint32_t captureYOffset = 0;
        if (enableTextureMaps && textype == 7u)
        {
            const uint32_t textureWidth =
                8u << ((polygon->TexParam >> 20u) & 0x7u);
            const uint32_t textureHeight =
                8u << ((polygon->TexParam >> 23u) & 0x7u);
            if (textureWidth == 128u || textureWidth == 256u)
            {
                const uint32_t textureAddress =
                    (polygon->TexParam & 0xFFFFu) << 3u;
                const uint32_t endAddress =
                    textureAddress + textureWidth * textureHeight * 2u;
                const uint32_t firstBlock = textureAddress >> 15u;
                const uint32_t lastBlock =
                    (endAddress + 0x7FFFu) >> 15u;
                int captureBlock = -1;
                for (uint32_t block = firstBlock;
                     block < lastBlock && block < 16u;
                     block++)
                {
                    if (captureInfo[block] != -1)
                        captureBlock = captureInfo[block];
                }
                if (captureBlock != -1)
                {
                    captureKind = textureWidth == 128u ? 1u : 2u;
                    captureLayer = captureKind == 1u
                        ? static_cast<uint32_t>(captureBlock)
                        : static_cast<uint32_t>(captureBlock >> 2);
                    captureYOffset = captureKind == 1u
                        ? ((polygon->TexParam & 0xFFFFu) >> 5u) & 0x7Fu
                        : ((polygon->TexParam & 0xFFFFu) >> 6u) & 0xFFu;
                }
            }
        }

        const VariantKey key {
            polygon->TexParam,
            polygon->TexPalette,
            polygon->IsShadowMask ? 4u : ((polygon->Attr >> 4) & 0x3u),
            (enableTextureMaps && textype != 0) ? 1u : 0u,
            captureKind,
            captureLayer,
            captureYOffset,
        };

        uint32_t variantIndex = 0;
        auto variantIt = std::find(State->VariantData.begin(), State->VariantData.end(), key);
        if (variantIt == State->VariantData.end())
        {
            if (State->VariantData.size() < kMaxVariants)
            {
                variantIndex = static_cast<uint32_t>(State->VariantData.size());
                State->VariantData.push_back(key);
            }
            else
            {
                variantIndex = kMaxVariants - 1;
            }
        }
        else
        {
            variantIndex = static_cast<uint32_t>(variantIt - State->VariantData.begin());
        }
        outputPolygon.Variant = variantIndex;
        State->VariantMetaData[variantIndex] = {
            key.Textured,
            key.BlendMode,
            key.TexParam,
            key.TexPalette,
            key.CaptureKind,
            key.CaptureLayer,
            key.CaptureYOffset,
            0,
        };
        outputPolygon.TextureLayer = static_cast<float>(key.CaptureLayer);

        const uint32_t nverts = polygon->NumVertices;
        uint32_t vtop = polygon->VTop;
        uint32_t vbot = polygon->VBottom;
        uint32_t curVL = vtop;
        uint32_t curVR = vtop;
        uint32_t nextVL = 0;
        uint32_t nextVR = 0;

        if (polygon->FacingView)
        {
            nextVL = (curVL + 1) % nverts;
            nextVR = curVR == 0 ? nverts - 1 : curVR - 1;
        }
        else
        {
            nextVL = curVL == 0 ? nverts - 1 : curVL - 1;
            nextVR = (curVR + 1) % nverts;
        }

        int32_t positions[10][2] = {};
        int32_t ytop = static_cast<int32_t>(State->ScreenHeight);
        int32_t ybot = 0;
        for (uint32_t vertex = 0; vertex < nverts; vertex++)
        {
            if (State->HiresCoordinates)
            {
                positions[vertex][0] =
                    (polygon->Vertices[vertex]->HiresPosition[0] * static_cast<int32_t>(State->ScaleFactor)) >> 4;
                positions[vertex][1] =
                    (polygon->Vertices[vertex]->HiresPosition[1] * static_cast<int32_t>(State->ScaleFactor)) >> 4;
            }
            else
            {
                positions[vertex][0] =
                    polygon->Vertices[vertex]->FinalPosition[0] * static_cast<int32_t>(State->ScaleFactor);
                positions[vertex][1] =
                    polygon->Vertices[vertex]->FinalPosition[1] * static_cast<int32_t>(State->ScaleFactor);
            }
            ytop = std::min(positions[vertex][1], ytop);
            ybot = std::max(positions[vertex][1], ybot);
        }

        outputPolygon.YTop = ytop;
        outputPolygon.YBot = ybot;
        outputPolygon.XMin = static_cast<int32_t>(State->ScreenWidth);
        outputPolygon.XMax = 0;

        auto reserveYSpan = [&]() -> SpanSetupY* {
            if (numYSpans >= kMaxYSpanSetups)
                return nullptr;
            return &State->YSpanData[numYSpans++];
        };
        auto appendIndex = [&](uint32_t spanL, uint32_t spanR, int32_t y) -> bool {
            if (numSetupIndices >= State->MaxSetupIndices || y < 0 || y > 0xFFFF)
                return false;
            State->SetupIndexData[numSetupIndices++] = {
                static_cast<uint16_t>(polygonIndex),
                static_cast<uint16_t>(spanL),
                static_cast<uint16_t>(spanR),
                static_cast<uint16_t>(y),
            };
            return true;
        };

        if (ybot == ytop)
        {
            vtop = 0;
            vbot = 0;
            outputPolygon.YBot++;
            if (positions[1][0] < positions[vtop][0]) vtop = 1;
            if (positions[1][0] > positions[vbot][0]) vbot = 1;
            const uint32_t last = nverts - 1;
            if (positions[last][0] < positions[vtop][0]) vtop = last;
            if (positions[last][0] > positions[vbot][0]) vbot = last;

            const uint32_t curSpanL = numYSpans;
            SpanSetupY* spanL = reserveYSpan();
            const uint32_t curSpanR = numYSpans;
            SpanSetupY* spanR = reserveYSpan();
            if (!spanL || !spanR)
                return spanOverflow();
            SetupYSpanDummy(outputPolygon, *spanL, polygon, vtop, 0, positions);
            SetupYSpanDummy(outputPolygon, *spanR, polygon, vbot, 1, positions);
            if (!appendIndex(curSpanL, curSpanR, ytop))
                return spanOverflow();
        }
        else
        {
            uint32_t curSpanL = numYSpans;
            SpanSetupY* spanL = reserveYSpan();
            uint32_t curSpanR = numYSpans;
            SpanSetupY* spanR = reserveYSpan();
            if (!spanL || !spanR)
                return spanOverflow();
            SetupYSpan(outputPolygon, *spanL, polygon, curVL, nextVL, 0, positions);
            SetupYSpan(outputPolygon, *spanR, polygon, curVR, nextVR, 1, positions);

            for (int32_t y = ytop; y < ybot; y++)
            {
                if (y >= positions[nextVL][1] && curVL != polygon->VBottom)
                {
                    while (y >= positions[nextVL][1] && curVL != polygon->VBottom)
                    {
                        curVL = nextVL;
                        if (polygon->FacingView)
                            nextVL = (curVL + 1) % nverts;
                        else
                            nextVL = curVL == 0 ? nverts - 1 : curVL - 1;
                    }
                    curSpanL = numYSpans;
                    spanL = reserveYSpan();
                    if (!spanL)
                        return spanOverflow();
                    SetupYSpan(outputPolygon, *spanL, polygon, curVL, nextVL, 0, positions);
                }

                if (y >= positions[nextVR][1] && curVR != polygon->VBottom)
                {
                    while (y >= positions[nextVR][1] && curVR != polygon->VBottom)
                    {
                        curVR = nextVR;
                        if (polygon->FacingView)
                            nextVR = curVR == 0 ? nverts - 1 : curVR - 1;
                        else
                            nextVR = (curVR + 1) % nverts;
                    }
                    curSpanR = numYSpans;
                    spanR = reserveYSpan();
                    if (!spanR)
                        return spanOverflow();
                    SetupYSpan(outputPolygon, *spanR, polygon, curVR, nextVR, 1, positions);
                }

                if (!appendIndex(curSpanL, curSpanR, y))
                    return spanOverflow();
            }
        }
    }

    if (State->VariantData.empty())
        State->VariantData.push_back({});

    {
        MetalComputeState::FrameSlot* slot = nullptr;
        uint32_t slotIndex = 0;
        for (uint32_t i = 0; i < kFrameSlotCount; i++)
        {
            bool expected = false;
            if (State->Slots[i].InFlight.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
            {
                slot = &State->Slots[i];
                slotIndex = i;
                break;
            }
        }
        if (!slot)
        {
            State->SkippedBusyFrames.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        std::memcpy([slot->SetupIndices contents],
                    State->SetupIndexData.data(),
                    static_cast<size_t>(numSetupIndices) * sizeof(SetupIndices));
        std::memcpy([slot->YSpans contents],
                    State->YSpanData.data(),
                    static_cast<size_t>(numYSpans) * sizeof(SpanSetupY));
        std::memcpy([slot->Polygons contents],
                    State->PolygonData.data(),
                    static_cast<size_t>(polygonCount) * sizeof(RenderPolygon));
        std::memcpy([slot->VariantMetaBuffer contents],
                    State->VariantMetaData.data(),
                    static_cast<size_t>(kMaxVariants) * sizeof(VariantMeta));
        std::memcpy([slot->TextureMemoryBuffer contents],
                    GPU.VRAMFlat_Texture,
                    sizeof(GPU.VRAMFlat_Texture));
        std::memcpy([slot->TexturePaletteBuffer contents],
                    GPU.VRAMFlat_TexPal,
                    sizeof(GPU.VRAMFlat_TexPal));
        auto* toonTable =
            static_cast<uint32_t*>([slot->ToonTableBuffer contents]);
        for (uint32_t index = 0; index < 32u; index++)
        {
            const uint32_t color = GPU3D.RenderToonTable[index];
            uint32_t r = (color << 1u) & 0x3Eu; if (r) r++;
            uint32_t g = (color >> 4u) & 0x3Eu; if (g) g++;
            uint32_t b = (color >> 9u) & 0x3Eu; if (b) b++;
            toonTable[index] = r | (g << 8u) | (b << 16u);
        }
        std::memset([slot->Header contents], 0, slot->Header.length);

        const uint32_t variantCount = static_cast<uint32_t>(State->VariantData.size());
        const uint32_t polygonGroups = DispatchGroups(polygonCount, 32);
        const uint32_t coarseTilesX = State->ScreenWidth / State->CoarseTileW;
        const uint32_t coarseTilesY = State->ScreenHeight / State->CoarseTileH;
        const uint32_t tileCount = State->TilesPerLine * State->TileLines;

        const FoundationConfig foundationConfig {
            variantCount,
            State->MaxWorkTiles,
            tileCount,
            kRasteriseChunkSize,
        };
        const SpanBinConfig spanConfig {
            polygonCount,
            variantCount,
            numSetupIndices,
            State->ScreenWidth,
            State->ScreenHeight,
            State->TileSize,
            State->TilesPerLine,
            State->TileLines,
            kCoarseTileCountX,
            State->CoarseTileCountY,
            State->CoarseTileW,
            State->CoarseTileH,
            State->MaxWorkTiles,
            kBinStride,
            kCoarseBinStride,
            polygonGroups,
            GPU3D.RenderAlphaRef,
            GPU3D.RenderDispCnt,
            State->TileWorkCapacity,
            polygonCount > 0 && GPU3D.RenderPolygonRAM[0] &&
                    GPU3D.RenderPolygonRAM[0]->WBuffer
                ? 1u
                : 0u,
        };

        uint32_t clearR = (GPU3D.RenderClearAttr1 << 1) & 0x3Eu;
        if (clearR) clearR++;
        uint32_t clearG = (GPU3D.RenderClearAttr1 >> 4) & 0x3Eu;
        if (clearG) clearG++;
        uint32_t clearB = (GPU3D.RenderClearAttr1 >> 9) & 0x3Eu;
        if (clearB) clearB++;
        const uint32_t clearA = (GPU3D.RenderClearAttr1 >> 16) & 0x1Fu;
        auto* finalTables =
            static_cast<uint32_t*>([slot->FinalTablesBuffer contents]);
        for (uint32_t index = 0; index < 8u; index++)
            finalTables[index] = ConvertRGB555ToRGB6(GPU3D.RenderEdgeTable[index]);
        for (uint32_t index = 0; index < 34u; index++)
            finalTables[8u + index] = GPU3D.RenderFogDensityTable[index];

        const FinalPassConfig finalPassConfig {
            State->ScreenWidth,
            State->ScreenHeight,
            GPU3D.RenderDispCnt,
            ((GPU3D.RenderClearAttr2 & 0x7FFFu) * 0x200u) + 0x1FFu,
            GPU3D.RenderClearAttr1 & 0x3F008000u,
            GPU3D.RenderFogOffset,
            GPU3D.RenderFogShift,
            ConvertFogColorToRGB6A5(GPU3D.RenderFogColor),
        };

        const CompleteDepthBlendConfig depthBlendConfig {
            State->ScreenWidth,
            State->ScreenHeight,
            State->TileSize,
            State->TilesPerLine,
            kBinStride,
            polygonGroups,
            State->MaxWorkTiles,
            State->TileWorkCapacity,
            polygonCount,
            clearR |
                (clearG << 8u) |
                (clearB << 16u) |
                (clearA << 24u),
            ((GPU3D.RenderClearAttr2 & 0x7FFFu) *
                0x200u) +
                0x1FFu,
            GPU3D.RenderClearAttr1 & 0x3F008000u,
            GPU3D.RenderDispCnt,
            (GPU3D.RenderClearAttr2 >> 16u) & 0xFFu,
            (GPU3D.RenderClearAttr2 >> 24u) & 0xFFu,
            spanConfig.Reserved,
        };


        bool ownsTileMemory = false;
        if (State->DepthBlendReady && State->FinalPassReady)
        {
            bool expected = false;
            ownsTileMemory = State->TileMemoryInFlight.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel,
                std::memory_order_relaxed);
            if (!ownsTileMemory)
                State->SkippedTileBusyFrames.fetch_add(
                    1, std::memory_order_relaxed);
        }

        const bool submitTileRaster =
            ownsTileMemory &&
            State->TileRasterReady &&
            polygonGroups > 0 &&
            numSetupIndices > 0;
        const bool submitDepthBlend =
            ownsTileMemory && State->DepthBlendReady;
        const bool submitFinalPass =
            submitDepthBlend && State->FinalPassReady;

        if (!ownsTileMemory)
        {
            slot->InFlight.store(false, std::memory_order_release);
            return true;
        }
        id<MTLCommandBuffer> command = [State->Queue commandBuffer];
        if (!command)
        {
            if (ownsTileMemory)
                State->TileMemoryInFlight.store(
                    false, std::memory_order_release);
            slot->InFlight.store(false, std::memory_order_release);
            return false;
        }
        command.label = @"MelonPrime Metal Compute Phase 8V Summary-Free Production";

        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->ClearIndirectPipeline];
            [encoder setBuffer:slot->Header offset:0 atIndex:0];
            [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(variantCount, 32), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
            [encoder endEncoding];
        }
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->ClearCoarseMaskPipeline];
            [encoder setBuffer:slot->CoarseMask offset:0 atIndex:0];
            [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(tileCount, 64), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [encoder endEncoding];
        }
        if (numSetupIndices > 0)
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->InterpSpansPipeline];
            [encoder setBuffer:slot->SetupIndices offset:0 atIndex:0];
            [encoder setBuffer:slot->YSpans offset:0 atIndex:1];
            [encoder setBuffer:slot->XSpans offset:0 atIndex:2];
            [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:3];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(numSetupIndices, 32), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
            [encoder endEncoding];
        }
        if (polygonGroups > 0 && numSetupIndices > 0)
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->BinCombinedPipeline];
            [encoder setBuffer:slot->Header offset:0 atIndex:0];
            [encoder setBuffer:slot->Polygons offset:0 atIndex:1];
            [encoder setBuffer:slot->XSpans offset:0 atIndex:2];
            [encoder setBuffer:slot->CoarseMask offset:0 atIndex:3];
            [encoder setBuffer:slot->FineMask offset:0 atIndex:4];
            [encoder setBuffer:slot->WorkOffsets offset:0 atIndex:5];
            [encoder setBuffer:slot->WorkDescs offset:0 atIndex:6];
            [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:7];
            [encoder dispatchThreadgroups:MTLSizeMake(polygonGroups, coarseTilesX, coarseTilesY)
                     threadsPerThreadgroup:MTLSizeMake(State->CoarseTileArea, 1, 1)];
            [encoder endEncoding];
        }
        if (polygonGroups > 0 && numSetupIndices > 0)
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->CalcOffsetsPipeline];
            [encoder setBuffer:slot->Header offset:0 atIndex:0];
            [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(variantCount, 32), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
            [encoder endEncoding];
        }
        if (polygonGroups > 0 && numSetupIndices > 0)
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->SortWorkPolygonsPipeline];
            [encoder setBuffer:slot->Header offset:0 atIndex:0];
            [encoder setBuffer:slot->Polygons offset:0 atIndex:1];
            [encoder setBuffer:slot->WorkDescs offset:0 atIndex:2];
            [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:3];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(State->MaxWorkTiles, 32), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
            [encoder endEncoding];
        }
        if (ownsTileMemory)
        {
            if (submitTileRaster)
            {            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->TextureRasterPipeline];
                [encoder setBuffer:slot->Header offset:0 atIndex:0];
                [encoder setBuffer:slot->Polygons offset:0 atIndex:1];
                [encoder setBuffer:slot->XSpans offset:0 atIndex:2];
                [encoder setBuffer:slot->WorkDescs offset:0 atIndex:3];
                [encoder setBuffer:slot->VariantMetaBuffer offset:0 atIndex:4];
                [encoder setBuffer:State->ColorTiles offset:0 atIndex:5];
                [encoder setBuffer:State->DepthTiles offset:0 atIndex:6];
                [encoder setBuffer:State->AttrTiles offset:0 atIndex:7];
                [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:9];
                [encoder setBuffer:slot->TextureMemoryBuffer offset:0 atIndex:10];
                [encoder setBuffer:slot->TexturePaletteBuffer offset:0 atIndex:11];
                [encoder setBuffer:slot->ToonTableBuffer offset:0 atIndex:12];
                [encoder setTexture:State->Capture128Texture atIndex:0];
                [encoder setTexture:State->Capture256Texture atIndex:1];
                [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(State->MaxWorkTiles, 64), 1, 1)
                         threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [encoder endEncoding];
                }
            }
            if (submitDepthBlend)
            {                {
                    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                    [encoder
                        setComputePipelineState:
                            State->CompleteDepthBlendPipeline];
                    [encoder setBuffer:slot->FineMask offset:0 atIndex:0];
                    [encoder setBuffer:slot->WorkOffsets offset:0 atIndex:1];
                    [encoder setBuffer:slot->Polygons offset:0 atIndex:2];
                    [encoder setBuffer:State->ColorTiles offset:0 atIndex:3];
                    [encoder setBuffer:State->DepthTiles offset:0 atIndex:4];
                    [encoder setBuffer:State->AttrTiles offset:0 atIndex:5];
                    [encoder setBuffer:State->DepthBlendColor offset:0 atIndex:6];
                    [encoder setBuffer:State->DepthBlendDepth offset:0 atIndex:7];
                    [encoder setBuffer:State->DepthBlendAttr offset:0 atIndex:8];
                    [encoder
                        setBytes:&depthBlendConfig
                           length:sizeof(depthBlendConfig)
                          atIndex:10];
                    [encoder
                        setBuffer:slot->TextureMemoryBuffer
                           offset:0
                          atIndex:11];
                    const uint32_t pixelCount = State->ScreenWidth * State->ScreenHeight;
                    [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(pixelCount, 64), 1, 1)
                             threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [encoder endEncoding];
                }
                if (submitFinalPass)
                {                    {
                        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                        [encoder setComputePipelineState:State->FinalPassPipeline];
                        [encoder setBuffer:State->DepthBlendColor offset:0 atIndex:0];
                        [encoder setBuffer:State->DepthBlendDepth offset:0 atIndex:1];
                        [encoder setBuffer:State->DepthBlendAttr offset:0 atIndex:2];
                        [encoder setBuffer:slot->FinalTablesBuffer offset:0 atIndex:4];
                        [encoder setBytes:&finalPassConfig length:sizeof(finalPassConfig) atIndex:5];
                        [encoder setTexture:slot->FinalTexture atIndex:0];
                        const uint32_t finalPixelCount = State->ScreenWidth * State->ScreenHeight;
                        [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(finalPixelCount, 64), 1, 1)
                                 threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                        [encoder endEncoding];
                    }
                }
            }
        }

        const uint64_t serial = State->SubmittedFrames.fetch_add(1, std::memory_order_relaxed) + 1;
        const uint64_t generation = slot->Generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        const bool completedComputeSurface = ownsTileMemory;
        const bool completedFinalPass = submitFinalPass;
        MetalComputeState* state = State.get();
        if (submitFinalPass)
        {
            State->SubmittedFinalSlot.store(slotIndex, std::memory_order_release);
            State->SubmittedFinalSerial.store(serial, std::memory_order_release);
        }
        slot->LastCommand = command;
        [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status == MTLCommandBufferStatusCompleted)
            {
                if (completedFinalPass)
                {
                    state->PublishedFinalSlot.store(
                        slotIndex, std::memory_order_release);
                    state->PublishedFinalSerial.store(
                        serial, std::memory_order_release);
                }
            }
            else
            {
                const char* message = completed.error
                    ? [[completed.error localizedDescription] UTF8String]
                    : "unknown command-buffer failure";
                state->ComputeVisibleFault.store(
                    true, std::memory_order_release);
                std::fprintf(stderr,
                    "[MelonPrime] metal compute span/bin: frame=%llu "
                    "GPU failure: %s; visible cutover will fall back\n",
                    static_cast<unsigned long long>(serial), message);
            }
            if (completedComputeSurface)
                state->TileMemoryInFlight.store(
                    false, std::memory_order_release);
            if (slot->Generation.load(std::memory_order_acquire) == generation)
                slot->InFlight.store(false, std::memory_order_release);
        }];
        [command commit];
    }

    return true;
}

void MetalComputeRenderer3D::RenderFrame()
{
    @autoreleasepool
    {
        const int requestedScale = State
            ? std::max(1, static_cast<int>(State->RequestedScaleFactor))
            : 1;

        if (State)
        {
            State->LastFrameEngineALayer = GPU.ScreenSwap ? 0u : 1u;
            State->LastFrameRenderedScale = requestedScale;
            const uint32_t displayModeA =
                (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
            const bool engineA3DEnabled =
                (GPU.GPU2D_A.DispCnt & (1u << 3)) != 0u;
            State->LastFrameUseHiRes3D =
                requestedScale > 1 &&
                GPU.ScreensEnabled &&
                displayModeA == 1u &&
                engineA3DEnabled &&
                GPU3D.RenderNumPolygons > 0 &&
                !GPU3D.AbortFrame;
        }

        if (RasterReference.GetScaleFactor() != requestedScale ||
            RasterReference.GetTargetWidth() != 256 * requestedScale ||
            RasterReference.GetTargetHeight() != 192 * requestedScale)
        {
            if (!RasterReference.ForceScaleFactor(requestedScale))
            {
                if (State)
                    State->LastFrameComputeVisible = false;
                std::fprintf(stderr,
                    "[MelonPrime] metal compute: failed to resize "
                    "RasterReference to scale=%d; using previous raster target\n",
                    requestedScale);
                RasterReference.RenderFrame();
                return;
            }
            // MELONPRIME_METAL_COMPUTE_FORCE_SCALE_CHECK_V1
        }

        if (State &&
            State->ComputeVisibleFault.exchange(
                false, std::memory_order_acq_rel))
        {
            State->VisibleCutoverDisabled = true;
            State->LastFrameComputeVisible = false;
            std::fprintf(stderr,
                "[MelonPrime] metal compute visible: disabled after GPU "
                "command failure; restart renderer to retry\n");
        }

        const bool visibleRequested =
            MetalComputeVisibleEnabled() &&
            State &&
            !State->VisibleCutoverDisabled;
        const bool visibleEligible =
            visibleRequested &&
            State->Ready &&
            State->SpanBinReady &&
            State->TileRasterReady &&
            State->DepthBlendReady &&
            State->TextureVariantReady &&
            State->FinalPassReady &&
            !State->CpuReadbackRequired &&
            !GPU3D.AbortFrame;
        if (!visibleEligible)
        {
            if (State)
            {
                State->LastFrameComputeVisible = false;
                State->LastFrameComputeReused = false;
                if (visibleRequested)
                {
                    if (!State->LoggedVisibleFallback)
                    {
                        State->LoggedVisibleFallback = true;
                        std::fprintf(stderr,
                            "[MelonPrime] metal compute visible: frame uses "
                            "RasterReference fallback "
                            "(cpuReadback=%u ready=%u abort=%u)\n",
                            State->CpuReadbackRequired ? 1u : 0u,
                            State->FinalPassReady ? 1u : 0u,
                            GPU3D.AbortFrame ? 1u : 0u);
                    }
                }
            }

            RasterReference.RenderFrame();
            return;
        }

        const bool previousFrameWasCompute =
            State->LastFrameComputeVisible;
        if (GPU3D.RenderFrameIdentical &&
            previousFrameWasCompute &&
            ComputeFinalReady())
        {
            State->LastFrameComputeVisible = true;
            State->LastFrameComputeReused = true;
            return;
        }

        const uint64_t serialBefore = GetComputeFinalSerial();
        const bool submitted = SubmitRealFrameSpanBin();
        const uint64_t serialAfter = GetComputeFinalSerial();
        const bool finalSubmitted =
            submitted &&
            serialAfter > serialBefore &&
            GetComputeFinalTexture() != nullptr;

        if (finalSubmitted)
        {
            State->LastFrameComputeVisible = true;
            State->LastFrameComputeReused = false;
            if (!State->LoggedVisibleCutover)
            {
                State->LoggedVisibleCutover = true;
                std::fprintf(stderr,
                    "[MelonPrime] metal compute visible: CUTOVER active "
                    "scale=%d size=%ux%u serial=%llu "
                    "rasterReference=stopped\n",
                    requestedScale,
                    State->ScreenWidth,
                    State->ScreenHeight,
                    static_cast<unsigned long long>(serialAfter));
            }
            return;
        }

        State->LastFrameComputeVisible = false;
        State->LastFrameComputeReused = false;
        if (!State->LoggedVisibleFallback)
        {
            State->LoggedVisibleFallback = true;
            std::fprintf(stderr,
                "[MelonPrime] metal compute visible: no final slot submitted; "
                "using RasterReference for this frame "
                "(spanBusy=%llu tileBusy=%llu)\n",
                static_cast<unsigned long long>(
                    State->SkippedBusyFrames.load(
                        std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    State->SkippedTileBusyFrames.load(
                        std::memory_order_relaxed)));
        }

        RasterReference.RenderFrame();
    }
}

void MetalComputeRenderer3D::FinishRendering()
{
    if (!State || !State->LastFrameComputeVisible)
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
    if (State && State->LastFrameComputeVisible)
    {
        if (void* texture = GetComputeFinalTexture())
            return texture;
    }
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
    return State && State->Ready
        ? static_cast<int>(State->ScreenWidth)
        : RasterReference.GetTargetWidth();
}

int MetalComputeRenderer3D::GetTargetHeight() const noexcept
{
    return State && State->Ready
        ? static_cast<int>(State->ScreenHeight)
        : RasterReference.GetTargetHeight();
}

int MetalComputeRenderer3D::GetScaleFactor() const noexcept
{
    return State
        ? std::max(1, static_cast<int>(State->RequestedScaleFactor))
        : RasterReference.GetScaleFactor();
}

bool MetalComputeRenderer3D::LastFrameUsesHighResolution3D() const noexcept
{
    return State
        ? State->LastFrameUseHiRes3D
        : RasterReference.LastFrameUsesHighResolution3D();
}

uint32_t MetalComputeRenderer3D::GetLastFrameEngineALayer() const noexcept
{
    return State
        ? State->LastFrameEngineALayer
        : RasterReference.GetLastFrameEngineALayer();
}

int MetalComputeRenderer3D::GetLastFrameRenderedScale() const noexcept
{
    return State
        ? State->LastFrameRenderedScale
        : RasterReference.GetLastFrameRenderedScale();
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
    return State && State->Ready && State->SpanBinReady &&
           State->TileRasterReady && State->DepthBlendReady &&
           State->TextureVariantReady && State->FinalPassReady;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
