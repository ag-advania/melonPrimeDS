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
// MELONPRIME_METAL_COMPUTE_LEGACY_SUMMARY_RETIREMENT_V1
// MELONPRIME_METAL_COMPUTE_FRAME_BOOKKEEPING_CLEANUP_V1
// MELONPRIME_METAL_COMPUTE_CHANGE_DRIVEN_SNAPSHOTS_V1

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_MetalCompute.h"
#include "GPU3D_RasterEdge.h"
#include "GPU3D_RasterDifferential.h"

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

constexpr uint32_t kMaxVariants = 2048;
constexpr uint32_t kMaxPolygons = 2048;
constexpr uint32_t kMaxYSpanSetups = kMaxPolygons * 10;
constexpr uint32_t kRasteriseChunkSize = 32768;
constexpr uint32_t kBinStride = 2048 / 32;
constexpr uint32_t kCoarseBinStride = kBinStride / 32;
constexpr uint32_t kCoarseTileCountX = 8;
constexpr uint32_t kFrameSlotCount = 3;
// Per-slot tile scratch budget. Each frame slot owns its own Color/Depth/Attr
// tile memory so GPU frames can overlap without falling back to the raster
// renderer; MaxWorkTiles is derived from this budget so binning never emits
// work that the raster and depth-blend passes would have to discard.
//
// 256 MiB covers the full 16-work-items-per-tile request up to scale 4 (the
// range this fork is tuned for). Above that the budget lowers the work count,
// and frames whose bounded work exceeds it are rejected as compute submission
// failures rather than rendering with missing polygons.
constexpr size_t kTileMemoryBudgetBytesPerSlot = 256u * 1024u * 1024u;
constexpr uint32_t kWorkTilesPerTile = 16;



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

bool MetalComputeFallbackTraceEnabled()
{
    // Diagnostic-only: normal play does not format or emit per-frame fallback
    // records. This is intentionally separate from the production one-shot
    // warning so intermittent fallback sequences can be reconstructed.
    static const bool enabled = []() {
        const char* value =
            std::getenv("MELONPRIME_METAL_COMPUTE_TRACE_FALLBACKS");
        return value && value[0] == '1';
    }();
    return enabled;
}


bool UpdateSharedSnapshotIfChanged(
    id<MTLBuffer> buffer,
    const void* source,
    size_t bytes,
    bool& valid)
{
    if (!buffer || !source || bytes > static_cast<size_t>(buffer.length))
        return false;

    void* destination = [buffer contents];
    if (!valid || std::memcmp(destination, source, bytes) != 0)
    {
        std::memcpy(destination, source, bytes);
        valid = true;
    }
    return true;
}

bool UpdateSharedSnapshotForVersion(
    id<MTLBuffer> buffer,
    const void* source,
    size_t bytes,
    uint64_t sourceVersion,
    uint64_t& bufferVersion)
{
    if (bufferVersion == sourceVersion)
        return true;
    if (!buffer || !source || bytes > static_cast<size_t>(buffer.length))
        return false;

    void* destination = [buffer contents];
    if (!destination)
        return false;
    std::memcpy(destination, source, bytes);
    bufferVersion = sourceVersion;
    return true;
}

constexpr uint32_t kVariantWorkCountStart = 0;
constexpr uint32_t kSortedWorkOffsetStart = kVariantWorkCountStart + kMaxVariants * 4;
constexpr uint32_t kVariantWorkRealCountStart = kSortedWorkOffsetStart + kMaxVariants;
constexpr uint32_t kSortWorkCountStart = kVariantWorkRealCountStart + kMaxVariants;
constexpr uint32_t kRasterWorkCountStart = kSortWorkCountStart + 4;
constexpr uint32_t kBinHeaderWords = kRasterWorkCountStart + 3;

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
    uint32_t WBuffer;
    uint32_t FirstPolygon;
    uint32_t BatchPolygonCount;
};
static_assert(sizeof(SpanBinConfig) == 88, "MSL SpanBinConfig layout mismatch");

struct PolygonBatch
{
    uint32_t FirstPolygon;
    uint32_t PolygonCount;
};

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
    int32_t InsideStart, InsideEnd, EdgeCovL, EdgeCovR;
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
    uint32_t FacingView;
};
static_assert(sizeof(RenderPolygon) == 44, "MSL RenderPolygon layout mismatch");

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

#include "GPU3D_MetalComputeSpanMath.inc"

// The span math fragment above is prepended at library-creation time; it owns
// the SpanSetupY/SpanSetupX declarations and the XSpanSetup_* flag constants.
static constexpr const char* kMetalComputeSource = R"MSL(

constant uint MaxVariants = 2048u;
constant uint VariantWorkCountStart = 0u;
constant uint SortedWorkOffsetStart = VariantWorkCountStart + MaxVariants * 4u;
constant uint VariantWorkRealCountStart = SortedWorkOffsetStart + MaxVariants;
constant uint SortWorkCountStart = VariantWorkRealCountStart + MaxVariants;
constant uint RasterWorkCountStart = SortWorkCountStart + 4u;

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
    uint wBuffer;
    uint firstPolygon;
    uint batchPolygonCount;
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
    uint FacingView;
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
    if (gid < 3u)
        atomic_store_explicit(&header[RasterWorkCountStart + gid], 0u, memory_order_relaxed);

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
        atomic_store_explicit(&header[RasterWorkCountStart + 0u],
                              clampedCount,
                              memory_order_relaxed);
        atomic_store_explicit(&header[RasterWorkCountStart + 1u], 1u, memory_order_relaxed);
        atomic_store_explicit(&header[RasterWorkCountStart + 2u], 1u, memory_order_relaxed);
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

static inline bool ShouldDecrementRightVertical(
    thread const SpanSetupY& spanL,
    thread const SpanSetupY& spanR,
    int xL,
    int xR)
{
    return spanR.Increment == 0 &&
        (spanL.Increment != 0 || xL != xR) && xR != 0;
}

