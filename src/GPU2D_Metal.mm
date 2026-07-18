// MelonPrimeDS - experimental Metal 2D renderer scaffold (Metal-plan Phase 4)
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_2D_HUD_PARITY_V1
// MELONPRIME_METAL_2D_SCANLINE_SNAPSHOT_V1
// MELONPRIME_METAL_2D_DIRECT_SEGMENTED_CUTOVER_V2
// MELONPRIME_METAL_2D_HOT_PATH_CLEANUP_V1
// MELONPRIME_METAL_2D_PERSISTENT_UPLOAD_RING_V1
// MELONPRIME_METAL_2D_DIRECT_SNAPSHOT_RING_V1
// MELONPRIME_METAL_2D_CHANGE_DRIVEN_SPRITE_CACHE_V1
// MELONPRIME_METAL_2D_SHADOW_PATH_REMOVAL_V1
// MELONPRIME_METAL_2D_LEGACY_FULL_FRAME_REMOVAL_V1

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU.h"
#include "GPU2D_Metal.h"
#include "MelonPrimeMetalLibrary.h"
#include "MetalContext.h"
#include "NDS.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace melonDS
{

namespace
{
constexpr int kScreenW = 256;
constexpr int kScreenH = 192;
constexpr int kBGLayerCount = 22;




bool MetalSegmented2DVisibleEnabled()
{
    return true;
}

bool MetalSegmented2DSnapshotEnabled()
{
    // Direct renderer state: no diagnostic environment switch.
    return true;
}

constexpr size_t MetalAlignConstantStride(size_t size)
{
    return (size + 255u) & ~size_t(255u);
}

uint64_t MetalSnapshotHash(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ull;
    constexpr uint64_t prime = 1099511628211ull;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

constexpr uint8_t kBGBaseIndex[4][4] = {
    {2, 10, 6, 14},
    {0, 4, 16, 20},
    {0, 4, 12, 16},
    {18, 19, 12, 16},
};

struct LayerVertex
{
    float position[2];
    float texcoord[2];
};

constexpr LayerVertex kLayerQuad[] = {
    {{0.0f, 1.0f}, {0.0f, 1.0f}},
    {{1.0f, 0.0f}, {1.0f, 0.0f}},
    {{1.0f, 1.0f}, {1.0f, 1.0f}},
    {{0.0f, 1.0f}, {0.0f, 1.0f}},
    {{0.0f, 0.0f}, {0.0f, 0.0f}},
    {{1.0f, 0.0f}, {1.0f, 0.0f}},
};

NSString* const kMetal2DLayerShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexIn {\n"
     "    float2 position [[attribute(0)]];\n"
     "    float2 texcoord [[attribute(1)]];\n"
     "};\n"
     "struct VOut {\n"
     "    float4 position [[position]];\n"
     "    float2 texcoord;\n"
     "};\n"
     "struct BGConfig {\n"
     "    uint2 size;\n"
     "    uint type;\n"
     "    uint palOffset;\n"
     "    uint tileOffset;\n"
     "    uint mapOffset;\n"
     "    uint clamp;\n"
     "    uint pad0;\n"
     "};\n"
     "struct LayerConfig {\n"
     "    uint vramMask;\n"
     "    uint3 pad0;\n"
     "    BGConfig bgConfig[4];\n"
     "};\n"
     "vertex VOut mp2d_layer_vs(VertexIn in [[stage_in]]) {\n"
     "    VOut out;\n"
     "    out.position = float4((in.position * 2.0) - 1.0, 0.0, 1.0);\n"
     "    out.texcoord = in.texcoord;\n"
     "    return out;\n"
     "}\n"
     "uint vramRead8(texture2d<uint> vram, constant LayerConfig& config, uint addr) {\n"
     "    return vram.read(uint2(addr & 0x3ffu, (addr >> 10) & config.vramMask)).r;\n"
     "}\n"
     "uint vramRead16(texture2d<uint> vram, constant LayerConfig& config, uint addr) {\n"
     "    uint lo = vramRead8(vram, config, addr);\n"
     "    uint hi = vramRead8(vram, config, addr + 1u);\n"
     "    return lo | (hi << 8);\n"
     "}\n"
     "float4 paletteRead(texture2d<uint> palette, uint row, uint id, bool transparent) {\n"
     "    uint raw = palette.read(uint2(id & 0xffu, row)).r;\n"
     "    float3 rgb = float3(float(raw & 0x1fu), float((raw >> 5) & 0x1fu), float((raw >> 10) & 0x1fu)) / 31.0;\n"
     "    return float4(rgb, transparent ? 0.0 : 1.0);\n"
     "}\n"
     "float4 bgText16(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram, texture2d<uint> palette) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + (((coord.x >> 3) & 0x1fu) << 1) + (((coord.y >> 3) & 0x1fu) << 6);\n"
     "    if (bg.size.y == 512u) {\n"
     "        if (bg.size.x == 512u) mapOffset += (((coord.x >> 8) & 0x1u) << 11) + (((coord.y >> 8) & 0x1u) << 12);\n"
     "        else mapOffset += (((coord.y >> 8) & 0x1u) << 11);\n"
     "    } else if (bg.size.x == 512u) mapOffset += (((coord.x >> 8) & 0x1u) << 11);\n"
     "    uint mapVal = vramRead16(vram, config, mapOffset);\n"
     "    uint tileOffset = (bg.tileOffset << 1) + ((mapVal & 0x3ffu) << 6);\n"
     "    tileOffset += ((mapVal & (1u << 10)) != 0u) ? (7u - (coord.x & 0x7u)) : (coord.x & 0x7u);\n"
     "    tileOffset += ((mapVal & (1u << 11)) != 0u) ? ((7u - (coord.y & 0x7u)) << 3) : ((coord.y & 0x7u) << 3);\n"
     "    uint col = vramRead8(vram, config, tileOffset >> 1);\n"
     "    col = ((tileOffset & 0x1u) != 0u) ? (col >> 4) : (col & 0xfu);\n"
     "    col += (mapVal >> 12) << 4;\n"
     "    return paletteRead(palette, bg.palOffset, col, (col & 0xfu) == 0u);\n"
     "}\n"
     "float4 bgText256(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram, texture2d<uint> palette) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + (((coord.x >> 3) & 0x1fu) << 1) + (((coord.y >> 3) & 0x1fu) << 6);\n"
     "    if (bg.size.y == 512u) {\n"
     "        if (bg.size.x == 512u) mapOffset += (((coord.x >> 8) & 0x1u) << 11) + (((coord.y >> 8) & 0x1u) << 12);\n"
     "        else mapOffset += (((coord.y >> 8) & 0x1u) << 11);\n"
     "    } else if (bg.size.x == 512u) mapOffset += (((coord.x >> 8) & 0x1u) << 11);\n"
     "    uint mapVal = vramRead16(vram, config, mapOffset);\n"
     "    uint tileOffset = bg.tileOffset + ((mapVal & 0x3ffu) << 6);\n"
     "    tileOffset += ((mapVal & (1u << 10)) != 0u) ? (7u - (coord.x & 0x7u)) : (coord.x & 0x7u);\n"
     "    tileOffset += ((mapVal & (1u << 11)) != 0u) ? ((7u - (coord.y & 0x7u)) << 3) : ((coord.y & 0x7u) << 3);\n"
     "    uint col = vramRead8(vram, config, tileOffset);\n"
     "    uint pal = (bg.palOffset != 0u) ? (mapVal >> 12) : 0u;\n"
     "    return paletteRead(palette, bg.palOffset + pal, col, col == 0u);\n"
     "}\n"
     "float4 bgAffine256(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram, texture2d<uint> palette) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + (coord.x >> 3) + ((coord.y >> 3) * (bg.size.x >> 3));\n"
     "    uint mapVal = vramRead8(vram, config, mapOffset);\n"
     "    uint tileOffset = bg.tileOffset + (mapVal << 6) + ((coord.y & 0x7u) << 3) + (coord.x & 0x7u);\n"
     "    uint col = vramRead8(vram, config, tileOffset);\n"
     "    return paletteRead(palette, bg.palOffset, col, col == 0u);\n"
     "}\n"
     "float4 bgExtended256(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram, texture2d<uint> palette) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + (((coord.x >> 3) + ((coord.y >> 3) * (bg.size.x >> 3))) << 1);\n"
     "    uint mapVal = vramRead16(vram, config, mapOffset);\n"
     "    uint tileOffset = bg.tileOffset + ((mapVal & 0x3ffu) << 6);\n"
     "    tileOffset += ((mapVal & (1u << 10)) != 0u) ? (7u - (coord.x & 0x7u)) : (coord.x & 0x7u);\n"
     "    tileOffset += ((mapVal & (1u << 11)) != 0u) ? ((7u - (coord.y & 0x7u)) << 3) : ((coord.y & 0x7u) << 3);\n"
     "    uint col = vramRead8(vram, config, tileOffset);\n"
     "    uint pal = (bg.palOffset != 0u) ? (mapVal >> 12) : 0u;\n"
     "    return paletteRead(palette, bg.palOffset + pal, col, col == 0u);\n"
     "}\n"
     "float4 bgBitmap256(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram, texture2d<uint> palette) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + coord.x + (coord.y * bg.size.x);\n"
     "    uint col = vramRead8(vram, config, mapOffset);\n"
     "    return paletteRead(palette, bg.palOffset, col, col == 0u);\n"
     "}\n"
     "float4 bgBitmapDirect(uint layer, uint2 coord, constant LayerConfig& config, texture2d<uint> vram) {\n"
     "    constant BGConfig& bg = config.bgConfig[layer];\n"
     "    uint mapOffset = bg.mapOffset + ((coord.x + (coord.y * bg.size.x)) << 1);\n"
     "    uint col = vramRead16(vram, config, mapOffset);\n"
     "    float r = float((col << 1) & 0x3eu) / 63.0;\n"
     "    float g = float((col >> 4) & 0x3eu) / 63.0;\n"
     "    float b = float((col >> 9) & 0x3eu) / 63.0;\n"
     "    return float4(r, g, b, float(col >> 15));\n"
     "}\n"
     "fragment float4 mp2d_layer_fs(VOut in [[stage_in]],\n"
     "                              constant LayerConfig& config [[buffer(0)]],\n"
     "                              constant uint& curBG [[buffer(1)]],\n"
     "                              texture2d<uint> vram [[texture(0)]],\n"
     "                              texture2d<uint> palette [[texture(1)]]) {\n"
     "    constant BGConfig& bg = config.bgConfig[curBG];\n"
     "    uint2 coord = uint2(in.texcoord * float2(bg.size));\n"
     "    if (bg.type == 0u) return bgText16(curBG, coord, config, vram, palette);\n"
     "    if (bg.type == 1u) return bgText256(curBG, coord, config, vram, palette);\n"
     "    if (bg.type == 2u) return bgAffine256(curBG, coord, config, vram, palette);\n"
     "    if (bg.type == 3u) return bgExtended256(curBG, coord, config, vram, palette);\n"
     "    if (bg.type == 4u) return bgBitmap256(curBG, coord, config, vram, palette);\n"
     "    if (bg.type == 5u) return bgBitmapDirect(curBG, coord, config, vram);\n"
     "    return float4(0.0, 0.0, 0.0, 0.0);\n"
     "}\n";

#include "GPU2D_MetalFullGpuShaders.inc"

struct LayerConfigCpu
{
    uint32_t vramMask = 0;
    uint32_t pad0[3] = {};
    struct BGConfig
    {
        uint32_t size[2] = {};
        uint32_t type = 0;
        uint32_t palOffset = 0;
        uint32_t tileOffset = 0;
        uint32_t mapOffset = 0;
        uint32_t clamp = 0;
        uint32_t pad0[1] = {};
    } bgConfig[4];
};

struct SpriteConfigCpu
{
    uint32_t vramMask = 0;
    uint32_t pad0[3] = {};
    int32_t rotscale[32][4] = {};
    struct OAMConfig
    {
        int32_t position[2] = {};
        int32_t flip[2] = {};
        int32_t size[2] = {};
        int32_t boundSize[2] = {};
        uint32_t objMode = 0;
        uint32_t type = 0;
        uint32_t palOffset = 0;
        uint32_t tileOffset = 0;
        uint32_t tileStride = 0;
        uint32_t rotscale = 0;
        uint32_t bgPrio = 0;
        uint32_t mosaic = 0;
    } oam[128];
};

struct ScanlineConfigCpu
{
    struct Scanline
    {
        int32_t bgOffset[4][4] = {};
        int32_t bgRotscale[2][4] = {};
        uint32_t backColor = 0;
        uint32_t winRegs = 0;
        uint32_t winMask = 0;
        uint32_t pad0[1] = {};
        int32_t winPos[4] = {};
        uint32_t bgMosaicEnable[4] = {};
        int32_t mosaicSize[4] = {};

        // Layer enables, priority and blending are latched per visible line.
        // Sampling them once at VBlank can leave BG0/3D visible while hiding
        // the game's BG/OBJ HUD layers.
        uint32_t bgPrio[4] = {};
        uint32_t enableOBJ = 0;
        uint32_t enable3D = 0;
        uint32_t blendCnt = 0;
        uint32_t blendEffect = 0;
        uint32_t blendCoef[4] = {};
    } scanline[192];
};

struct SpriteScanlineConfigCpu
{
    int32_t mosaicLine[192] = {};
};

struct LayerScanlineSnapshotCpu
{
    LayerConfigCpu line[192] = {};
};

struct SpriteScanlineMetaCpu
{
    uint32_t numSprites = 0;
    uint32_t useMosaic = 0;
    uint64_t configHash = 0;
};

struct SpriteScanlineMetaFrameCpu
{
    SpriteScanlineMetaCpu line[192] = {};
};

struct CompositorConfigCpu
{
    uint32_t bgPrio[4] = {};
    uint32_t enableOBJ = 0;
    uint32_t enable3D = 0;
    uint32_t blendCnt = 0;
    uint32_t blendEffect = 0;
    uint32_t blendCoef[4] = {};
};

static_assert((sizeof(LayerConfigCpu) & 15) == 0);
static_assert((sizeof(SpriteConfigCpu) & 15) == 0);
static_assert((sizeof(ScanlineConfigCpu) & 15) == 0);
static_assert((sizeof(SpriteScanlineConfigCpu) & 15) == 0);
static_assert((sizeof(LayerScanlineSnapshotCpu) & 15) == 0);
static_assert((sizeof(SpriteScanlineMetaFrameCpu) & 15) == 0);
static_assert((sizeof(CompositorConfigCpu) & 15) == 0);


struct MetalSegmentedUploadLayout
{
    NSUInteger SpriteFrameLength = 0;
    NSUInteger SpriteFrameOffset = 0;
    NSUInteger ScanlineFrameOffset = 0;
    NSUInteger SpriteScanlineFrameOffset = 0;
    NSUInteger FrameLength = 0;
};

MetalSegmentedUploadLayout MetalMakeSegmentedUploadLayout(
    size_t spriteSnapshotStride) noexcept
{
    MetalSegmentedUploadLayout layout;
    layout.SpriteFrameLength =
        static_cast<NSUInteger>(spriteSnapshotStride) *
        static_cast<NSUInteger>(kScreenH);
    layout.SpriteFrameOffset = 0;
    layout.ScanlineFrameOffset =
        static_cast<NSUInteger>(
            MetalAlignConstantStride(layout.SpriteFrameLength));
    layout.SpriteScanlineFrameOffset =
        static_cast<NSUInteger>(
            MetalAlignConstantStride(
                layout.ScanlineFrameOffset +
                sizeof(ScanlineConfigCpu)));
    layout.FrameLength =
        static_cast<NSUInteger>(
            MetalAlignConstantStride(
                layout.SpriteScanlineFrameOffset +
                sizeof(SpriteScanlineConfigCpu)));
    return layout;
}

struct MetalSegmentedUploadRing
{
    static constexpr uint32_t SlotCount = 6;

    std::array<id<MTLBuffer>, SlotCount> Buffers {};
    std::atomic<uint32_t> BusyMask {0};
    std::atomic<uint32_t> NextSlot {0};
    NSUInteger FrameLength = 0;

    bool Ready() const noexcept
    {
        if (FrameLength == 0)
            return false;
        for (id<MTLBuffer> buffer : Buffers)
        {
            if (!buffer || buffer.length < FrameLength)
                return false;
        }
        return true;
    }
};


int32_t MetalReserveSegmentedUploadSlot(
    const std::shared_ptr<MetalSegmentedUploadRing>& ring) noexcept
{
    if (!ring || !ring->Ready())
        return -1;

    const uint32_t start =
        ring->NextSlot.fetch_add(
            1u,
            std::memory_order_relaxed) %
        MetalSegmentedUploadRing::SlotCount;

    for (uint32_t offset = 0;
         offset < MetalSegmentedUploadRing::SlotCount;
         offset++)
    {
        const uint32_t slot =
            (start + offset) %
            MetalSegmentedUploadRing::SlotCount;
        const uint32_t bit = 1u << slot;
        const uint32_t previous =
            ring->BusyMask.fetch_or(
                bit,
                std::memory_order_acq_rel);
        if ((previous & bit) == 0u)
            return static_cast<int32_t>(slot);
    }

    return -1;
}

void MetalReleaseSegmentedUploadSlot(
    const std::shared_ptr<MetalSegmentedUploadRing>& ring,
    int32_t slot) noexcept
{
    if (!ring || slot < 0 ||
        slot >= static_cast<int32_t>(
            MetalSegmentedUploadRing::SlotCount))
    {
        return;
    }

    ring->BusyMask.fetch_and(
        ~(1u << static_cast<uint32_t>(slot)),
        std::memory_order_release);
}

template <typename DirtyState>
void UploadMetalDirtyRows(
    id<MTLTexture> texture,
    const uint8_t* source,
    DirtyState& dirty,
    bool forceFull)
{
    if (!texture || !source)
        return;

    static_assert(VRAMDirtyGranularity == 512);
    const NSUInteger width = texture.width;
    const NSUInteger height = texture.height;
    if (width == 0 || height == 0 ||
        (width % VRAMDirtyGranularity) != 0)
    {
        return;
    }

    const uint32_t dirtyUnitsPerRow =
        static_cast<uint32_t>(width / VRAMDirtyGranularity);
    NSUInteger row = 0;

    while (row < height)
    {
        const bool rowDirty =
            forceFull ||
            dirty.CheckRange(
                static_cast<uint32_t>(row) * dirtyUnitsPerRow,
                dirtyUnitsPerRow);
        if (!rowDirty)
        {
            row++;
            continue;
        }

        const NSUInteger startRow = row;
        row++;
        while (row < height)
        {
            const bool nextDirty =
                forceFull ||
                dirty.CheckRange(
                    static_cast<uint32_t>(row) * dirtyUnitsPerRow,
                    dirtyUnitsPerRow);
            if (!nextDirty)
                break;
            row++;
        }

        const NSUInteger rowCount = row - startRow;
        [texture replaceRegion:MTLRegionMake2D(
                                   0, startRow, width, rowCount)
                   mipmapLevel:0
                     withBytes:source + (startRow * width)
                   bytesPerRow:width];
    }

}
}

struct MetalRenderer2D::Metal2DState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> LayerLibrary = nil;
    id<MTLRenderPipelineState> LayerPipeline = nil;
    id<MTLBuffer> LayerVertexBuffer = nil;
    id<MTLLibrary> FullGpuLibrary = nil;
    id<MTLRenderPipelineState> SpritePipeline = nil;
    id<MTLRenderPipelineState> SpriteMosaicFlagsPipeline = nil;
    id<MTLRenderPipelineState> SpriteWindowPipeline = nil;
    id<MTLRenderPipelineState> CompositorPipeline = nil;
    id<MTLDepthStencilState> SpriteDepthState = nil;
    id<MTLSamplerState> RepeatSampler = nil;
    id<MTLTexture> DummyColorTexture = nil;
    id<MTLTexture> OutputTex = nil;
    id<MTLTexture> OBJLayerTex = nil;
    id<MTLTexture> OBJDepthTex = nil;
    id<MTLTexture> AllBGLayerTex[kBGLayerCount] = {};
    id<MTLTexture> BGLayerTex[4] = {};
    id<MTLTexture> VRAMTexBG = nil;
    id<MTLTexture> VRAMTexOBJ = nil;
    id<MTLTexture> PalTexBG = nil;
    id<MTLTexture> PalTexOBJ = nil;
    id<MTLTexture> MosaicTex = nil;
    id<MTLTexture> SpriteTex = nil;
    id<MTLBuffer> LayerConfigBuffer = nil;
    id<MTLBuffer> SpriteConfigBuffer = nil;
    id<MTLBuffer> ScanlineConfigBuffer = nil;
    id<MTLBuffer> SpriteScanlineConfigBuffer = nil;
    id<MTLBuffer> LayerScanlineSnapshotBuffer = nil;
    id<MTLBuffer> SpriteScanlineMetaBuffer = nil;
    id<MTLBuffer> CompositorConfigBuffer = nil;
    std::shared_ptr<MetalSegmentedUploadRing> SegmentedUploadRing;
    std::shared_ptr<MetalSegmentedUploadRing> SegmentedCaptureRing;
    int32_t SegmentedCaptureSlot = -1;
    bool SegmentedCaptureAttempted = false;
    LayerConfigCpu LayerConfig;
    SpriteConfigCpu SpriteConfig;
    CompositorConfigCpu CompositorConfig;
    std::array<uint64_t, kScreenH> LayerSnapshotHash {};
    std::array<uint64_t, kScreenH> SpriteSnapshotHash {};
    std::array<uint8_t, kScreenH> LayerSnapshotValid {};
    std::array<uint8_t, kScreenH> SpriteSnapshotValid {};
    std::array<uint32_t, kScreenH> SpriteSnapshotCount {};
    std::array<uint8_t, kScreenH> SpriteSnapshotMosaic {};
    size_t SpriteSnapshotStride = 0;
    int LayerSnapshotLastLine = -1;
    int SpriteSnapshotLastLine = -1;
    // MELONPRIME_METAL_FRAME_BOOTSTRAP_V1: renderer-owned monotonic epoch
    // from MetalRenderer::Start3DRendering (not NDS::NumFrames). 0 means no
    // active reservation.
    uint64_t SnapshotFrameEpoch = 0;
    bool SnapshotBuffersReady = false;
    bool SegmentedRenderReady = false;
    bool SegmentedFrameOutputCleared = false;
    bool LoggedSnapshotAllocation = false;
    // MELONPRIME_METAL_PER_INSTANCE_DIAGNOSTICS_V1: was a function-static in
    // GPU2D_MetalFullGpuMethods.inc's same-frame capture-hazard diagnostic,
    // which mixed counts across MetalRenderer2D instances (and raced if two
    // instances' emu threads hit it concurrently). See
    // RenderSegmentedGpuFrame()'s !allowCaptureTextures branch.
    uint32_t SameFrameCaptureHazardEnteredCount = 0;
    bool LoggedSameFrameCaptureHazard = false;
    uint32_t BGVRAMRange[4][4] = {};
    int NumSprites = 0;
    bool SpriteUseMosaic = false;

    // Phase 8P: cache raw OBJ source state and normalize OAM only when the
    // source changes. Per-line work consumes precomputed active masks.
    std::array<uint16_t, 512> SpriteSourceOAM {};
    std::array<int, 16> SpriteSourceCaptureInfo {};
    uint32_t SpriteSourceDispCnt = 0;
    int32_t SpriteSourceMosaicWidth = 0;
    uint32_t SpriteSourceVramMask = 0;
    bool SpriteSourceCacheValid = false;
    SpriteConfigCpu SpriteStaticConfig {};
    std::array<SpriteConfigCpu::OAMConfig, 128>
        SpriteNormalizedOAM {};
    std::array<std::array<uint64_t, 2>, kScreenH>
        SpriteActiveMask {};

    int Scale = 0;
    bool FullGpuReady = false;
    bool BGUploadInitialized = false;
    bool OBJUploadInitialized = false;
    bool BGPaletteUploadInitialized = false;
    bool OBJPaletteUploadInitialized = false;
    bool LoggedFirstAllocation = false;
};

MetalRenderer2D::MetalRenderer2D(melonDS::GPU2D& gpu2D) noexcept
    : Renderer2D(gpu2D),
      State(std::make_unique<Metal2DState>())
{
}

MetalRenderer2D::~MetalRenderer2D() = default;

bool MetalRenderer2D::BuildLayerPipeline() noexcept
{
    if (!State || !State->Device)
        return false;

    Metal2DState& state = *State;
    if (state.LayerLibrary && state.LayerPipeline && state.LayerVertexBuffer)
        return true;

    NSError* error = nil;
    state.LayerLibrary = [state.Device newLibraryWithSource:kMetal2DLayerShaderSource options:nil error:&error];
    if (!state.LayerLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to compile layer shaders: %s\n", message);
        return false;
    }

    MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
    vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[0].offset = offsetof(LayerVertex, position);
    vertexDesc.attributes[0].bufferIndex = 0;
    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].offset = offsetof(LayerVertex, texcoord);
    vertexDesc.attributes[1].bufferIndex = 0;
    vertexDesc.layouts[0].stride = sizeof(LayerVertex);
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = [state.LayerLibrary newFunctionWithName:@"mp2d_layer_vs"];
    pipelineDesc.fragmentFunction = [state.LayerLibrary newFunctionWithName:@"mp2d_layer_fs"];
    pipelineDesc.vertexDescriptor = vertexDesc;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    state.LayerPipeline = [state.Device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!state.LayerPipeline)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create layer pipeline: %s\n", message);
        return false;
    }

    state.LayerVertexBuffer =
        [state.Device newBufferWithBytes:kLayerQuad
                                  length:sizeof(kLayerQuad)
                                 options:MTLResourceStorageModeShared];
    if (!state.LayerVertexBuffer)
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create layer vertex buffer\n");
        return false;
    }

    return true;
}

bool MetalRenderer2D::PrerenderConfiguredLayers() noexcept
{
    if (!State || !State->Queue || !State->LayerPipeline || !State->LayerVertexBuffer)
        return false;

    Metal2DState& state = *State;
    id<MTLCommandBuffer> commandBuffer = [state.Queue commandBuffer];
    if (!commandBuffer)
        return false;

    for (int layer = 0; layer < 4; layer++)
    {
        const auto& cfg = state.LayerConfig.bgConfig[layer];
        id<MTLTexture> target = state.BGLayerTex[layer];
        if (!target || cfg.type >= 6 || cfg.size[0] == 0 || cfg.size[1] == 0)
            continue;

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (!encoder)
            return false;

        [encoder setRenderPipelineState:state.LayerPipeline];
        [encoder setVertexBuffer:state.LayerVertexBuffer offset:0 atIndex:0];
        [encoder setFragmentBuffer:state.LayerConfigBuffer offset:0 atIndex:0];
        uint32_t layerIndex = static_cast<uint32_t>(layer);
        [encoder setFragmentBytes:&layerIndex length:sizeof(layerIndex) atIndex:1];
        [encoder setFragmentTexture:state.VRAMTexBG atIndex:0];
        [encoder setFragmentTexture:state.PalTexBG atIndex:1];
        [encoder setViewport:(MTLViewport){0.0,
                                           0.0,
                                           static_cast<double>(cfg.size[0]),
                                           static_cast<double>(cfg.size[1]),
                                           0.0,
                                           1.0}];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        [encoder endEncoding];
    }

    // All Metal 2D work uses the 3D renderer's command queue. Queue
    // ordering replaces the old CPU wait and keeps the frame GPU-resident.
    [commandBuffer commit];
    return true;
}