// Full DS X-span setup. Structurally identical to the OpenGL compute
// InterpSpans shader (GPU3D_Compute_shaders.h): edge coverage, fill flags and
// fixed-point perspective/linear attribute interpolation along Y.
kernel void mp_compute_interp_spans_geometry(
    device const SetupIndices* setupIndices [[buffer(0)]],
    device const SpanSetupY* ySpans [[buffer(1)]],
    device SpanSetupX* xSpans [[buffer(2)]],
    constant SpanBinConfig& config [[buffer(3)]],
    device const RenderPolygon* polygons [[buffer(4)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= config.numSetupIndices)
        return;

    const SetupIndices setup = setupIndices[gid];
    SpanSetupY spanL = ySpans[setup.SpanIdxL];
    SpanSetupY spanR = ySpans[setup.SpanIdxR];
    const RenderPolygon polygon = polygons[min(uint(setup.PolyIdx),
                                               max(config.numPolygons, 1u) - 1u)];
    const int y = int(setup.Y);
    const bool wBuffer = config.wBuffer != 0u;

    const int dxL = spanL.DxInitial + (y - spanL.Y0) * spanL.Increment;
    const int dxR = spanR.DxInitial + (y - spanR.Y0) * spanR.Increment;
    int xL = CalculateX(dxL, spanL);
    int xR = CalculateX(dxR, spanR);

    if (ShouldDecrementRightVertical(spanL, spanR, xL, xR))
        xR--;

    SpanSetupX out = {};
    out.Flags = 0u;

    int edgeLenL = 1;
    int edgeLenR = 1;

    const bool swappedEdges = xL > xR;
    if (swappedEdges)
    {
        // Crossed edges: swap sides and take the y-major coverage of each.
        const SpanSetupY tmpSpan = spanL;
        spanL = spanR;
        spanR = tmpSpan;

        const int tmpX = xL;
        xL = xR;
        xR = tmpX;

        mp_edge_params(true, true, dxR, spanL, edgeLenL, out.EdgeCovL);
        mp_edge_params(false, true, dxL, spanR, edgeLenR, out.EdgeCovR);
    }
    else
    {
        mp_edge_params(false, false, dxL, spanL, edgeLenL, out.EdgeCovL);
        mp_edge_params(true, false, dxR, spanR, edgeLenR, out.EdgeCovR);
    }

    out.CovLInitial = (out.EdgeCovL >> 12) & 0x3FF;
    if (out.CovLInitial == 0x3FF)
        out.CovLInitial = 0;
    out.CovRInitial = (out.EdgeCovR >> 12) & 0x3FF;
    if (out.CovRInitial == 0x3FF)
        out.CovRInitial = 0;

    out.X0 = xL;
    out.X1 = xR + 1;

    const uint polyAlpha = (polygon.Attr >> 16u) & 0x1Fu;
    const bool isWireframe = polyAlpha == 0u;
    if (!isWireframe || y == polygon.YTop || y == polygon.YBot - 1)
        out.Flags |= XSpanSetup_FillInside;

    out.InsideStart = min(out.X0 + edgeLenL, out.X1);
    out.InsideEnd = min(out.X1 - edgeLenR, out.X1);

    const bool fillAllEdges = isWireframe ||
        (polyAlpha < 31u && (config.dispCnt & (1u << 3u)) != 0u) ||
        (config.dispCnt & (3u << 4u)) != 0u;
    const bool bottomXMajor =
        y == polygon.YBot - 1 && spanL.X1 != spanR.X1;
    const bool leftNegative = spanL.X1 < spanL.X0;
    const bool rightNegative = spanR.X1 < spanR.X0;
    const bool leftXMajor = spanL.Increment > 0x40000;
    const bool rightXMajor = spanR.Increment > 0x40000;
    bool fillLeft;
    bool fillRight;
    if (swappedEdges)
    {
        fillLeft = leftNegative || !leftXMajor || (bottomXMajor && leftXMajor);
        fillRight = (!rightNegative && rightXMajor) ||
            (!(rightNegative && rightXMajor) && spanL.Increment == 0) ||
            (bottomXMajor && rightXMajor);
    }
    else
    {
        fillLeft = leftNegative || !leftXMajor || (bottomXMajor && leftXMajor) ||
            (spanL.Increment == spanR.Increment && out.X0 + edgeLenL == out.X1);
        fillRight = (!rightNegative && rightXMajor) || spanR.Increment == 0 ||
            (bottomXMajor && rightXMajor);
    }
    if (fillAllEdges || fillLeft)
        out.Flags |= XSpanSetup_FillLeft;
    if (fillAllEdges || fillRight)
        out.Flags |= XSpanSetup_FillRight;

    if (spanL.I0 == spanL.I1)
    {
        out.TexcoordU0 = spanL.TexcoordU0;
        out.TexcoordV0 = spanL.TexcoordV0;
        out.ColorR0 = spanL.ColorR0;
        out.ColorG0 = spanL.ColorG0;
        out.ColorB0 = spanL.ColorB0;
        out.Z0 = spanL.Z0;
        out.W0 = spanL.W0;
    }
    else
    {
        const int i = y - spanL.I0;
        const int ifactor = mp_calc_factor_y(spanL, i);
        const int idiff = spanL.I1 - spanL.I0;

        out.Z0 = wBuffer
            ? int(mp_interp_z_wbuffer(
                  spanL.Z0, spanL.Z1, ifactor, MPFactorShiftY, true))
            : int(mp_interp_z_zbuffer(
                  spanL.Z0, spanL.Z1, i, spanL.IRecip, idiff, true));

        if (spanL.Linear == 0)
        {
            out.TexcoordU0 = mp_interp_attr_persp(
                spanL.TexcoordU0, spanL.TexcoordU1, ifactor, MPFactorShiftY);
            out.TexcoordV0 = mp_interp_attr_persp(
                spanL.TexcoordV0, spanL.TexcoordV1, ifactor, MPFactorShiftY);
            out.ColorR0 = mp_interp_attr_persp(
                spanL.ColorR0, spanL.ColorR1, ifactor, MPFactorShiftY);
            out.ColorG0 = mp_interp_attr_persp(
                spanL.ColorG0, spanL.ColorG1, ifactor, MPFactorShiftY);
            out.ColorB0 = mp_interp_attr_persp(
                spanL.ColorB0, spanL.ColorB1, ifactor, MPFactorShiftY);
            out.W0 = mp_interp_attr_persp(
                spanL.W0, spanL.W1, ifactor, MPFactorShiftY);
        }
        else
        {
            out.TexcoordU0 = mp_interp_attr_linear(
                spanL.TexcoordU0, spanL.TexcoordU1, i, spanL.IRecip, idiff, true);
            out.TexcoordV0 = mp_interp_attr_linear(
                spanL.TexcoordV0, spanL.TexcoordV1, i, spanL.IRecip, idiff, true);
            out.ColorR0 = mp_interp_attr_linear(
                spanL.ColorR0, spanL.ColorR1, i, spanL.IRecip, idiff, true);
            out.ColorG0 = mp_interp_attr_linear(
                spanL.ColorG0, spanL.ColorG1, i, spanL.IRecip, idiff, true);
            out.ColorB0 = mp_interp_attr_linear(
                spanL.ColorB0, spanL.ColorB1, i, spanL.IRecip, idiff, true);
            // The linear path is only taken when W0 == W1.
            out.W0 = spanL.W0;
        }
    }

    if (spanR.I0 == spanR.I1)
    {
        out.TexcoordU1 = spanR.TexcoordU0;
        out.TexcoordV1 = spanR.TexcoordV0;
        out.ColorR1 = spanR.ColorR0;
        out.ColorG1 = spanR.ColorG0;
        out.ColorB1 = spanR.ColorB0;
        out.Z1 = spanR.Z0;
        out.W1 = spanR.W0;
    }
    else
    {
        const int i = y - spanR.I0;
        const int ifactor = mp_calc_factor_y(spanR, i);
        const int idiff = spanR.I1 - spanR.I0;

        out.Z1 = wBuffer
            ? int(mp_interp_z_wbuffer(
                  spanR.Z0, spanR.Z1, ifactor, MPFactorShiftY, true))
            : int(mp_interp_z_zbuffer(
                  spanR.Z0, spanR.Z1, i, spanR.IRecip, idiff, true));

        if (spanR.Linear == 0)
        {
            out.TexcoordU1 = mp_interp_attr_persp(
                spanR.TexcoordU0, spanR.TexcoordU1, ifactor, MPFactorShiftY);
            out.TexcoordV1 = mp_interp_attr_persp(
                spanR.TexcoordV0, spanR.TexcoordV1, ifactor, MPFactorShiftY);
            out.ColorR1 = mp_interp_attr_persp(
                spanR.ColorR0, spanR.ColorR1, ifactor, MPFactorShiftY);
            out.ColorG1 = mp_interp_attr_persp(
                spanR.ColorG0, spanR.ColorG1, ifactor, MPFactorShiftY);
            out.ColorB1 = mp_interp_attr_persp(
                spanR.ColorB0, spanR.ColorB1, ifactor, MPFactorShiftY);
            out.W1 = mp_interp_attr_persp(
                spanR.W0, spanR.W1, ifactor, MPFactorShiftY);
        }
        else
        {
            out.TexcoordU1 = mp_interp_attr_linear(
                spanR.TexcoordU0, spanR.TexcoordU1, i, spanR.IRecip, idiff, true);
            out.TexcoordV1 = mp_interp_attr_linear(
                spanR.TexcoordV0, spanR.TexcoordV1, i, spanR.IRecip, idiff, true);
            out.ColorR1 = mp_interp_attr_linear(
                spanR.ColorR0, spanR.ColorR1, i, spanR.IRecip, idiff, true);
            out.ColorG1 = mp_interp_attr_linear(
                spanR.ColorG0, spanR.ColorG1, i, spanR.IRecip, idiff, true);
            out.ColorB1 = mp_interp_attr_linear(
                spanR.ColorB0, spanR.ColorB1, i, spanR.IRecip, idiff, true);
            out.W1 = spanR.W0;
        }
    }

    if (out.W0 == out.W1 && ((out.W0 | out.W1) & 0x7F) == 0)
        out.Flags |= XSpanSetup_Linear;

    // W-buffering only needs XRecip on linear spans; Z-buffering always does.
    if ((!wBuffer || (out.Flags & XSpanSetup_Linear) != 0u) &&
        out.X1 != out.X0)
    {
        out.XRecip = int((1u << 30u) / uint(out.X1 - out.X0));
    }

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
        const uint localPolygonIdx = groupIdx * 32u + localIdx;
        const uint polygonIdx = config.firstPolygon + localPolygonIdx;
        if (localPolygonIdx < config.batchPolygonCount &&
            polygonIdx < config.numPolygons &&
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
        const uint polygonIdx = config.firstPolygon + groupIdx * 32u + bit;
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

        // A conservative host-side proof partitions consecutive polygons so
        // every batch fits this bounded work buffer without dropping a bit.
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
        const uint polygonIdx = config.firstPolygon + groupIdx * 32u + bit;
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

// MELONPRIME_METAL_COMPUTE_COMMAND_LIFETIME_V1
// This translation unit is part of the `core` target, which is built without
// -fobjc-arc. -[MTLCommandQueue commandBuffer] returns an autoreleased object,
// so a command buffer kept past the enclosing @autoreleasepool (for GetLine()'s
// completion wait, renderer reconfiguration and teardown) has to be retained
// explicitly. Without this the frame slot holds a dangling pointer as soon as
// the command completes and the pool drains.
void RetainFrameCommand(id<MTLCommandBuffer>& stored, id<MTLCommandBuffer> command)
{
    if (stored == command)
        return;
#if !__has_feature(objc_arc)
    [command retain];
    [stored release];
#endif
    stored = command;
}

void ReleaseFrameCommand(id<MTLCommandBuffer>& stored)
{
#if !__has_feature(objc_arc)
    [stored release];
#endif
    stored = nil;
}

// Same ownership rule for the long-lived resources: -newBufferWithLength: and
// -newTextureWithDescriptor: hand back +1 references, so reconfiguring a scale
// has to release the previous allocation instead of dropping it on the floor.
// At high internal resolutions the per-slot tile scratch is hundreds of MiB.
template <typename ObjectT>
void ReleaseMetalObject(ObjectT& object)
{
#if !__has_feature(objc_arc)
    [object release];
#endif
    object = nil;
}

// Externally owned objects (capture arrays owned by MetalRenderer) are retained
// while the compute renderer holds a reference, so re-creating the owner's
// resources cannot leave this renderer with a dangling texture.
template <typename ObjectT>
void RetainMetalObject(ObjectT& stored, ObjectT object)
{
    if (stored == object)
        return;
#if !__has_feature(objc_arc)
    [object retain];
    [stored release];
#endif
    stored = object;
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
    const int32_t x0 = positions[vertex][0];

    span.X0 = span.X1 = x0;
    span.XMin = span.XMax = x0;
    span.Y0 = span.Y1 = positions[vertex][1];

    const int32_t boundsXMin =
        RasterEdge::ConservativeRightVerticalMin(x0, side != 0);
    if (boundsXMin < renderPolygon.XMin)
    {
        renderPolygon.XMin = boundsXMin;
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
        span.XMax = span.XMin;
        minXY = maxXY = span.Y0;
    }

    const int32_t boundsXMin = RasterEdge::ConservativeRightVerticalMin(
        span.XMin, side && span.X0 == span.X1);
    if (boundsXMin < renderPolygon.XMin)
    {
        renderPolygon.XMin = boundsXMin;
        renderPolygon.XMinY = minXY;
    }
    if (span.XMax > renderPolygon.XMax)
    {
        renderPolygon.XMax = span.XMax;
        renderPolygon.XMaxY = maxXY;
    }

    const int32_t xlen = span.XMax + 1 - span.XMin;
    const int32_t ylen = span.Y1 - span.Y0;
    span.Increment = RasterEdge::CalculateSlopeIncrement(
        span.X0, span.X1, span.XMin, span.XMax, span.Y0, span.Y1);

    const bool xMajor = span.Increment > 0x40000;
    if (side)
    {
        if (xMajor)
            span.DxInitial = negative ? (0x20000 + 0x40000) : (span.Increment - 0x20000);
        else if (span.Increment != 0)
            span.DxInitial = negative ? 0x40000 : 0;
        else
            span.DxInitial = 0;
    }
    else
    {
        if (xMajor)
            span.DxInitial = negative ? ((span.Increment - 0x20000) + 0x40000) : 0x20000;
        else if (span.Increment != 0)
            span.DxInitial = negative ? 0x40000 : 0;
    }

    if (xMajor && xlen != 0)
        span.XCovIncr = (ylen << 10) / xlen;

    const int32_t interpolationOffset = RasterEdge::InterpolationOriginOffset(
        span.Increment, side != 0, negative);
    span.I0 = span.Y0 - interpolationOffset;
    span.I1 = span.Y1 - interpolationOffset;

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
        id<MTLTexture> FinalTexture = nil;
        // Phase 8Z: tile scratch and layer memory are owned per frame slot, so
        // slots no longer serialise on a single shared tile-memory allocation.
        id<MTLBuffer> ColorTiles = nil;
        id<MTLBuffer> DepthTiles = nil;
        id<MTLBuffer> AttrTiles = nil;
        id<MTLBuffer> DepthBlendColor = nil;
        id<MTLBuffer> DepthBlendDepth = nil;
        id<MTLBuffer> DepthBlendAttr = nil;
        id<MTLBuffer> DepthBlendWinner = nil;
        id<MTLBuffer> BlendContinuationState = nil;
        id<MTLBuffer> FinalColorBuffer = nil;
        id<MTLBuffer> NativeColorBuffer = nil;
        id<MTLTexture> NativeTexture = nil;
        bool VariantMetaSnapshotValid = false;
        uint64_t TextureMemoryVersion = 0;
        uint64_t TexturePaletteVersion = 0;
        bool ToonTableSnapshotValid = false;
        bool FinalTablesSnapshotValid = false;
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
    id<MTLComputePipelineState> TextureRasterPipeline = nil;
    id<MTLComputePipelineState> CompleteDepthBlendPipeline = nil;
    id<MTLComputePipelineState> CorrectCoveragePipeline = nil;
    id<MTLComputePipelineState> FinalPassPipeline = nil;
    id<MTLComputePipelineState> NativeResolvePipeline = nil;

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
    std::array<PolygonBatch, kMaxPolygons> PolygonBatches {};
    uint32_t PolygonBatchCount = 0;

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
    uint32_t MaxSetupIndices = 192 * kMaxPolygons;
    uint32_t TileWorkCapacity = 0;
    uint64_t TextureMemoryVersion = 1;
    uint64_t TexturePaletteVersion = 1;
    bool HiresCoordinates = false;
    bool Ready = false;
    bool SpanBinReady = false;
    bool TileRasterReady = false;
    bool DepthBlendReady = false;
    bool TextureVariantReady = false;
    bool FinalPassReady = false;
    bool CpuReadbackRequired = true;
    bool LastFrameComputeVisible = false;
    bool LastFrameUseHiRes3D = false;
    bool VisibleCutoverDisabled = false;
    bool LoggedVisibleCutover = false;
    bool LoggedVisibleFallback = false;
    uint32_t LastFrameEngineALayer = 1;
    int LastFrameRenderedScale = 1;
    std::atomic<bool> ComputeVisibleFault { false };
    bool LoggedOverflow = false;
    bool LoggedWorkCapacity = false;
    bool LoggedVariantOverflow = false;
    std::atomic<int> SubmittedFinalSlot { -1 };
    std::atomic<uint64_t> SubmittedFinalSerial { 0 };

    // GetLine() state. The native line buffer belongs to the slot that produced
    // the currently visible compute frame and is only resolved once per frame.
    int NativeLineSlot = -1;
    uint64_t NativeLineSerial = 0;
    bool NativeLineReady = false;
    std::array<uint32_t, 256u * 192u> NativeLineBuffer {};
    std::array<uint32_t, 256u> NativeScrolledLine {};
    RasterDifferential::State RasterDiff;
};

namespace
{

// Releases everything a frame slot owns. Called before re-allocating a slot for
// a new scale and on teardown; see ReleaseMetalObject() for why this is manual.
// Templated so the private nested FrameSlot type never has to be named here.
template <typename SlotT>
void ReleaseFrameSlotResources(SlotT& slot)
{
    ReleaseMetalObject(slot.Header);
    ReleaseMetalObject(slot.SetupIndices);
    ReleaseMetalObject(slot.YSpans);
    ReleaseMetalObject(slot.XSpans);
    ReleaseMetalObject(slot.Polygons);
    ReleaseMetalObject(slot.CoarseMask);
    ReleaseMetalObject(slot.FineMask);
    ReleaseMetalObject(slot.WorkOffsets);
    ReleaseMetalObject(slot.WorkDescs);
    ReleaseMetalObject(slot.VariantMetaBuffer);
    ReleaseMetalObject(slot.TextureMemoryBuffer);
    ReleaseMetalObject(slot.TexturePaletteBuffer);
    ReleaseMetalObject(slot.ToonTableBuffer);
    ReleaseMetalObject(slot.FinalTablesBuffer);
    ReleaseMetalObject(slot.FinalTexture);
    ReleaseMetalObject(slot.ColorTiles);
    ReleaseMetalObject(slot.DepthTiles);
    ReleaseMetalObject(slot.AttrTiles);
    ReleaseMetalObject(slot.DepthBlendColor);
    ReleaseMetalObject(slot.DepthBlendDepth);
    ReleaseMetalObject(slot.DepthBlendAttr);
    ReleaseMetalObject(slot.DepthBlendWinner);
    ReleaseMetalObject(slot.BlendContinuationState);
    ReleaseMetalObject(slot.FinalColorBuffer);
    ReleaseMetalObject(slot.NativeColorBuffer);
    ReleaseMetalObject(slot.NativeTexture);
    slot.VariantMetaSnapshotValid = false;
    slot.TextureMemoryVersion = 0;
    slot.TexturePaletteVersion = 0;
    slot.ToonTableSnapshotValid = false;
    slot.FinalTablesSnapshotValid = false;
}

} // namespace

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
        ReleaseFrameCommand(slot.LastCommand);
        ReleaseFrameSlotResources(slot);
    }

    // Device and Queue are borrowed from RasterReference and must not be
    // released here; everything below came from a +1 "new"/"retain".
    ReleaseMetalObject(State->Capture128Texture);
    ReleaseMetalObject(State->Capture256Texture);
    ReleaseMetalObject(State->DummyCapture128Texture);
    ReleaseMetalObject(State->DummyCapture256Texture);
    ReleaseMetalObject(State->ClearIndirectPipeline);
    ReleaseMetalObject(State->ClearCoarseMaskPipeline);
    ReleaseMetalObject(State->CalcOffsetsPipeline);
    ReleaseMetalObject(State->SortWorkPipeline);
    ReleaseMetalObject(State->SortWorkPolygonsPipeline);
    ReleaseMetalObject(State->InterpSpansPipeline);
    ReleaseMetalObject(State->BinCombinedPipeline);
    ReleaseMetalObject(State->TextureRasterPipeline);
    ReleaseMetalObject(State->CompleteDepthBlendPipeline);
    ReleaseMetalObject(State->CorrectCoveragePipeline);
    ReleaseMetalObject(State->FinalPassPipeline);
    ReleaseMetalObject(State->NativeResolvePipeline);
    ReleaseMetalObject(State->Library);
    ReleaseMetalObject(State->TexturedLibrary);
    ReleaseMetalObject(State->CompleteDepthBlendLibrary);
    ReleaseMetalObject(State->FinalPassLibrary);
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
    // The span/bin and textured-raster libraries share the DS fixed-point span
    // math and the SpanSetupY/SpanSetupX declarations.
    NSString* spanMath =
        [NSString stringWithUTF8String:kMetalComputeSpanMathSource];
    NSString* source = [spanMath stringByAppendingString:
        [NSString stringWithUTF8String:kMetalComputeSource]];
    State->Library = [State->Device newLibraryWithSource:source options:nil error:&error];
    if (!State->Library)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal compute: MSL compile failed: %s\n",
            message);
        return false;
    }

    NSString* texturedSource = [spanMath stringByAppendingString:
        [NSString stringWithUTF8String:kMetalComputeTexturedSource]];
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
    State->TextureRasterPipeline = BuildComputePipeline(
        State->Device,
        State->TexturedLibrary,
        @"mp_compute_rasterise_texture_variants");
    State->CompleteDepthBlendPipeline = BuildComputePipeline(
        State->Device,
        State->CompleteDepthBlendLibrary,
        @"mp_compute_depth_blend_complete");
    State->CorrectCoveragePipeline = BuildComputePipeline(
        State->Device,
        State->CompleteDepthBlendLibrary,
        @"mp_compute_correct_accepted_coverage");
    State->FinalPassPipeline = BuildComputePipeline(
        State->Device, State->FinalPassLibrary,
        @"mp_compute_final_pass");
    State->NativeResolvePipeline = BuildComputePipeline(
        State->Device, State->FinalPassLibrary,
        @"mp_compute_native_resolve");

    if (!State->ClearIndirectPipeline || !State->ClearCoarseMaskPipeline ||
        !State->CalcOffsetsPipeline || !State->SortWorkPipeline ||
        !State->SortWorkPolygonsPipeline || !State->InterpSpansPipeline ||
        !State->BinCombinedPipeline || !State->TextureRasterPipeline ||
        !State->CompleteDepthBlendPipeline || !State->CorrectCoveragePipeline ||
        !State->FinalPassPipeline ||
        !State->NativeResolvePipeline)
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
        State->TextureRasterPipeline.maxTotalThreadsPerThreadgroup,
        State->CompleteDepthBlendPipeline.maxTotalThreadsPerThreadgroup,
        State->CorrectCoveragePipeline.maxTotalThreadsPerThreadgroup,
        State->FinalPassPipeline.maxTotalThreadsPerThreadgroup,
        State->NativeResolvePipeline.maxTotalThreadsPerThreadgroup,
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
    // Retained alias: SetCaptureTextures() releases whatever is currently held
    // when it swaps in the real capture arrays.
    RetainMetalObject(State->Capture128Texture, State->DummyCapture128Texture);
    RetainMetalObject(State->Capture256Texture, State->DummyCapture256Texture);
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

    // Drop every reference into the slots before they are torn down. If this
    // function fails partway, GetLine() must not still believe a previous
    // compute frame is readable out of a released buffer.
    State->LastFrameComputeVisible = false;
    State->NativeLineReady = false;
    State->NativeLineSlot = -1;
    State->NativeLineSerial = 0;
    State->SubmittedFinalSlot.store(-1, std::memory_order_release);

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
        ReleaseFrameCommand(slot.LastCommand);
        // The GPU is idle for this slot now, so the previous allocation can go.
        // Skipping this leaks the whole per-slot working set on every scale
        // change, which at 4x is hundreds of MiB per slot.
        ReleaseFrameSlotResources(slot);
    }

    State->ScaleFactor = static_cast<uint32_t>(scale);
    State->ScreenWidth = 256u * State->ScaleFactor;
    State->ScreenHeight = 192u * State->ScaleFactor;

    uint32_t range = static_cast<uint32_t>((scale >= 5) + (scale >= 9));
    const NSUInteger maxRasterThreads = State->TextureRasterPipeline
        ? State->TextureRasterPipeline.maxTotalThreadsPerThreadgroup
        : 0u;
    while (range > 0u)
    {
        const NSUInteger candidateTileSize = 8u << range;
        if (candidateTileSize * candidateTileSize <= maxRasterThreads)
            break;
        range--;
    }
    State->TileSize = 8u << range;
    State->CoarseTileCountY = 4u + (scale >= 9 ? 2u : 0u);
    State->CoarseTileArea = kCoarseTileCountX * State->CoarseTileCountY;
    State->CoarseTileW = kCoarseTileCountX * State->TileSize;
    State->CoarseTileH = State->CoarseTileCountY * State->TileSize;
    State->TilesPerLine = State->ScreenWidth / State->TileSize;
    State->TileLines = State->ScreenHeight / State->TileSize;
    State->MaxSetupIndices = State->ScreenHeight * kMaxPolygons;

    // Binning and tile consumption must agree on one work count. Deriving
    // MaxWorkTiles from the tile-memory budget (instead of capping consumption
    // afterwards) removes the class of bug where binning emits work that the
    // raster/depth-blend passes silently ignore.
    const size_t tileArea = static_cast<size_t>(State->TileSize) * State->TileSize;
    const size_t bytesPerWork = tileArea * sizeof(uint32_t) * 3u;
    const uint32_t desiredWorkTiles =
        State->TilesPerLine * State->TileLines * kWorkTilesPerTile;
    uint32_t workTiles = desiredWorkTiles;
    if (bytesPerWork != 0)
    {
        const size_t budgetCapacity =
            std::max<size_t>(1, kTileMemoryBudgetBytesPerSlot / bytesPerWork);
        workTiles = static_cast<uint32_t>(
            std::min<size_t>(workTiles, budgetCapacity));
    }
    State->MaxWorkTiles = std::max<uint32_t>(workTiles, 1u);

    // The raster kernel runs one thread per tile pixel.
    const NSUInteger rasterThreadsRequired = tileArea;
    if (State->TextureRasterPipeline &&
        State->TextureRasterPipeline.maxTotalThreadsPerThreadgroup <
            rasterThreadsRequired)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute: tile raster needs %zu threads per "
            "group at scale=%d but the device allows %zu\n",
            static_cast<size_t>(rasterThreadsRequired), scale,
            static_cast<size_t>(
                State->TextureRasterPipeline.maxTotalThreadsPerThreadgroup));
        return false;
    }

    State->YSpanData.resize(kMaxYSpanSetups);
    State->SetupIndexData.resize(State->MaxSetupIndices);
    State->PolygonData.resize(kMaxPolygons);
    State->VariantData.reserve(kMaxVariants);
    State->VariantMetaData.resize(kMaxVariants);

    // Tile scratch dominates the allocation. Shrink the work budget (which also
    // shrinks the capacity, keeping binning and consumption in agreement)
    // instead of disabling compute outright when the device cannot back the
    // full request.
    for (;;)
    {
        const size_t attemptBytes =
            static_cast<size_t>(State->MaxWorkTiles) * tileArea * sizeof(uint32_t);
        bool allocated = true;
        for (auto& slot : State->Slots)
        {
            slot.ColorTiles =
                [State->Device newBufferWithLength:attemptBytes
                                           options:MTLResourceStorageModePrivate];
            slot.DepthTiles =
                [State->Device newBufferWithLength:attemptBytes
                                           options:MTLResourceStorageModePrivate];
            slot.AttrTiles =
                [State->Device newBufferWithLength:attemptBytes
                                           options:MTLResourceStorageModePrivate];
            if (!slot.ColorTiles || !slot.DepthTiles || !slot.AttrTiles)
            {
                allocated = false;
                break;
            }
        }
        if (allocated)
            break;

        for (auto& slot : State->Slots)
        {
            ReleaseMetalObject(slot.ColorTiles);
            ReleaseMetalObject(slot.DepthTiles);
            ReleaseMetalObject(slot.AttrTiles);
        }
        if (State->MaxWorkTiles <= 1024u)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute tile memory: allocation failed "
                "scale=%d even at minimum work capacity\n",
                scale);
            return false;
        }
        State->MaxWorkTiles >>= 1u;
    }

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
    const size_t tileBytes =
        static_cast<size_t>(State->MaxWorkTiles) * tileArea * sizeof(uint32_t);
    const size_t screenPixelBytes =
        static_cast<size_t>(State->ScreenWidth) *
        State->ScreenHeight *
        sizeof(uint32_t);
    const size_t twoLayerPixelBytes = screenPixelBytes * 2u;
    constexpr size_t kNativePixelBytes = 256u * 192u * sizeof(uint32_t);

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
        slot.VariantMetaSnapshotValid = false;
        slot.TextureMemoryVersion = 0;
        slot.TexturePaletteVersion = 0;
        slot.ToonTableSnapshotValid = false;
        slot.FinalTablesSnapshotValid = false;
        MTLTextureDescriptor* finalDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:State->ScreenWidth
                                            height:State->ScreenHeight
                                         mipmapped:NO];
        finalDescriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        finalDescriptor.storageMode = MTLStorageModePrivate;
        slot.FinalTexture = [State->Device newTextureWithDescriptor:finalDescriptor];

        slot.DepthBlendColor =
            [State->Device newBufferWithLength:twoLayerPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.DepthBlendDepth =
            [State->Device newBufferWithLength:twoLayerPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.DepthBlendAttr =
            [State->Device newBufferWithLength:twoLayerPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.DepthBlendWinner =
            [State->Device newBufferWithLength:twoLayerPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.BlendContinuationState =
            [State->Device newBufferWithLength:screenPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.FinalColorBuffer =
            [State->Device newBufferWithLength:screenPixelBytes
                                       options:MTLResourceStorageModePrivate];
        slot.NativeColorBuffer =
            [State->Device newBufferWithLength:kNativePixelBytes
                                       options:MTLResourceStorageModeShared];

        MTLTextureDescriptor* nativeDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:256
                                            height:192
                                         mipmapped:NO];
        nativeDescriptor.usage =
            MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        nativeDescriptor.storageMode = MTLStorageModePrivate;
        slot.NativeTexture = [State->Device newTextureWithDescriptor:nativeDescriptor];

        if (!slot.Header || !slot.SetupIndices || !slot.YSpans || !slot.XSpans ||
            !slot.Polygons || !slot.CoarseMask || !slot.FineMask ||
            !slot.WorkOffsets || !slot.WorkDescs || !slot.VariantMetaBuffer ||
            !slot.TextureMemoryBuffer || !slot.TexturePaletteBuffer ||
            !slot.ToonTableBuffer || !slot.FinalTablesBuffer ||
            !slot.FinalTexture || !slot.ColorTiles || !slot.DepthTiles ||
            !slot.AttrTiles || !slot.DepthBlendColor || !slot.DepthBlendDepth ||
            !slot.DepthBlendAttr || !slot.BlendContinuationState ||
            !slot.DepthBlendWinner ||
            !slot.FinalColorBuffer ||
            !slot.NativeColorBuffer || !slot.NativeTexture)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: buffer allocation failed "
                "scale=%d maxWorkTiles=%u tileMiB=%.1f\n",
                scale, State->MaxWorkTiles,
                static_cast<double>(tileBytes * 3u) / (1024.0 * 1024.0));
            return false;
        }
    }

    // Capacity and work count are now the same number by construction; the
    // GPU-side clamps remain as defence in depth only.
    State->TileWorkCapacity = State->MaxWorkTiles;
    State->SubmittedFinalSlot.store(-1, std::memory_order_release);
    State->SubmittedFinalSerial.store(0, std::memory_order_release);
    State->NativeLineReady = false;
    State->NativeLineSlot = -1;
    State->NativeLineSerial = 0;

    std::fprintf(stderr,
        "[MelonPrime] metal compute span/bin: configured scale=%d screen=%ux%u "
        "tile=%u grid=%ux%u maxWorkTiles=%u (requested=%u) maxXSpans=%u "
        "tileMemoryMiB=%.1f/slot slots=%u fullCoverage=%u\n",
        scale,
        State->ScreenWidth,
        State->ScreenHeight,
        State->TileSize,
        State->TilesPerLine,
        State->TileLines,
        State->MaxWorkTiles,
        desiredWorkTiles,
        State->MaxSetupIndices,
        static_cast<double>(tileBytes * 3u) / (1024.0 * 1024.0),
        kFrameSlotCount,
        State->MaxWorkTiles == desiredWorkTiles ? 1u : 0u);
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
        [encoder dispatchThreadgroupsWithIndirectBuffer:headerBuffer
                                  indirectBufferOffset:
                                      kSortWorkCountStart * sizeof(uint32_t)
                                  threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder endEncoding];
    }
    if (!CompleteCommandBuffer(workCommand, "foundation offset/sort self-test"))
        return false;

    if (header[kSortWorkCountStart + 0] != 1 ||
        header[kSortWorkCountStart + 1] != 1 ||
        header[kSortWorkCountStart + 2] != 1 ||
        header[kRasterWorkCountStart + 0] != polygonCount ||
        header[kRasterWorkCountStart + 1] != 1 ||
        header[kRasterWorkCountStart + 2] != 1 ||
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
    // Opaque (alpha 31) so the span is not treated as a wireframe outline.
    polygon.Attr = 31u << 16u;

    const SpanBinConfig spanConfig {
        1, 1, setupCount,
        64, 32, 8, 8, 4,
        8, 4, 64, 32,
        maxWorkTiles, kBinStride, kCoarseBinStride, 1,
        0, 0, maxWorkTiles, 0,
        0, 1
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
        [encoder setBuffer:polygonBuffer offset:0 atIndex:4];
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
    constexpr int32_t expectedXRecip = static_cast<int32_t>((1u << 30) / 31u);
    for (uint32_t i = 0; i < setupCount; i++)
    {
        // Software conditionally moves a non-coincident right vertical edge
        // left after both X values are known. Coverage remains 31 on both
        // unswapped sides and the edge runs put the inside interval one pixel in.
        const bool geometryOk = outX[i].X0 == 16 && outX[i].X1 == 47;
        const bool edgesOk =
            outX[i].InsideStart == 17 && outX[i].InsideEnd == 46 &&
            outX[i].EdgeCovL == 31 && outX[i].EdgeCovR == 31 &&
            outX[i].CovLInitial == 0 && outX[i].CovRInitial == 0;
        // Linear | Inside | Left | Right (both dummy edges carry W == 0).
        const bool flagsOk = outX[i].Flags == 0xFu;
        const bool recipOk = outX[i].XRecip == expectedXRecip;
        if (!geometryOk || !edgesOk || !flagsOk || !recipOk)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: interp self-test mismatch "
                "line=%u x=%d..%d inside=%d..%d cov=%d/%d covInit=%d/%d "
                "flags=%08x xrecip=%d\n",
                i, outX[i].X0, outX[i].X1,
                outX[i].InsideStart, outX[i].InsideEnd,
                outX[i].EdgeCovL, outX[i].EdgeCovR,
                outX[i].CovLInitial, outX[i].CovRInitial,
                outX[i].Flags, outX[i].XRecip);
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

void MetalComputeRenderer3D::Reset()
{
    RasterReference.Reset();
    if (!State)
        return;
    State->LoggedOverflow = false;
    State->LoggedWorkCapacity = false;
    State->LoggedVariantOverflow = false;
    State->LastFrameComputeVisible = false;
    State->VisibleCutoverDisabled = false;
    State->LoggedVisibleCutover = false;
    State->LoggedVisibleFallback = false;
    State->ComputeVisibleFault.store(false, std::memory_order_release);
    State->NativeLineReady = false;
    State->NativeLineSlot = -1;
    State->NativeLineSerial = 0;
    State->SubmittedFinalSlot.store(-1, std::memory_order_release);
}

void MetalComputeRenderer3D::SetThreaded(bool threaded) noexcept
{
    // Differential verification must consume the exact polygon/texture RAM
    // snapshot that the compute submission below will read.  A nested software
    // render thread can still be walking RenderPolygonRAM after the owning 3D
    // render thread releases earlier scanlines, allowing the next frame to
    // replace later polygons and producing a false cross-frame comparison.
    RasterReference.SetThreaded(
        RasterDifferential::Enabled() ? false : threaded);
}

bool MetalComputeRenderer3D::IsThreaded() const noexcept
{
    return RasterReference.IsThreaded();
}

void MetalComputeRenderer3D::SetScaleFactor(int scale) noexcept
{
    if (RasterDifferential::Enabled())
        scale = 1;
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

    const uint32_t inputPolygonCount =
        std::min<uint32_t>(GPU3D.RenderNumPolygons, kMaxPolygons);
    uint32_t polygonCount = 0;
    uint32_t numYSpans = 0;
    uint32_t numSetupIndices = 0;
    State->VariantData.clear();

    const bool enableTextureMaps = (GPU3D.RenderDispCnt & (1u << 0)) != 0;
    int captureInfo[16] = {};
    GPU.GetCaptureInfo_Texture(captureInfo);
    auto spanOverflow = [&]() -> bool {
        if (!State->LoggedOverflow)
        {
            State->LoggedOverflow = true;
            std::fprintf(stderr,
                "[MelonPrime] metal compute span/bin: frame input exceeded span budget ySpans=%u/%u xSpans=%u/%u; compute submission rejected\n",
                numYSpans,
                kMaxYSpanSetups,
                numSetupIndices,
                State->MaxSetupIndices);
        }
        return true;
    };

    for (uint32_t sourcePolygonIndex = 0;
         sourcePolygonIndex < inputPolygonCount; sourcePolygonIndex++)
    {
        Polygon* polygon = GPU3D.RenderPolygonRAM[sourcePolygonIndex];
        if (!polygon || polygon->Degenerate ||
            polygon->NumVertices < 2 || polygon->NumVertices > 10)
        {
            continue;
        }
        const uint32_t polygonIndex = polygonCount++;
        RenderPolygon& outputPolygon = State->PolygonData[polygonIndex];
        outputPolygon = {};
        outputPolygon.FirstXSpan = numSetupIndices;
        outputPolygon.Attr = polygon->Attr;
        outputPolygon.FacingView = polygon->FacingView ? 1u : 0u;

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
            if (State->VariantData.size() >= kMaxVariants)
            {
                // Folding extra variants onto index 255 would make earlier
                // polygons sample a later polygon's texture, because the
                // variant metadata slot is overwritten per polygon. Skip the
                // whole compute frame instead of rendering a wrong one.
                if (!State->LoggedVariantOverflow)
                {
                    State->LoggedVariantOverflow = true;
                    std::fprintf(stderr,
                        "[MelonPrime] metal compute: frame needs more than %u "
                        "texture variants; compute submission rejected "
                        "without backend fallback\n",
                        kMaxVariants);
                }
                return true;
            }
            variantIndex = static_cast<uint32_t>(State->VariantData.size());
            State->VariantData.push_back(key);
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
            // Native 1x output must use the DS-quantized coordinates that the
            // Software renderer consumes. HiresPosition preserves subpixel
            // detail for enlarged targets, but rounding it back down at 1x is
            // not guaranteed to reproduce FinalPosition (notably for moving
            // HUD geometry).
            if (State->HiresCoordinates && State->ScaleFactor > 1)
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
    {
        State->VariantData.push_back({});
        State->VariantMetaData[0] = {};
    }
    const uint32_t variantCount =
        static_cast<uint32_t>(State->VariantData.size());

    // Partition the ordered polygon stream into consecutive batches whose
    // conservative bounding-box tile sum fits the fixed tile scratch. The
    // exact bin test can only remove tiles, so no GPU-side work may be dropped.
    State->PolygonBatchCount = 0;
    uint32_t batchFirst = 0;
    uint32_t batchCount = 0;
    uint64_t batchTiles = 0;
    const int32_t tileSize = static_cast<int32_t>(State->TileSize);
    for (uint32_t polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++)
    {
        const RenderPolygon& polygon = State->PolygonData[polygonIndex];
        const int32_t minX = std::clamp(
            polygon.XMin, 0, static_cast<int32_t>(State->ScreenWidth) - 1);
        const int32_t maxX = std::clamp(
            polygon.XMax, 0, static_cast<int32_t>(State->ScreenWidth) - 1);
        const int32_t minY = std::clamp(
            polygon.YTop, 0, static_cast<int32_t>(State->ScreenHeight) - 1);
        const int32_t maxY = std::clamp(
            polygon.YBot - 1, 0, static_cast<int32_t>(State->ScreenHeight) - 1);
        uint64_t polygonTiles = 0;
        if (minX <= maxX && minY <= maxY)
        {
            polygonTiles =
                static_cast<uint64_t>(maxX / tileSize - minX / tileSize + 1) *
                static_cast<uint64_t>(maxY / tileSize - minY / tileSize + 1);
        }
        if (polygonTiles > State->MaxWorkTiles)
            return true;
        if (batchCount != 0 && batchTiles + polygonTiles > State->MaxWorkTiles)
        {
            State->PolygonBatches[State->PolygonBatchCount++] =
                { batchFirst, batchCount };
            batchFirst = polygonIndex;
            batchCount = 0;
            batchTiles = 0;
        }
        batchTiles += polygonTiles;
        batchCount++;
    }
    if (batchCount != 0)
    {
        State->PolygonBatches[State->PolygonBatchCount++] =
            { batchFirst, batchCount };
    }
    if (State->PolygonBatchCount == 0)
        State->PolygonBatches[State->PolygonBatchCount++] = { 0, 0 };

    {
        MetalComputeState::FrameSlot* slot = nullptr;
        uint32_t slotIndex = 0;
        while (!slot)
        {
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

            if (slot)
                break;

            // Metal Compute is the selected renderer, so temporary GPU
            // back-pressure must throttle emulation instead of switching one
            // visible frame to RasterReference. Wait for the oldest submitted
            // slot; the command queue remains ordered and the completed
            // handler makes the slot claimable on the next loop iteration.
            uint32_t oldestIndex = 0;
            uint64_t oldestGeneration =
                State->Slots[0].Generation.load(std::memory_order_acquire);
            for (uint32_t i = 1; i < kFrameSlotCount; i++)
            {
                const uint64_t generation =
                    State->Slots[i].Generation.load(std::memory_order_acquire);
                if (generation < oldestGeneration)
                {
                    oldestGeneration = generation;
                    oldestIndex = i;
                }
            }

            id<MTLCommandBuffer> oldestCommand =
                State->Slots[oldestIndex].LastCommand;
            if (!oldestCommand)
                return false;
            [oldestCommand waitUntilCompleted];
            if (oldestCommand.status != MTLCommandBufferStatusCompleted)
                return false;
            while (State->Slots[oldestIndex].InFlight.load(
                       std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
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
        std::array<uint32_t, 32> toonTable {};
        for (uint32_t index = 0; index < 32u; index++)
        {
            const uint32_t color = GPU3D.RenderToonTable[index];
            uint32_t r = (color << 1u) & 0x3Eu; if (r) r++;
            uint32_t g = (color >> 4u) & 0x3Eu; if (g) g++;
            uint32_t b = (color >> 9u) & 0x3Eu; if (b) b++;
            toonTable[index] = r | (g << 8u) | (b << 16u);
        }

        const size_t variantBytes =
            static_cast<size_t>(variantCount) * sizeof(VariantMeta);
        if (!UpdateSharedSnapshotIfChanged(
                slot->VariantMetaBuffer,
                State->VariantMetaData.data(),
                variantBytes,
                slot->VariantMetaSnapshotValid) ||
            !UpdateSharedSnapshotForVersion(
                slot->TextureMemoryBuffer,
                GPU.VRAMFlat_Texture,
                sizeof(GPU.VRAMFlat_Texture),
                State->TextureMemoryVersion,
                slot->TextureMemoryVersion) ||
            !UpdateSharedSnapshotForVersion(
                slot->TexturePaletteBuffer,
                GPU.VRAMFlat_TexPal,
                sizeof(GPU.VRAMFlat_TexPal),
                State->TexturePaletteVersion,
                slot->TexturePaletteVersion) ||
            !UpdateSharedSnapshotIfChanged(
                slot->ToonTableBuffer,
                toonTable.data(),
                sizeof(toonTable),
                slot->ToonTableSnapshotValid))
        {
            slot->InFlight.store(false, std::memory_order_release);
            return false;
        }
        std::memset([slot->Header contents], 0, slot->Header.length);

        // Binning only runs when there are X-spans to bin. Reporting zero
        // polygon groups in that case keeps the depth-blend pass from reading
        // fine-mask words this frame never wrote.
        const uint32_t coarseTilesX = State->ScreenWidth / State->CoarseTileW;
        const uint32_t coarseTilesY = State->ScreenHeight / State->CoarseTileH;
        const uint32_t tileCount = State->TilesPerLine * State->TileLines;

        const FoundationConfig foundationConfig {
            variantCount,
            State->MaxWorkTiles,
            tileCount,
            kRasteriseChunkSize,
        };
        const SpanBinConfig baseSpanConfig {
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
            0u,
            GPU3D.RenderAlphaRef,
            GPU3D.RenderDispCnt,
            State->TileWorkCapacity,
            polygonCount > 0 && GPU3D.RenderPolygonRAM[0] &&
                    GPU3D.RenderPolygonRAM[0]->WBuffer
                ? 1u
                : 0u,
            0u,
            0u,
        };

        uint32_t clearR = (GPU3D.RenderClearAttr1 << 1) & 0x3Eu;
        if (clearR) clearR++;
        uint32_t clearG = (GPU3D.RenderClearAttr1 >> 4) & 0x3Eu;
        if (clearG) clearG++;
        uint32_t clearB = (GPU3D.RenderClearAttr1 >> 9) & 0x3Eu;
        if (clearB) clearB++;
        const uint32_t clearA = (GPU3D.RenderClearAttr1 >> 16) & 0x1Fu;
        std::array<uint32_t, kFinalTableWords> finalTables {};
        for (uint32_t index = 0; index < 8u; index++)
            finalTables[index] = ConvertRGB555ToRGB6(GPU3D.RenderEdgeTable[index]);
        for (uint32_t index = 0; index < 34u; index++)
            finalTables[8u + index] = GPU3D.RenderFogDensityTable[index];
        if (!UpdateSharedSnapshotIfChanged(
                slot->FinalTablesBuffer,
                finalTables.data(),
                sizeof(finalTables),
                slot->FinalTablesSnapshotValid))
        {
            slot->InFlight.store(false, std::memory_order_release);
            return false;
        }

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

        const CompleteDepthBlendConfig baseDepthBlendConfig {
            State->ScreenWidth,
            State->ScreenHeight,
            State->TileSize,
            State->TilesPerLine,
            kBinStride,
            0u,
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
            baseSpanConfig.WBuffer,
            0u,
            0u,
            0u,
            numSetupIndices,
        };

        // Tile scratch and layer buffers belong to this slot, so a frame never
        // has to wait for the previous compute frame to retire.
        const bool submitDepthBlend = State->DepthBlendReady;
        const bool submitFinalPass =
            submitDepthBlend && State->FinalPassReady;

        id<MTLCommandBuffer> command = [State->Queue commandBuffer];
        if (!command)
        {
            slot->InFlight.store(false, std::memory_order_release);
            return false;
        }
        command.label = @"MelonPrime Metal Compute Frame";

        if (numSetupIndices > 0)
        {
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:State->InterpSpansPipeline];
            [encoder setBuffer:slot->SetupIndices offset:0 atIndex:0];
            [encoder setBuffer:slot->YSpans offset:0 atIndex:1];
            [encoder setBuffer:slot->XSpans offset:0 atIndex:2];
            [encoder setBytes:&baseSpanConfig length:sizeof(baseSpanConfig) atIndex:3];
            [encoder setBuffer:slot->Polygons offset:0 atIndex:4];
            [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(numSetupIndices, 32), 1, 1)
                     threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
            [encoder endEncoding];
        }
        for (uint32_t batchIndex = 0;
             batchIndex < State->PolygonBatchCount; batchIndex++)
        {
            const PolygonBatch batch = State->PolygonBatches[batchIndex];
            const uint32_t polygonGroups = numSetupIndices > 0
                ? DispatchGroups(batch.PolygonCount, 32) : 0u;
            SpanBinConfig spanConfig = baseSpanConfig;
            spanConfig.PolygonGroups = polygonGroups;
            spanConfig.FirstPolygon = batch.FirstPolygon;
            spanConfig.BatchPolygonCount = batch.PolygonCount;
            CompleteDepthBlendConfig depthBlendConfig = baseDepthBlendConfig;
            depthBlendConfig.PolygonGroups = polygonGroups;
            depthBlendConfig.FirstPolygon = batch.FirstPolygon;
            depthBlendConfig.Continuation = batchIndex != 0 ? 1u : 0u;
            depthBlendConfig.BatchPolygonCount = batch.PolygonCount;

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
            if (polygonGroups > 0)
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
            if (polygonGroups > 0)
            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->CalcOffsetsPipeline];
                [encoder setBuffer:slot->Header offset:0 atIndex:0];
                [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:1];
                [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(variantCount, 32), 1, 1)
                         threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
                [encoder endEncoding];
            }
            if (polygonGroups > 0)
            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->SortWorkPolygonsPipeline];
                [encoder setBuffer:slot->Header offset:0 atIndex:0];
                [encoder setBuffer:slot->Polygons offset:0 atIndex:1];
                [encoder setBuffer:slot->WorkDescs offset:0 atIndex:2];
                [encoder setBytes:&foundationConfig length:sizeof(foundationConfig) atIndex:3];
                [encoder dispatchThreadgroupsWithIndirectBuffer:slot->Header
                                          indirectBufferOffset:
                                              kSortWorkCountStart * sizeof(uint32_t)
                                          threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
                [encoder endEncoding];
            }
            if (State->TileRasterReady && polygonGroups > 0)
            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->TextureRasterPipeline];
                [encoder setBuffer:slot->Header offset:0 atIndex:0];
                [encoder setBuffer:slot->Polygons offset:0 atIndex:1];
                [encoder setBuffer:slot->XSpans offset:0 atIndex:2];
                [encoder setBuffer:slot->WorkDescs offset:0 atIndex:3];
                [encoder setBuffer:slot->VariantMetaBuffer offset:0 atIndex:4];
                [encoder setBuffer:slot->ColorTiles offset:0 atIndex:5];
                [encoder setBuffer:slot->DepthTiles offset:0 atIndex:6];
                [encoder setBuffer:slot->AttrTiles offset:0 atIndex:7];
                [encoder setBytes:&spanConfig length:sizeof(spanConfig) atIndex:9];
                [encoder setBuffer:slot->TextureMemoryBuffer offset:0 atIndex:10];
                [encoder setBuffer:slot->TexturePaletteBuffer offset:0 atIndex:11];
                [encoder setBuffer:slot->ToonTableBuffer offset:0 atIndex:12];
                [encoder setTexture:State->Capture128Texture atIndex:0];
                [encoder setTexture:State->Capture256Texture atIndex:1];
                [encoder dispatchThreadgroupsWithIndirectBuffer:slot->Header
                                          indirectBufferOffset:
                                              kRasterWorkCountStart * sizeof(uint32_t)
                                          threadsPerThreadgroup:
                                              MTLSizeMake(State->TileSize,
                                                          State->TileSize,
                                                          1)];
                [encoder endEncoding];
            }
            if (submitDepthBlend)
            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->CompleteDepthBlendPipeline];
                [encoder setBuffer:slot->FineMask offset:0 atIndex:0];
                [encoder setBuffer:slot->WorkOffsets offset:0 atIndex:1];
                [encoder setBuffer:slot->Polygons offset:0 atIndex:2];
                [encoder setBuffer:slot->ColorTiles offset:0 atIndex:3];
                [encoder setBuffer:slot->DepthTiles offset:0 atIndex:4];
                [encoder setBuffer:slot->AttrTiles offset:0 atIndex:5];
                [encoder setBuffer:slot->DepthBlendColor offset:0 atIndex:6];
                [encoder setBuffer:slot->DepthBlendDepth offset:0 atIndex:7];
                [encoder setBuffer:slot->DepthBlendAttr offset:0 atIndex:8];
                [encoder setBuffer:slot->BlendContinuationState offset:0 atIndex:9];
                [encoder setBytes:&depthBlendConfig length:sizeof(depthBlendConfig) atIndex:10];
                [encoder setBuffer:slot->TextureMemoryBuffer offset:0 atIndex:11];
                [encoder setBuffer:slot->DepthBlendWinner offset:0 atIndex:12];
                const uint32_t pixelCount = State->ScreenWidth * State->ScreenHeight;
                [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(pixelCount, 64), 1, 1)
                         threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [encoder endEncoding];

                if (State->ScaleFactor == 1 &&
                    (GPU3D.RenderDispCnt & (1u << 4u)) != 0u &&
                    numSetupIndices > 0)
                {
                    encoder = [command computeCommandEncoder];
                    [encoder setComputePipelineState:State->CorrectCoveragePipeline];
                    [encoder setBuffer:slot->FineMask offset:0 atIndex:0];
                    [encoder setBuffer:slot->WorkOffsets offset:0 atIndex:1];
                    [encoder setBuffer:slot->SetupIndices offset:0 atIndex:2];
                    [encoder setBuffer:slot->XSpans offset:0 atIndex:3];
                    [encoder setBuffer:slot->AttrTiles offset:0 atIndex:4];
                    [encoder setBuffer:slot->DepthBlendAttr offset:0 atIndex:5];
                    [encoder setBuffer:slot->DepthBlendWinner offset:0 atIndex:6];
                    [encoder setBytes:&depthBlendConfig length:sizeof(depthBlendConfig) atIndex:7];
                    [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(numSetupIndices, 64), 1, 1)
                             threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [encoder endEncoding];
                }
            }
        }
        if (submitFinalPass)
        {
            const uint32_t finalPixelCount =
                State->ScreenWidth * State->ScreenHeight;
            {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->FinalPassPipeline];
                [encoder setBuffer:slot->DepthBlendColor offset:0 atIndex:0];
                [encoder setBuffer:slot->DepthBlendDepth offset:0 atIndex:1];
                [encoder setBuffer:slot->DepthBlendAttr offset:0 atIndex:2];
                [encoder setBuffer:slot->FinalTablesBuffer offset:0 atIndex:4];
                [encoder setBytes:&finalPassConfig length:sizeof(finalPassConfig) atIndex:5];
                [encoder setBuffer:slot->FinalColorBuffer offset:0 atIndex:6];
                [encoder setTexture:slot->FinalTexture atIndex:0];
                [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(finalPixelCount, 64), 1, 1)
                         threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [encoder endEncoding];
            }
            {
                // Compute-owned native resolve: 256x192 texture for display
                // capture/ownership plus the shared buffer GetLine() reads.
                const NativeResolveConfig resolveConfig {
                    State->ScreenWidth,
                    State->ScreenHeight,
                    State->ScaleFactor,
                    0u,
                };
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:State->NativeResolvePipeline];
                [encoder setBuffer:slot->FinalColorBuffer offset:0 atIndex:0];
                [encoder setBuffer:slot->NativeColorBuffer offset:0 atIndex:1];
                [encoder setBytes:&resolveConfig length:sizeof(resolveConfig) atIndex:2];
                [encoder setTexture:slot->NativeTexture atIndex:0];
                [encoder dispatchThreadgroups:MTLSizeMake(DispatchGroups(256u * 192u, 64), 1, 1)
                         threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [encoder endEncoding];
            }
        }

        uint64_t serial =
            State->SubmittedFinalSerial.load(std::memory_order_acquire);
        if (submitFinalPass)
        {
            serial = State->SubmittedFinalSerial.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            State->SubmittedFinalSlot.store(
                slotIndex, std::memory_order_release);
            // GetLine() resolves lazily from this slot; the readback is only
            // performed if the software 2D compositor actually asks for it.
            State->NativeLineSlot = static_cast<int>(slotIndex);
            State->NativeLineSerial = serial;
            State->NativeLineReady = false;
        }

        const uint64_t generation =
            slot->Generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        MetalComputeState* state = State.get();
        RetainFrameCommand(slot->LastCommand, command);
        [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status != MTLCommandBufferStatusCompleted)
            {
                const char* message = completed.error
                    ? [[completed.error localizedDescription] UTF8String]
                    : "unknown command-buffer failure";
                state->ComputeVisibleFault.store(
                    true, std::memory_order_release);
                std::fprintf(stderr,
                    "[MelonPrime] metal compute span/bin: frame=%llu "
                    "GPU failure: %s; compute output halted without "
                    "RasterReference fallback\n",
                    static_cast<unsigned long long>(serial), message);
            }
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
        const bool computeVisible = MetalComputeVisibleEnabled() && State;

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
                    State->LastFrameComputeVisible = ComputeFinalReady();
                std::fprintf(stderr,
                    "[MelonPrime] metal compute: failed to resize "
                    "support target to scale=%d; retaining compute output\n",
                    requestedScale);
                if (!computeVisible)
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
                "[MelonPrime] metal compute visible: halted after GPU "
                "command failure without RasterReference fallback; restart "
                "renderer to retry\n");
        }

        const bool visibleRequested =
            computeVisible;
        const bool visibleEligible =
            visibleRequested &&
            !State->VisibleCutoverDisabled &&
            State->Ready &&
            State->SpanBinReady &&
            State->TileRasterReady &&
            State->DepthBlendReady &&
            State->TextureVariantReady &&
            State->FinalPassReady;
        // CpuReadbackRequired no longer gates the compute path: it only selects
        // how the compute result is consumed (GetLine() readback versus the
        // GPU-resident texture), which GetLine() handles below.
        if (visibleRequested && GPU3D.AbortFrame)
        {
            // A genuine mid-render VCOUNT disruption owns no valid new 3D
            // image. GetLine() supplies the renderer-neutral zero scanline
            // directly; do not switch this one frame to RasterReference.
            State->LastFrameComputeVisible = ComputeFinalReady();
            State->NativeLineReady = false;
            return;
        }

        if (!visibleEligible)
        {
            if (visibleRequested)
            {
                State->LastFrameComputeVisible = ComputeFinalReady();
                State->NativeLineReady = false;
                if (MetalComputeFallbackTraceEnabled())
                {
                    std::fprintf(stderr,
                        "[MetalComputeFallbackTrace] kind=prevented-ineligible "
                        "scale=%d engineALayer=%u ready=%u span=%u "
                        "tile=%u depth=%u variant=%u final=%u fault=%u\n",
                        requestedScale,
                        State->LastFrameEngineALayer,
                        State->Ready ? 1u : 0u,
                        State->SpanBinReady ? 1u : 0u,
                        State->TileRasterReady ? 1u : 0u,
                        State->DepthBlendReady ? 1u : 0u,
                        State->TextureVariantReady ? 1u : 0u,
                        State->FinalPassReady ? 1u : 0u,
                        State->VisibleCutoverDisabled ? 1u : 0u);
                }
                return;
            }

            RasterReference.RenderFrame();
            return;
        }

        // MELONPRIME_METAL_COMPUTE_VRAM_COHERENCY_V1
        // The compute renderer reads texture/palette VRAM and the clear bitmap
        // straight out of the flat VRAM mirrors, and RasterReference no longer
        // runs on compute frames, so this path owns the shared dirty tracking.
        // DeriveState() is destructive, hence exactly one consumer per frame.
        auto textureDirty =
            GPU.VRAMDirty_Texture.DeriveState(GPU.VRAMMap_Texture, GPU);
        auto texPalDirty =
            GPU.VRAMDirty_TexPal.DeriveState(GPU.VRAMMap_TexPal, GPU);
        const bool textureChanged =
            GPU.MakeVRAMFlat_TextureCoherent(textureDirty);
        const bool texPalChanged =
            GPU.MakeVRAMFlat_TexPalCoherent(texPalDirty);
        const bool vramChanged = textureChanged || texPalChanged;
        if (textureChanged)
            State->TextureMemoryVersion++;
        if (texPalChanged)
            State->TexturePaletteVersion++;

        const bool rasterDifferential =
            RasterDifferential::Enabled() && requestedScale == 1;
        if (rasterDifferential)
            RasterReference.RenderSoftwareReferenceFrame();

        const bool previousFrameWasCompute =
            State->LastFrameComputeVisible;
        if (GPU3D.RenderFrameIdentical &&
            !vramChanged &&
            previousFrameWasCompute &&
            ComputeFinalReady())
        {
            State->LastFrameComputeVisible = true;
            if (rasterDifferential)
                State->RasterDiff.CompareFrame(
                    *this, RasterReference.GetSoftwareReference(), "MetalCompute");
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
            if (rasterDifferential)
                State->RasterDiff.CompareFrame(
                    *this, RasterReference.GetSoftwareReference(), "MetalCompute");
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

        State->LastFrameComputeVisible = ComputeFinalReady();
        State->NativeLineReady = false;
        if (MetalComputeFallbackTraceEnabled())
        {
            std::fprintf(stderr,
                "[MetalComputeFallbackTrace] kind=prevented-no-final scale=%d "
                "engineALayer=%u submitted=%u serialBefore=%llu "
                "serialAfter=%llu inFlight=%u%u%u\n",
                requestedScale,
                State->LastFrameEngineALayer,
                submitted ? 1u : 0u,
                static_cast<unsigned long long>(serialBefore),
                static_cast<unsigned long long>(serialAfter),
                State->Slots[0].InFlight.load(std::memory_order_acquire) ? 1u : 0u,
                State->Slots[1].InFlight.load(std::memory_order_acquire) ? 1u : 0u,
                State->Slots[2].InFlight.load(std::memory_order_acquire) ? 1u : 0u);
        }
        if (!State->LoggedVisibleFallback)
        {
            State->LoggedVisibleFallback = true;
            std::fprintf(stderr,
                "[MelonPrime] metal compute visible: no final slot submitted; "
                "retaining previous compute output without RasterReference "
                "fallback\n");
        }
    }
}