bool MetalRenderer2D::Configure(void* preferredDevice, void* preferredQueue, int scale) noexcept
{
    if (!State)
        return false;

    Metal2DState& state = *State;
    id<MTLDevice> preferredMetalDevice =
        preferredDevice ? (__bridge id<MTLDevice>)preferredDevice : nil;
    id<MTLCommandQueue> preferredMetalQueue =
        preferredQueue ? (__bridge id<MTLCommandQueue>)preferredQueue : nil;
    if (preferredMetalQueue && preferredMetalDevice &&
        preferredMetalQueue.device != preferredMetalDevice)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: preferred queue/device mismatch\n");
        return false;
    }
    if (preferredMetalDevice && state.Device && state.Device != preferredMetalDevice)
    {
        state.OutputTex = nil;
        state.LayerLibrary = nil;
        state.LayerPipeline = nil;
        state.LayerVertexBuffer = nil;
        state.FullGpuLibrary = nil;
        state.SpritePipeline = nil;
        state.SpriteMosaicFlagsPipeline = nil;
        state.SpriteWindowPipeline = nil;
        state.CompositorPipeline = nil;
        state.SpriteDepthState = nil;
        state.RepeatSampler = nil;
        state.DummyColorTexture = nil;
        state.FullGpuReady = false;
        state.BGUploadInitialized = false;
        state.OBJUploadInitialized = false;
        state.BGPaletteUploadInitialized = false;
        state.OBJPaletteUploadInitialized = false;
        state.OBJLayerTex = nil;
        state.OBJDepthTex = nil;
        for (id<MTLTexture>& texture : state.AllBGLayerTex)
            texture = nil;
        for (id<MTLTexture>& texture : state.BGLayerTex)
            texture = nil;
        state.VRAMTexBG = nil;
        state.VRAMTexOBJ = nil;
        state.PalTexBG = nil;
        state.PalTexOBJ = nil;
        state.MosaicTex = nil;
        state.SpriteTex = nil;
        state.LayerConfigBuffer = nil;
        state.SpriteConfigBuffer = nil;
        state.ScanlineConfigBuffer = nil;
        state.SpriteScanlineConfigBuffer = nil;
        state.CompositorConfigBuffer = nil;
        MetalReleaseSegmentedUploadSlot(
            state.SegmentedCaptureRing,
            state.SegmentedCaptureSlot);
        state.SegmentedCaptureRing.reset();
        state.SegmentedCaptureSlot = -1;
        state.SegmentedCaptureAttempted = false;
        state.SegmentedUploadRing.reset();
        state.SpriteSourceCacheValid = false;
        state.Scale = 0;
        state.Device = preferredMetalDevice;
        state.Queue = nil;
    }

    if (!state.Device)
    {
        state.Device = preferredMetalDevice
            ? preferredMetalDevice
            : (__bridge id<MTLDevice>)MelonPrimeSharedMetalDeviceHandle();
        if (!state.Device)
        {
            std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create Metal device\n");
            return false;
        }
    }
    if (preferredMetalQueue)
    {
        state.Queue = preferredMetalQueue;
    }
    else if (!state.Queue)
    {
        state.Queue = [state.Device newCommandQueue];
        if (!state.Queue)
        {
            std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create command queue\n");
            return false;
        }
    }

    ScaleFactor = std::max(1, scale);
    if (state.Queue && state.OutputTex && state.OBJLayerTex && state.OBJDepthTex &&
        state.LayerLibrary && state.LayerPipeline && state.LayerVertexBuffer &&
        state.AllBGLayerTex[0] && state.VRAMTexBG && state.VRAMTexOBJ &&
        state.PalTexBG && state.PalTexOBJ && state.MosaicTex && state.SpriteTex &&
        state.LayerConfigBuffer && state.SpriteConfigBuffer && state.ScanlineConfigBuffer &&
        state.SpriteScanlineConfigBuffer && state.CompositorConfigBuffer &&
        state.SegmentedUploadRing &&
        state.SegmentedUploadRing->Ready() &&
        state.FullGpuReady && state.Scale == ScaleFactor)
    {
        return true;
    }

    if (!BuildLayerPipeline())
        return false;
    if (!BuildFullGpuPipelines())
        return false;

    const NSUInteger width = static_cast<NSUInteger>(kScreenW * ScaleFactor);
    const NSUInteger height = static_cast<NSUInteger>(kScreenH * ScaleFactor);

    MTLTextureDescriptor* outputDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    outputDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> newOutput = [state.Device newTextureWithDescriptor:outputDesc];
    MTLTextureDescriptor* objLayerDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objLayerDesc.textureType = MTLTextureType2DArray;
    objLayerDesc.arrayLength = 2;
    objLayerDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objLayerDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJLayer = [state.Device newTextureWithDescriptor:objLayerDesc];

    MTLTextureDescriptor* objDepthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objDepthDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objDepthDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJDepth = [state.Device newTextureWithDescriptor:objDepthDesc];

    MTLTextureDescriptor* bgVRAMDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                                           width:1024
                                                          height:(GPU2D.Num == 0) ? 512 : 128
                                                       mipmapped:NO];
    bgVRAMDesc.usage = MTLTextureUsageShaderRead;
    bgVRAMDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newVRAMBG = [state.Device newTextureWithDescriptor:bgVRAMDesc];

    MTLTextureDescriptor* objVRAMDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                                           width:1024
                                                          height:(GPU2D.Num == 0) ? 256 : 128
                                                       mipmapped:NO];
    objVRAMDesc.usage = MTLTextureUsageShaderRead;
    objVRAMDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newVRAMOBJ = [state.Device newTextureWithDescriptor:objVRAMDesc];

    MTLTextureDescriptor* bgPalDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                                           width:256
                                                          height:1 + (4 * 16)
                                                       mipmapped:NO];
    bgPalDesc.usage = MTLTextureUsageShaderRead;
    bgPalDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newPalBG = [state.Device newTextureWithDescriptor:bgPalDesc];

    MTLTextureDescriptor* objPalDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                                           width:256
                                                          height:1 + 16
                                                       mipmapped:NO];
    objPalDesc.usage = MTLTextureUsageShaderRead;
    objPalDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newPalOBJ = [state.Device newTextureWithDescriptor:objPalDesc];

    MTLTextureDescriptor* mosaicDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Sint
                                                           width:256
                                                          height:16
                                                       mipmapped:NO];
    mosaicDesc.usage = MTLTextureUsageShaderRead;
    mosaicDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> newMosaic = [state.Device newTextureWithDescriptor:mosaicDesc];

    MTLTextureDescriptor* spriteDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1024
                                                          height:512
                                                       mipmapped:NO];
    spriteDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    spriteDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newSprite = [state.Device newTextureWithDescriptor:spriteDesc];

    id<MTLBuffer> newLayerConfig =
        [state.Device newBufferWithLength:sizeof(LayerConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newSpriteConfig =
        [state.Device newBufferWithLength:sizeof(SpriteConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newScanlineConfig =
        [state.Device newBufferWithLength:sizeof(ScanlineConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newSpriteScanlineConfig =
        [state.Device newBufferWithLength:sizeof(SpriteScanlineConfigCpu) options:MTLResourceStorageModeShared];
    id<MTLBuffer> newCompositorConfig =
        [state.Device newBufferWithLength:sizeof(CompositorConfigCpu) options:MTLResourceStorageModeShared];

    const bool snapshotRequested = MetalSegmented2DSnapshotEnabled();
    const size_t spriteSnapshotStride =
        MetalAlignConstantStride(sizeof(SpriteConfigCpu));
    id<MTLBuffer> newLayerScanlineSnapshot = nil;
    id<MTLBuffer> newSpriteScanlineMeta = nil;
    if (snapshotRequested)
    {
        newLayerScanlineSnapshot =
            [state.Device newBufferWithLength:sizeof(LayerScanlineSnapshotCpu)
                                      options:MTLResourceStorageModeShared];
        newSpriteScanlineMeta =
            [state.Device newBufferWithLength:sizeof(SpriteScanlineMetaFrameCpu)
                                      options:MTLResourceStorageModeShared];
    }

    std::shared_ptr<MetalSegmentedUploadRing>
        newSegmentedUploadRing;
    if (snapshotRequested)
    {
        const MetalSegmentedUploadLayout uploadLayout =
            MetalMakeSegmentedUploadLayout(
                spriteSnapshotStride);
        newSegmentedUploadRing =
            std::make_shared<MetalSegmentedUploadRing>();
        newSegmentedUploadRing->FrameLength =
            uploadLayout.FrameLength;
        for (id<MTLBuffer>& buffer :
             newSegmentedUploadRing->Buffers)
        {
            buffer = [state.Device
                newBufferWithLength:uploadLayout.FrameLength
                            options:MTLResourceStorageModeShared];
        }
        if (!newSegmentedUploadRing->Ready())
            newSegmentedUploadRing.reset();
    }

    id<MTLTexture> newBGLayers[kBGLayerCount] = {};
    const uint16_t bgSizes[8][3] = {
        {128, 128, 2},
        {256, 256, 4},
        {256, 512, 4},
        {512, 256, 4},
        {512, 512, 4},
        {512, 1024, 1},
        {1024, 512, 1},
        {1024, 1024, 2},
    };

    int bgLayer = 0;
    for (const auto& size : bgSizes)
    {
        for (int variant = 0; variant < size[2]; variant++)
        {
            MTLTextureDescriptor* bgDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                    width:size[0]
                                                                   height:size[1]
                                                                mipmapped:NO];
            bgDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            bgDesc.storageMode = MTLStorageModePrivate;
            newBGLayers[bgLayer++] = [state.Device newTextureWithDescriptor:bgDesc];
        }
    }

    bool bgLayersReady = bgLayer == kBGLayerCount;
    for (id<MTLTexture> texture : newBGLayers)
        bgLayersReady = bgLayersReady && texture;

    if (!newOutput || !newOBJLayer || !newOBJDepth || !newVRAMBG || !newVRAMOBJ ||
        !newPalBG || !newPalOBJ || !newMosaic || !newSprite ||
        !newLayerConfig || !newSpriteConfig || !newScanlineConfig ||
        !newSpriteScanlineConfig || !newCompositorConfig || !bgLayersReady ||
        (snapshotRequested &&
         (!newLayerScanlineSnapshot ||
          !newSpriteScanlineMeta ||
          !newSegmentedUploadRing)))
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to allocate scaffold targets for engine %u\n", GPU2D.Num);
        return false;
    }

    int8_t mosaicPixels[256 * 16] = {};
    for (int m = 0; m < 16; m++)
    {
        int mosx = 0;
        for (int x = 0; x < 256; x++)
        {
            mosaicPixels[(m * 256) + x] = static_cast<int8_t>(mosx);
            mosx = (mosx == m) ? 0 : (mosx + 1);
        }
    }
    [newMosaic replaceRegion:MTLRegionMake2D(0, 0, 256, 16)
                 mipmapLevel:0
                   withBytes:mosaicPixels
                 bytesPerRow:256];

    state.OutputTex = newOutput;
    state.OBJLayerTex = newOBJLayer;
    state.OBJDepthTex = newOBJDepth;
    for (int i = 0; i < kBGLayerCount; i++)
        state.AllBGLayerTex[i] = newBGLayers[i];
    state.VRAMTexBG = newVRAMBG;
    state.VRAMTexOBJ = newVRAMOBJ;
    state.PalTexBG = newPalBG;
    state.PalTexOBJ = newPalOBJ;
    state.MosaicTex = newMosaic;
    state.SpriteTex = newSprite;
    state.LayerConfigBuffer = newLayerConfig;
    state.SpriteConfigBuffer = newSpriteConfig;
    state.ScanlineConfigBuffer = newScanlineConfig;
    state.SpriteScanlineConfigBuffer = newSpriteScanlineConfig;
    state.LayerScanlineSnapshotBuffer = newLayerScanlineSnapshot;
    state.SpriteScanlineMetaBuffer = newSpriteScanlineMeta;
    state.CompositorConfigBuffer = newCompositorConfig;
    MetalReleaseSegmentedUploadSlot(
        state.SegmentedCaptureRing,
        state.SegmentedCaptureSlot);
    state.SegmentedCaptureRing.reset();
    state.SegmentedCaptureSlot = -1;
    state.SegmentedCaptureAttempted = false;
    state.SegmentedUploadRing =
        std::move(newSegmentedUploadRing);
    state.SpriteSnapshotStride = spriteSnapshotStride;
    state.SnapshotBuffersReady =
        snapshotRequested &&
        newLayerScanlineSnapshot &&
        newSpriteScanlineMeta &&
        state.SegmentedUploadRing &&
        state.SegmentedUploadRing->Ready();
    state.SegmentedRenderReady =
        state.SnapshotBuffersReady &&
        state.SegmentedUploadRing &&
        state.SegmentedUploadRing->Ready() &&
        state.FullGpuReady &&
        state.OutputTex &&
        state.OBJLayerTex &&
        state.OBJDepthTex;
    state.LayerSnapshotValid.fill(0);
    state.SpriteSnapshotValid.fill(0);
    state.LayerSnapshotLastLine = -1;
    state.SpriteSnapshotLastLine = -1;
    state.Scale = ScaleFactor;
    state.BGUploadInitialized = false;
    state.OBJUploadInitialized = false;
    state.BGPaletteUploadInitialized = false;
    state.OBJPaletteUploadInitialized = false;
    if (!UploadRawVRAMInputs())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to upload initial raw VRAM inputs for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!UploadPaletteInputs())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to upload initial palette inputs for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!RefreshLayerConfig())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to refresh initial layer config for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!RefreshSpriteConfig(0, kScreenH))
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to refresh initial sprite config for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!RefreshCompositorConfig())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to refresh initial compositor config for engine %u\n", GPU2D.Num);
        return false;
    }
    if (!PrerenderConfiguredLayers())
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to prerender initial BG layer scaffold for engine %u\n", GPU2D.Num);
        return false;
    }

    if (state.SnapshotBuffersReady &&
        !state.LoggedSnapshotAllocation)
    {
        state.LoggedSnapshotAllocation = true;
        std::fprintf(
            stderr,
            "[MelonPrime] metal segmented 2d snapshot: allocated "
            "engine=%u layerBytes=%zu spriteBytes=%zu "
            "spriteStride=%zu metaBytes=%zu visiblePath=unchanged\n",
            GPU2D.Num,
            sizeof(LayerScanlineSnapshotCpu),
            state.SpriteSnapshotStride *
                static_cast<size_t>(kScreenH),
            state.SpriteSnapshotStride,
            sizeof(SpriteScanlineMetaFrameCpu));
    }

    if (!state.LoggedFirstAllocation)
    {
        state.LoggedFirstAllocation = true;
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: scaffold targets engine=%u scale=%d size=%zux%zu bgLayers=%d objLayers=2 configBuffers=5 visible=0\n",
            GPU2D.Num,
            state.Scale,
            static_cast<size_t>(width),
            static_cast<size_t>(height),
            kBGLayerCount);
    }

    return true;
}

void MetalRenderer2D::DrawScanline(u32 line)
{
    CaptureScanlineState(static_cast<int>(line));
}

void MetalRenderer2D::DrawSprites(u32 line)
{
    CaptureSpriteScanlineState(static_cast<int>(line));
}

bool MetalRenderer2D::UploadRawVRAMInputs() noexcept
{
    if (!State || !State->VRAMTexBG || !State->VRAMTexOBJ)
        return false;

    uint8_t* bgVRAM = nullptr;
    uint32_t bgMask = 0;
    GPU2D.GetBGVRAM(bgVRAM, bgMask);
    if (!bgVRAM ||
        (static_cast<size_t>(bgMask) + 1u) !=
            static_cast<size_t>(State->VRAMTexBG.width) *
            static_cast<size_t>(State->VRAMTexBG.height))
    {
        return false;
    }

    uint8_t* objVRAM = nullptr;
    uint32_t objMask = 0;
    GPU2D.GetOBJVRAM(objVRAM, objMask);
    if (!objVRAM ||
        (static_cast<size_t>(objMask) + 1u) !=
            static_cast<size_t>(State->VRAMTexOBJ.width) *
            static_cast<size_t>(State->VRAMTexOBJ.height))
    {
        return false;
    }

    if (GPU2D.Num == 0)
    {
        auto bgDirty =
            GPU.VRAMDirty_ABG.DeriveState(
                GPU.VRAMMap_ABG, GPU);
        (void)GPU.MakeVRAMFlat_ABGCoherent(bgDirty);
        UploadMetalDirtyRows(
            State->VRAMTexBG,
            bgVRAM,
            bgDirty,
            !State->BGUploadInitialized);

        auto objDirty =
            GPU.VRAMDirty_AOBJ.DeriveState(
                GPU.VRAMMap_AOBJ, GPU);
        (void)GPU.MakeVRAMFlat_AOBJCoherent(objDirty);
        UploadMetalDirtyRows(
            State->VRAMTexOBJ,
            objVRAM,
            objDirty,
            !State->OBJUploadInitialized);
    }
    else
    {
        auto bgDirty =
            GPU.VRAMDirty_BBG.DeriveState(
                GPU.VRAMMap_BBG, GPU);
        (void)GPU.MakeVRAMFlat_BBGCoherent(bgDirty);
        UploadMetalDirtyRows(
            State->VRAMTexBG,
            bgVRAM,
            bgDirty,
            !State->BGUploadInitialized);

        auto objDirty =
            GPU.VRAMDirty_BOBJ.DeriveState(
                GPU.VRAMMap_BOBJ, GPU);
        (void)GPU.MakeVRAMFlat_BOBJCoherent(objDirty);
        UploadMetalDirtyRows(
            State->VRAMTexOBJ,
            objVRAM,
            objDirty,
            !State->OBJUploadInitialized);
    }

    State->BGUploadInitialized = true;
    State->OBJUploadInitialized = true;
    return true;
}