void MetalComputeRenderer3D::FinishRendering()
{
    if (!MetalComputeVisibleEnabled())
        RasterReference.FinishRendering();
}

void MetalComputeRenderer3D::RestartFrame()
{
    if (!MetalComputeVisibleEnabled())
        RasterReference.RestartFrame();
}

u32* MetalComputeRenderer3D::GetLine(int line)
{
    static u32 zeroLine[256] = {};

    if (MetalComputeVisibleEnabled() && GPU3D.AbortFrame)
        return zeroLine;

    // A visible compute frame owns its own scanlines. The DS-native resolve is
    // produced on the GPU inside the frame's command buffer, so this only has
    // to wait once per frame and copy the shared buffer.
    if (State && State->LastFrameComputeVisible && !GPU3D.AbortFrame &&
        ResolveComputeNativeLines())
    {
        u32* rawLine =
            &State->NativeLineBuffer[static_cast<size_t>(line) * 256u];

        const u16 xpos = GPU3D.RenderXPos;
        if (xpos == 0)
            return rawLine;

        if (xpos & 0x100)
        {
            int i = 0, j = xpos;
            for (; j < 512; i++, j++)
                State->NativeScrolledLine[i] = 0;
            for (j = 0; i < 256; i++, j++)
                State->NativeScrolledLine[i] = rawLine[j];
        }
        else
        {
            int i = 0, j = xpos;
            for (; j < 256; i++, j++)
                State->NativeScrolledLine[i] = rawLine[j];
            for (; i < 256; i++)
                State->NativeScrolledLine[i] = 0;
        }
        return State->NativeScrolledLine.data();
    }

    // Once Metal Compute is selected, a missing compute result must remain an
    // explicit compute failure. Returning RasterReference here would create a
    // one-frame backend switch and the alternating-screen flash this renderer
    // is required to avoid.
    if (MetalComputeVisibleEnabled())
        return zeroLine;

    return RasterReference.GetLine(line);
}

void* MetalComputeRenderer3D::GetColorTargetTexture() const noexcept
{
    if (MetalComputeVisibleEnabled())
    {
        if (State && State->LastFrameComputeVisible)
            return GetComputeFinalTexture();
        // The outer Metal renderer needs a device/size-bearing texture while
        // it configures presentation, before the first compute submission
        // exists. This bootstrap target is never published after cutover.
        if (State && !State->LoggedVisibleCutover)
            return RasterReference.GetColorTargetTexture();
        return nullptr;
    }
    return RasterReference.GetColorTargetTexture();
}

void* MetalComputeRenderer3D::GetNativeResolveTexture() const noexcept
{
    if (MetalComputeVisibleEnabled())
    {
        if (State && State->LastFrameComputeVisible)
            return GetComputeNativeResolveTexture();
        if (State && !State->LoggedVisibleCutover)
            return RasterReference.GetNativeResolveTexture();
        return nullptr;
    }
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