bool MetalRenderer2D::UploadPaletteInputs() noexcept
{
    if (!State || !State->PalTexBG || !State->PalTexOBJ)
        return false;

    bool bgExtChanged = false;
    bool objExtChanged = false;

    if (GPU2D.Num == 0)
    {
        auto bgExtDirty =
            GPU.VRAMDirty_ABGExtPal.DeriveState(
                GPU.VRAMMap_ABGExtPal, GPU);
        (void)GPU.MakeVRAMFlat_ABGExtPalCoherent(bgExtDirty);
        bgExtChanged = bgExtDirty.CheckRange(0, 64);

        auto objExtDirty =
            GPU.VRAMDirty_AOBJExtPal.DeriveState(
                &GPU.VRAMMap_AOBJExtPal, GPU);
        (void)GPU.MakeVRAMFlat_AOBJExtPalCoherent(objExtDirty);
        objExtChanged = objExtDirty.CheckRange(0, 16);
    }
    else
    {
        auto bgExtDirty =
            GPU.VRAMDirty_BBGExtPal.DeriveState(
                GPU.VRAMMap_BBGExtPal, GPU);
        (void)GPU.MakeVRAMFlat_BBGExtPalCoherent(bgExtDirty);
        bgExtChanged = bgExtDirty.CheckRange(0, 64);

        auto objExtDirty =
            GPU.VRAMDirty_BOBJExtPal.DeriveState(
                &GPU.VRAMMap_BOBJExtPal, GPU);
        (void)GPU.MakeVRAMFlat_BOBJExtPalCoherent(objExtDirty);
        objExtChanged = objExtDirty.CheckRange(0, 16);
    }

    const uint32_t bgPaletteMask =
        1u << (GPU2D.Num * 2u);
    const uint32_t objPaletteMask =
        2u << (GPU2D.Num * 2u);

    const bool uploadBG =
        !State->BGPaletteUploadInitialized ||
        bgExtChanged ||
        (GPU.PaletteDirty & bgPaletteMask) != 0;
    const bool uploadOBJ =
        !State->OBJPaletteUploadInitialized ||
        objExtChanged ||
        (GPU.PaletteDirty & objPaletteMask) != 0;

    if (uploadBG)
    {
        std::array<uint16_t, 256 * (1 + (4 * 16))>
            bgPalette = {};
        std::memcpy(
            bgPalette.data(),
            &GPU.Palette[GPU2D.Num ? 0x400 : 0],
            256 * sizeof(uint16_t));

        for (int slot = 0; slot < 4; slot++)
        {
            for (int pal = 0; pal < 16; pal++)
            {
                uint16_t* extPal =
                    GPU2D.GetBGExtPal(slot, pal);
                if (extPal)
                {
                    std::memcpy(
                        &bgPalette[
                            (1 + ((slot * 16) + pal)) * 256],
                        extPal,
                        256 * sizeof(uint16_t));
                }
            }
        }

        [State->PalTexBG replaceRegion:MTLRegionMake2D(
                                          0, 0,
                                          256, 1 + (4 * 16))
                              mipmapLevel:0
                                withBytes:bgPalette.data()
                              bytesPerRow:
                                  256 * sizeof(uint16_t)];
        State->BGPaletteUploadInitialized = true;
    }

    if (uploadOBJ)
    {
        std::array<uint16_t, 256 * (1 + 16)>
            objPalette = {};
        std::memcpy(
            objPalette.data(),
            &GPU.Palette[GPU2D.Num ? 0x600 : 0x200],
            256 * sizeof(uint16_t));

        uint16_t* objExtPal = GPU2D.GetOBJExtPal();
        if (objExtPal)
        {
            std::memcpy(
                &objPalette[256],
                objExtPal,
                256 * 16 * sizeof(uint16_t));
        }

        [State->PalTexOBJ replaceRegion:MTLRegionMake2D(
                                           0, 0, 256, 1 + 16)
                               mipmapLevel:0
                                 withBytes:objPalette.data()
                               bytesPerRow:
                                   256 * sizeof(uint16_t)];
        State->OBJPaletteUploadInitialized = true;
    }

    GPU.PaletteDirty = static_cast<uint8_t>(
        GPU.PaletteDirty &
        ~(bgPaletteMask | objPaletteMask));
    return true;
}

bool MetalRenderer2D::RefreshLayerConfig() noexcept
{
    if (!State || !State->LayerConfigBuffer)
        return false;

    Metal2DState& state = *State;
    state.LayerConfig = {};
    state.LayerConfig.vramMask = static_cast<uint32_t>(state.VRAMTexBG ? (state.VRAMTexBG.height - 1) : 0);
    for (auto& range : state.BGVRAMRange)
    {
        for (uint32_t& entry : range)
            entry = 0xFFFFFFFFu;
    }
    for (id<MTLTexture>& texture : state.BGLayerTex)
        texture = nil;

    int captureMask = GPU2D.Num ? 0x7 : 0x1F;
    int captureInfo[32] = {};
    GPU2D.GetCaptureInfo_BG(captureInfo);

    uint32_t tileBase = 0;
    uint32_t mapBase = 0;
    if (!GPU2D.Num)
    {
        tileBase = ((GPU2D.DispCnt >> 24) & 0x7) << 16;
        mapBase = ((GPU2D.DispCnt >> 27) & 0x7) << 16;
    }

    int layerType[4] = {1, 1, 0, 0};
    switch (GPU2D.DispCnt & 0x7)
    {
    case 0: layerType[2] = 1; layerType[3] = 1; break;
    case 1: layerType[2] = 1; layerType[3] = 2; break;
    case 2: layerType[2] = 2; layerType[3] = 2; break;
    case 3: layerType[2] = 1; layerType[3] = 3; break;
    case 4: layerType[2] = 2; layerType[3] = 3; break;
    case 5: layerType[2] = 3; layerType[3] = 3; break;
    case 6:
        layerType[0] = 0;
        layerType[1] = 0;
        layerType[2] = 4;
        layerType[3] = 0;
        break;
    case 7:
        layerType[2] = 0;
        layerType[3] = 0;
        break;
    }

    for (int layer = 0; layer < 4; layer++)
    {
        const int type = layerType[layer];
        if (!type)
            continue;

        const uint16_t bgCnt = GPU2D.BGCnt[layer];
        auto& cfg = state.LayerConfig.bgConfig[layer];

        cfg.tileOffset = tileBase + (((bgCnt >> 2) & 0xF) << 14);
        cfg.mapOffset = mapBase + (((bgCnt >> 8) & 0x1F) << 11);
        cfg.palOffset = 0;

        state.BGVRAMRange[layer][0] = cfg.tileOffset;
        state.BGVRAMRange[layer][2] = cfg.mapOffset;

        if ((layer == 0) && (GPU2D.DispCnt & (1 << 3)))
        {
            cfg.size[0] = 256;
            cfg.size[1] = 192;
            cfg.type = 6;
            cfg.clamp = 1;
            continue;
        }

        if (type == 1)
        {
            uint32_t tileSize = 0;
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x800; break;
            case 1: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x1000; break;
            case 2: cfg.size[0] = 256; cfg.size[1] = 512; mapSize = 0x1000; break;
            case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x2000; break;
            }

            if (bgCnt & (1 << 7))
            {
                cfg.type = 1;
                if (GPU2D.DispCnt & (1 << 30))
                {
                    int palOff = layer;
                    if ((layer < 2) && (bgCnt & (1 << 13)))
                        palOff += 2;
                    cfg.palOffset = 1 + (16 * palOff);
                }
                tileSize = 0x10000;
            }
            else
            {
                cfg.type = 0;
                tileSize = 0x8000;
            }

            cfg.clamp = 0;
            const int target = kBGBaseIndex[0][bgCnt >> 14] + layer;
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][1] = tileSize;
            state.BGVRAMRange[layer][3] = mapSize;
        }
        else if (type == 2)
        {
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x100; break;
            case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x400; break;
            case 2: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x1000; break;
            case 3: cfg.size[0] = 1024; cfg.size[1] = 1024; mapSize = 0x4000; break;
            }

            cfg.type = 2;
            cfg.clamp = !(bgCnt & (1 << 13));
            const int target = kBGBaseIndex[1][bgCnt >> 14] + layer - 2;
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][1] = 0x4000;
            state.BGVRAMRange[layer][3] = mapSize;
        }
        else if (type == 3)
        {
            if (bgCnt & (1 << 7))
            {
                uint32_t mapSize = 0;
                switch (bgCnt >> 14)
                {
                case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x4000; break;
                case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x10000; break;
                case 2: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x20000; break;
                case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x40000; break;
                }

                uint32_t tileOffset = 0;
                uint32_t mapOffset = ((bgCnt >> 8) & 0x1F) << 14;
                state.BGVRAMRange[layer][0] = 0xFFFFFFFFu;
                state.BGVRAMRange[layer][1] = 0xFFFFFFFFu;
                state.BGVRAMRange[layer][2] = mapOffset;
                state.BGVRAMRange[layer][3] = mapSize;

                if (bgCnt & (1 << 2))
                {
                    mapSize <<= 1;
                    int capBlock = -1;
                    if ((cfg.size[0] == 128) || (cfg.size[0] == 256))
                    {
                        uint32_t startAddr = mapOffset;
                        uint32_t endAddr = startAddr + mapSize;
                        startAddr >>= 14;
                        endAddr = (endAddr + 0x3FFF) >> 14;
                        for (uint32_t block = startAddr; block < endAddr; block++)
                        {
                            const int captured = captureInfo[block & captureMask];
                            if (captured != -1)
                                capBlock = captured;
                        }
                    }

                    if (capBlock != -1)
                    {
                        if (cfg.size[0] == 128)
                        {
                            cfg.type = 7;
                            tileOffset = capBlock;
                            mapOffset = (mapOffset >> 8) & 0x7F;
                        }
                        else
                        {
                            cfg.type = 8;
                            tileOffset = capBlock >> 2;
                            mapOffset = (mapOffset >> 9) & 0xFF;
                        }
                    }
                    else
                        cfg.type = 5;
                }
                else
                    cfg.type = 4;

                cfg.tileOffset = tileOffset;
                cfg.mapOffset = mapOffset;
                const int target = kBGBaseIndex[2][bgCnt >> 14] + layer - 2;
                state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            }
            else
            {
                uint32_t mapSize = 0;
                switch (bgCnt >> 14)
                {
                case 0: cfg.size[0] = 128; cfg.size[1] = 128; mapSize = 0x200; break;
                case 1: cfg.size[0] = 256; cfg.size[1] = 256; mapSize = 0x800; break;
                case 2: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x2000; break;
                case 3: cfg.size[0] = 1024; cfg.size[1] = 1024; mapSize = 0x8000; break;
                }

                cfg.type = 3;
                if (GPU2D.DispCnt & (1 << 30))
                {
                    int palOff = layer;
                    if ((layer < 2) && (bgCnt & (1 << 13)))
                        palOff += 2;
                    cfg.palOffset = 1 + (16 * palOff);
                }

                const int target = kBGBaseIndex[1][bgCnt >> 14] + layer - 2;
                state.BGLayerTex[layer] = state.AllBGLayerTex[target];
                state.BGVRAMRange[layer][1] = 0x10000;
                state.BGVRAMRange[layer][3] = mapSize;
            }

            cfg.clamp = !(bgCnt & (1 << 13));
        }
        else
        {
            uint32_t mapSize = 0;
            switch (bgCnt >> 14)
            {
            case 0: cfg.size[0] = 512; cfg.size[1] = 1024; mapSize = 0x80000; break;
            case 1: cfg.size[0] = 1024; cfg.size[1] = 512; mapSize = 0x80000; break;
            case 2: cfg.size[0] = 512; cfg.size[1] = 256; mapSize = 0x20000; break;
            case 3: cfg.size[0] = 512; cfg.size[1] = 512; mapSize = 0x40000; break;
            }

            cfg.type = 4;
            cfg.tileOffset = 0;
            cfg.mapOffset = 0;
            cfg.clamp = !(bgCnt & (1 << 13));
            const int target = kBGBaseIndex[3][bgCnt >> 14];
            state.BGLayerTex[layer] = state.AllBGLayerTex[target];
            state.BGVRAMRange[layer][0] = 0xFFFFFFFFu;
            state.BGVRAMRange[layer][1] = 0xFFFFFFFFu;
            state.BGVRAMRange[layer][3] = mapSize;
        }
    }

    // Store the selected pre-render target index in the otherwise-unused
    // BGConfig padding word. A segment can restore the exact texture binding
    // without reconstructing the original BGCnt register.
    for (int layer = 0; layer < 4; layer++)
    {
        state.LayerConfig.bgConfig[layer].pad0[0] = 0;
        if (!state.BGLayerTex[layer])
            continue;

        for (uint32_t index = 0;
             index < static_cast<uint32_t>(kBGLayerCount);
             index++)
        {
            if (state.BGLayerTex[layer] ==
                state.AllBGLayerTex[index])
            {
                state.LayerConfig.bgConfig[layer].pad0[0] =
                    index + 1u;
                break;
            }
        }
    }

    std::memcpy(
        [state.LayerConfigBuffer contents],
        &state.LayerConfig,
        sizeof(state.LayerConfig));
    return true;
}

bool MetalRenderer2D::RefreshSpriteConfig(int ystart, int yend) noexcept
{
    if (!State || !State->SpriteConfigBuffer)
        return false;

    Metal2DState& state = *State;
    const uint16_t* liveOAM =
        reinterpret_cast<const uint16_t*>(
            &GPU.OAM[GPU2D.Num ? 0x400 : 0]);
    std::array<int, 16> captureInfo {};
    GPU2D.GetCaptureInfo_OBJ(captureInfo.data());

    const uint32_t vramMask =
        static_cast<uint32_t>(
            state.VRAMTexOBJ
                ? (state.VRAMTexOBJ.height - 1)
                : 0);
    const uint32_t dispCnt = GPU2D.DispCnt;
    const int32_t mosaicWidth =
        static_cast<int32_t>(GPU2D.OBJMosaicSize[0]);
    const size_t oamBytes =
        state.SpriteSourceOAM.size() * sizeof(uint16_t);

    const bool sourceChanged =
        !state.SpriteSourceCacheValid ||
        state.SpriteSourceDispCnt != dispCnt ||
        state.SpriteSourceMosaicWidth != mosaicWidth ||
        state.SpriteSourceVramMask != vramMask ||
        state.SpriteSourceCaptureInfo != captureInfo ||
        std::memcmp(
            state.SpriteSourceOAM.data(),
            liveOAM,
            oamBytes) != 0;

    if (sourceChanged)
    {
        std::memcpy(
            state.SpriteSourceOAM.data(),
            liveOAM,
            oamBytes);
        state.SpriteSourceCaptureInfo = captureInfo;
        state.SpriteSourceDispCnt = dispCnt;
        state.SpriteSourceMosaicWidth = mosaicWidth;
        state.SpriteSourceVramMask = vramMask;
        state.SpriteStaticConfig = {};
        state.SpriteStaticConfig.vramMask = vramMask;
        state.SpriteNormalizedOAM = {};
        state.SpriteActiveMask = {};

        const uint16_t* oam =
            state.SpriteSourceOAM.data();
        for (int index = 0; index < 32; index++)
        {
            const int16_t* rotscale =
                reinterpret_cast<const int16_t*>(
                    &oam[(index * 16) + 3]);
            auto& destination =
                state.SpriteStaticConfig.rotscale[index];
            destination[0] = rotscale[0];
            destination[1] = rotscale[4];
            destination[2] = rotscale[8];
            destination[3] = rotscale[12];
        }

        static constexpr uint8_t kSpriteWidth[16] = {
            8, 16, 8, 8, 16, 32, 8, 8,
            32, 32, 16, 8, 64, 64, 32, 8,
        };
        static constexpr uint8_t kSpriteHeight[16] = {
            8, 8, 16, 8, 16, 8, 32, 8,
            32, 16, 32, 8, 64, 32, 64, 8,
        };

        auto activateRange = [&](int spriteIndex,
                                 int32_t start,
                                 int32_t end) {
            start = std::max<int32_t>(start, 0);
            end = std::min<int32_t>(end, kScreenH);
            if (start >= end)
                return;

            const uint32_t word =
                static_cast<uint32_t>(spriteIndex) >> 6u;
            const uint64_t bit =
                1ull <<
                (static_cast<uint32_t>(spriteIndex) & 63u);
            for (int32_t line = start; line < end; line++)
                state.SpriteActiveMask[line][word] |= bit;
        };

        const int captureMask = GPU2D.Num ? 0x7 : 0xF;
        for (int spriteIndex = 0;
             spriteIndex < 128;
             spriteIndex++)
        {
            const uint16_t* attrib =
                &oam[spriteIndex * 4];
            const uint32_t spriteType =
                (attrib[0] >> 8) & 0x3u;
            if (spriteType == 2u)
                continue;

            const int32_t x =
                static_cast<int32_t>(attrib[1] << 23) >> 23;
            const int32_t y =
                static_cast<int32_t>(attrib[0] << 24) >> 24;
            const uint32_t sizeParam =
                (attrib[0] >> 14) |
                ((attrib[1] & 0xC000u) >> 12);
            const int32_t width = kSpriteWidth[sizeParam];
            const int32_t height = kSpriteHeight[sizeParam];
            int32_t boundWidth = width;
            int32_t boundHeight = height;
            if (spriteType == 3u)
            {
                boundWidth <<= 1;
                boundHeight <<= 1;
            }
            if (x <= -boundWidth)
                continue;

            const uint32_t spriteMode =
                (attrib[0] >> 10) & 0x3u;
            if (spriteMode == 3u)
            {
                if ((dispCnt & 0x60u) == 0x60u)
                    continue;
                if ((attrib[2] >> 12) == 0)
                    continue;
            }

            SpriteConfigCpu::OAMConfig normalized {};
            normalized.position[0] = x;
            normalized.position[1] = y;
            normalized.size[0] = width;
            normalized.size[1] = height;
            normalized.boundSize[0] = boundWidth;
            normalized.boundSize[1] = boundHeight;

            if ((spriteType & 1u) != 0u)
            {
                normalized.flip[0] = 0;
                normalized.flip[1] = 0;
                normalized.rotscale =
                    (attrib[1] >> 9) & 0x1Fu;
            }
            else
            {
                normalized.flip[0] =
                    (attrib[1] & (1u << 12)) != 0u;
                normalized.flip[1] =
                    (attrib[1] & (1u << 13)) != 0u;
                normalized.rotscale = 0xFFFFFFFFu;
            }

            normalized.objMode = spriteMode;
            normalized.mosaic =
                (attrib[0] & (1u << 12)) != 0u &&
                spriteMode != 2u;
            normalized.bgPrio =
                (attrib[2] >> 10) & 0x3u;

            const uint32_t tileNumber =
                attrib[2] & 0x3FFu;
            if (spriteMode == 3u)
            {
                normalized.type = 2u;
                if ((dispCnt & (1u << 6)) != 0u)
                {
                    normalized.tileOffset =
                        tileNumber <<
                        (7u + ((dispCnt >> 22) & 0x1u));
                    normalized.tileStride =
                        static_cast<uint32_t>(width * 2);
                }
                else
                {
                    const bool is256 =
                        (dispCnt & (1u << 5)) != 0u;
                    uint32_t tileOffset = 0;
                    uint32_t tileStride = 0;
                    if (is256)
                    {
                        tileOffset =
                            ((tileNumber & 0x01Fu) << 4) +
                            ((tileNumber & 0x3E0u) << 7);
                        tileStride = 256u * 2u;
                    }
                    else
                    {
                        tileOffset =
                            ((tileNumber & 0x00Fu) << 4) +
                            ((tileNumber & 0x3F0u) << 7);
                        tileStride = 128u * 2u;
                    }

                    int capturedBlock = -1;
                    uint32_t startAddress = tileOffset;
                    uint32_t endAddress =
                        startAddress +
                        static_cast<uint32_t>(height) * tileStride;
                    startAddress >>= 14;
                    endAddress =
                        (endAddress + 0x3FFFu) >> 14;
                    for (uint32_t block = startAddress;
                         block < endAddress;
                         block++)
                    {
                        const int captured =
                            captureInfo[block & captureMask];
                        if (captured != -1)
                            capturedBlock = captured;
                    }

                    if (capturedBlock != -1)
                    {
                        if (!is256)
                        {
                            normalized.type = 3u;
                            tileStride =
                                static_cast<uint32_t>(capturedBlock);
                            tileOffset &= 0x7FFFu;
                        }
                        else
                        {
                            normalized.type = 4u;
                            tileStride =
                                static_cast<uint32_t>(capturedBlock >> 2);
                            tileOffset &= 0x1FFFFu;
                        }
                    }

                    normalized.tileOffset = tileOffset;
                    normalized.tileStride = tileStride;
                }

                normalized.palOffset =
                    1u + (attrib[2] >> 12);
            }
            else
            {
                if ((dispCnt & (1u << 4)) != 0u)
                {
                    normalized.tileOffset =
                        tileNumber <<
                        (5u + ((dispCnt >> 20) & 0x3u));
                    normalized.tileStride =
                        static_cast<uint32_t>((width >> 3) * 32);
                    if ((attrib[0] & (1u << 13)) != 0u)
                        normalized.tileStride <<= 1;
                }
                else
                {
                    normalized.tileOffset = tileNumber << 5;
                    normalized.tileStride = 32u * 32u;
                }

                if ((attrib[0] & (1u << 13)) != 0u)
                {
                    normalized.type = 1u;
                    normalized.palOffset =
                        (dispCnt & (1u << 31)) != 0u
                            ? (1u + (attrib[2] >> 12))
                            : 0u;
                }
                else
                {
                    normalized.type = 0u;
                    normalized.palOffset =
                        (attrib[2] >> 12) << 4;
                }
            }

            state.SpriteNormalizedOAM[spriteIndex] = normalized;
            activateRange(
                spriteIndex,
                y,
                y + boundHeight);
            const int32_t wrappedY = y & 0xFF;
            activateRange(
                spriteIndex,
                wrappedY,
                wrappedY + boundHeight);
        }

        state.SpriteSourceCacheValid = true;
    }

    state.SpriteConfig = state.SpriteStaticConfig;
    state.NumSprites = 0;
    state.SpriteUseMosaic = false;

    const int rangeStart =
        std::clamp(ystart, 0, kScreenH);
    const int rangeEnd =
        std::clamp(yend, 0, kScreenH);
    std::array<uint64_t, 2> active {};
    for (int line = rangeStart;
         line < rangeEnd;
         line++)
    {
        active[0] |= state.SpriteActiveMask[line][0];
        active[1] |= state.SpriteActiveMask[line][1];
    }

    for (uint32_t word = 0; word < 2u; word++)
    {
        uint64_t bits = active[word];
        while (bits != 0u && state.NumSprites < 128)
        {
            const uint32_t bit =
                static_cast<uint32_t>(
                    __builtin_ctzll(bits));
            bits &= bits - 1u;
            const uint32_t spriteIndex =
                (word << 6u) + bit;
            const auto& normalized =
                state.SpriteNormalizedOAM[spriteIndex];
            state.SpriteConfig.oam[state.NumSprites++] =
                normalized;
            if (normalized.mosaic != 0u && mosaicWidth > 0)
                state.SpriteUseMosaic = true;
        }
    }

    std::memcpy(
        [state.SpriteConfigBuffer contents],
        &state.SpriteConfig,
        sizeof(state.SpriteConfig));
    return true;
}

bool MetalRenderer2D::RefreshScanlineConfig(int line) noexcept
{
    if (!State || !State->ScanlineConfigBuffer || line < 0 || line >= 192)
        return false;

    Metal2DState& state = *State;
    ScanlineConfigCpu::Scanline& scanline = reinterpret_cast<ScanlineConfigCpu*>([state.ScanlineConfigBuffer contents])->scanline[line];
    scanline = {};

    const uint32_t bgMode = GPU2D.DispCnt & 0x7;
    const bool xMosaic = GPU2D.BGMosaicSize[0] > 0;

    if (GPU2D.DispCnt & (1 << 3))
    {
        const int xPos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
        scanline.bgOffset[0][0] = xPos - ((xPos & 0x100) << 1);
        scanline.bgOffset[0][1] = line;
        scanline.bgMosaicEnable[0] = false;
    }
    else
    {
        scanline.bgOffset[0][0] = GPU2D.BGXPos[0];
        if (GPU2D.BGCnt[0] & (1 << 6))
        {
            scanline.bgOffset[0][1] = GPU2D.BGYPos[0] + GPU2D.BGMosaicLine;
            scanline.bgMosaicEnable[0] = xMosaic;
        }
        else
        {
            scanline.bgOffset[0][1] = GPU2D.BGYPos[0] + line;
            scanline.bgMosaicEnable[0] = false;
        }
    }

    scanline.bgOffset[1][0] = GPU2D.BGXPos[1];
    if (GPU2D.BGCnt[1] & (1 << 6))
    {
        scanline.bgOffset[1][1] = GPU2D.BGYPos[1] + GPU2D.BGMosaicLine;
        scanline.bgMosaicEnable[1] = xMosaic;
    }
    else
    {
        scanline.bgOffset[1][1] = GPU2D.BGYPos[1] + line;
        scanline.bgMosaicEnable[1] = false;
    }

    if ((bgMode == 2) || (bgMode >= 4 && bgMode <= 6))
    {
        scanline.bgOffset[2][0] = GPU2D.BGXRefInternal[0];
        scanline.bgOffset[2][1] = GPU2D.BGYRefInternal[0];
        scanline.bgRotscale[0][0] = GPU2D.BGRotA[0];
        scanline.bgRotscale[0][1] = GPU2D.BGRotB[0];
        scanline.bgRotscale[0][2] = GPU2D.BGRotC[0];
        scanline.bgRotscale[0][3] = GPU2D.BGRotD[0];
    }
    else
    {
        scanline.bgOffset[2][0] = GPU2D.BGXPos[2];
        scanline.bgOffset[2][1] =
            (GPU2D.BGCnt[2] & (1 << 6)) ? GPU2D.BGYPos[2] + GPU2D.BGMosaicLine : GPU2D.BGYPos[2] + line;
    }
    scanline.bgMosaicEnable[2] = (GPU2D.BGCnt[2] & (1 << 6)) ? xMosaic : false;

    if (bgMode >= 1 && bgMode <= 5)
    {
        scanline.bgOffset[3][0] = GPU2D.BGXRefInternal[1];
        scanline.bgOffset[3][1] = GPU2D.BGYRefInternal[1];
        scanline.bgRotscale[1][0] = GPU2D.BGRotA[1];
        scanline.bgRotscale[1][1] = GPU2D.BGRotB[1];
        scanline.bgRotscale[1][2] = GPU2D.BGRotC[1];
        scanline.bgRotscale[1][3] = GPU2D.BGRotD[1];
    }
    else
    {
        scanline.bgOffset[3][0] = GPU2D.BGXPos[3];
        scanline.bgOffset[3][1] =
            (GPU2D.BGCnt[3] & (1 << 6)) ? GPU2D.BGYPos[3] + GPU2D.BGMosaicLine : GPU2D.BGYPos[3] + line;
    }
    scanline.bgMosaicEnable[3] = (GPU2D.BGCnt[3] & (1 << 6)) ? xMosaic : false;

    const uint16_t* palette = reinterpret_cast<const uint16_t*>(&GPU.Palette[GPU2D.Num ? 0x400 : 0]);
    scanline.backColor = palette[0];

    scanline.mosaicSize[0] = GPU2D.BGMosaicSize[0];
    scanline.mosaicSize[1] = GPU2D.BGMosaicSize[1];
    scanline.mosaicSize[2] = GPU2D.OBJMosaicSize[0];
    scanline.mosaicSize[3] = GPU2D.OBJMosaicSize[1];

    if (GPU2D.DispCnt & 0xE000)
        scanline.winRegs = GPU2D.WinCnt[2];
    else
        scanline.winRegs = 0xFF;

    scanline.winRegs |= (GPU2D.DispCnt & (1 << 15)) ? (GPU2D.WinCnt[3] << 8) : 0xFF00;
    scanline.winRegs |= (GPU2D.DispCnt & (1 << 14)) ? (GPU2D.WinCnt[1] << 16) : 0xFF0000;
    scanline.winRegs |= (GPU2D.DispCnt & (1 << 13)) ? (GPU2D.WinCnt[0] << 24) : 0xFF000000;
    scanline.winMask = 0;

    if ((GPU2D.DispCnt & (1 << 13)) && (GPU2D.Win0Active & 0x1))
    {
        const int x0 = GPU2D.Win0Coords[0];
        const int x1 = GPU2D.Win0Coords[1];
        if (x0 <= x1)
        {
            scanline.winPos[0] = x0;
            scanline.winPos[1] = x1;
            if (GPU2D.Win0Active == 0x3)
                scanline.winMask |= (1 << 0);
            scanline.winMask |= (1 << 1);
            GPU2D.Win0Active &= ~0x2;
        }
        else
        {
            scanline.winPos[0] = x1;
            scanline.winPos[1] = x0;
            if (GPU2D.Win0Active == 0x3)
                scanline.winMask |= (1 << 0);
            scanline.winMask |= (1 << 2);
            GPU2D.Win0Active |= 0x2;
        }
    }
    else
    {
        scanline.winPos[0] = 256;
        scanline.winPos[1] = 256;
    }

    if ((GPU2D.DispCnt & (1 << 14)) && (GPU2D.Win1Active & 0x1))
    {
        const int x0 = GPU2D.Win1Coords[0];
        const int x1 = GPU2D.Win1Coords[1];
        if (x0 <= x1)
        {
            scanline.winPos[2] = x0;
            scanline.winPos[3] = x1;
            if (GPU2D.Win1Active == 0x3)
                scanline.winMask |= (1 << 3);
            scanline.winMask |= (1 << 4);
            GPU2D.Win1Active &= ~0x2;
        }
        else
        {
            scanline.winPos[2] = x1;
            scanline.winPos[3] = x0;
            if (GPU2D.Win1Active == 0x3)
                scanline.winMask |= (1 << 3);
            scanline.winMask |= (1 << 5);
            GPU2D.Win1Active |= 0x2;
        }
    }
    else
    {
        scanline.winPos[2] = 256;
        scanline.winPos[3] = 256;
    }

    for (uint32_t& priority : scanline.bgPrio)
        priority = 0xFFFFFFFFu;
    for (int layer = 0; layer < 4; layer++)
    {
        if (GPU2D.LayerEnable & (1u << layer))
        {
            scanline.bgPrio[layer] =
                GPU2D.BGCnt[layer] & 0x3u;
        }
    }

    scanline.enableOBJ =
        (GPU2D.LayerEnable & (1u << 4)) ? 1u : 0u;
    scanline.enable3D =
        (GPU2D.DispCnt & (1u << 3)) ? 1u : 0u;
    scanline.blendCnt = GPU2D.BlendCnt;
    scanline.blendEffect =
        (GPU2D.BlendCnt >> 6) & 0x3u;
    scanline.blendCoef[0] = GPU2D.EVA;
    scanline.blendCoef[1] = GPU2D.EVB;
    scanline.blendCoef[2] = GPU2D.EVY;

    return true;
}

bool MetalRenderer2D::RefreshCompositorConfig() noexcept
{
    if (!State || !State->CompositorConfigBuffer)
        return false;

    Metal2DState& state = *State;
    state.CompositorConfig = {};
    for (uint32_t& prio : state.CompositorConfig.bgPrio)
        prio = 0xFFFFFFFFu;

    for (int layer = 0; layer < 4; layer++)
    {
        if (!(GPU2D.LayerEnable & (1 << layer)))
            continue;
        state.CompositorConfig.bgPrio[layer] = GPU2D.BGCnt[layer] & 0x3;
    }

    state.CompositorConfig.enableOBJ = !!(GPU2D.LayerEnable & (1 << 4));
    state.CompositorConfig.enable3D = !!(GPU2D.DispCnt & (1 << 3));
    state.CompositorConfig.blendCnt = GPU2D.BlendCnt;
    state.CompositorConfig.blendEffect = (GPU2D.BlendCnt >> 6) & 0x3;
    state.CompositorConfig.blendCoef[0] = GPU2D.EVA;
    state.CompositorConfig.blendCoef[1] = GPU2D.EVB;
    state.CompositorConfig.blendCoef[2] = GPU2D.EVY;

    std::memcpy([state.CompositorConfigBuffer contents],
                &state.CompositorConfig,
                sizeof(state.CompositorConfig));
    return true;
}

void MetalRenderer2D::Reset() noexcept
{
    if (!State)
        return;

    State->Queue = nil;
    State->LayerLibrary = nil;
    State->LayerPipeline = nil;
    State->LayerVertexBuffer = nil;
    State->FullGpuLibrary = nil;
    State->SpritePipeline = nil;
    State->SpriteMosaicFlagsPipeline = nil;
    State->SpriteWindowPipeline = nil;
    State->CompositorPipeline = nil;
    State->SpriteDepthState = nil;
    State->RepeatSampler = nil;
    State->DummyColorTexture = nil;
    State->FullGpuReady = false;
    State->BGUploadInitialized = false;
    State->OBJUploadInitialized = false;
    State->BGPaletteUploadInitialized = false;
    State->OBJPaletteUploadInitialized = false;
    State->OutputTex = nil;
    State->OBJLayerTex = nil;
    State->OBJDepthTex = nil;
    for (id<MTLTexture>& texture : State->AllBGLayerTex)
        texture = nil;
    for (id<MTLTexture>& texture : State->BGLayerTex)
        texture = nil;
    State->VRAMTexBG = nil;
    State->VRAMTexOBJ = nil;
    State->PalTexBG = nil;
    State->PalTexOBJ = nil;
    State->MosaicTex = nil;
    State->SpriteTex = nil;
    State->LayerConfigBuffer = nil;
    State->SpriteConfigBuffer = nil;
    State->ScanlineConfigBuffer = nil;
    State->SpriteScanlineConfigBuffer = nil;
    State->LayerScanlineSnapshotBuffer = nil;
    State->SpriteScanlineMetaBuffer = nil;
    State->CompositorConfigBuffer = nil;
    MetalReleaseSegmentedUploadSlot(
        State->SegmentedCaptureRing,
        State->SegmentedCaptureSlot);
    State->SegmentedCaptureRing.reset();
    State->SegmentedCaptureSlot = -1;
    State->SegmentedCaptureAttempted = false;
    State->SegmentedUploadRing.reset();
    State->LayerSnapshotValid.fill(0);
    State->SpriteSnapshotValid.fill(0);
    State->LayerSnapshotLastLine = -1;
    State->SpriteSnapshotLastLine = -1;
    State->SnapshotBuffersReady = false;
    State->SegmentedRenderReady = false;
    State->SpriteSnapshotStride = 0;
    State->SpriteSourceCacheValid = false;
    State->Scale = 0;
}

void* MetalRenderer2D::GetOutputTexture() const noexcept
{
    if (!State || !State->OutputTex)
        return nullptr;
    return (__bridge void*)State->OutputTex;
}

void* MetalRenderer2D::GetOBJLayerTexture() const noexcept
{
    if (!State || !State->OBJLayerTex)
        return nullptr;
    return (__bridge void*)State->OBJLayerTex;
}

void* MetalRenderer2D::GetOBJDepthTexture() const noexcept
{
    if (!State || !State->OBJDepthTex)
        return nullptr;
    return (__bridge void*)State->OBJDepthTex;
}

void* MetalRenderer2D::GetBGLayerTexture(int index) const noexcept
{
    if (!State || index < 0 || index >= kBGLayerCount || !State->AllBGLayerTex[index])
        return nullptr;
    return (__bridge void*)State->AllBGLayerTex[index];
}

void* MetalRenderer2D::GetBGVRAMTexture() const noexcept
{
    if (!State || !State->VRAMTexBG)
        return nullptr;
    return (__bridge void*)State->VRAMTexBG;
}

void* MetalRenderer2D::GetOBJVRAMTexture() const noexcept
{
    if (!State || !State->VRAMTexOBJ)
        return nullptr;
    return (__bridge void*)State->VRAMTexOBJ;
}

void* MetalRenderer2D::GetBGPaletteTexture() const noexcept
{
    if (!State || !State->PalTexBG)
        return nullptr;
    return (__bridge void*)State->PalTexBG;
}

void* MetalRenderer2D::GetOBJPaletteTexture() const noexcept
{
    if (!State || !State->PalTexOBJ)
        return nullptr;
    return (__bridge void*)State->PalTexOBJ;
}

void* MetalRenderer2D::GetMosaicTexture() const noexcept
{
    if (!State || !State->MosaicTex)
        return nullptr;
    return (__bridge void*)State->MosaicTex;
}

void* MetalRenderer2D::GetSpriteTexture() const noexcept
{
    if (!State || !State->SpriteTex)
        return nullptr;
    return (__bridge void*)State->SpriteTex;
}

int MetalRenderer2D::GetScaleFactor() const noexcept
{
    return ScaleFactor;
}

int MetalRenderer2D::GetTargetWidth() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.width) : 0;
}

int MetalRenderer2D::GetTargetHeight() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.height) : 0;
}

#include "GPU2D_MetalFullGpuMethods.inc"

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
