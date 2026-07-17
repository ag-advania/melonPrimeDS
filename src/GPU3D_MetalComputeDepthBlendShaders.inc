// MELONPRIME_METAL_COMPUTE_COMPLETE_DEPTH_BLEND_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1
// MELONPRIME_METAL_COMPUTE_LEGACY_SUMMARY_RETIREMENT_V1
// Standalone MSL library for DS two-layer depth/blend, shadow and clear-bitmap processing.
static constexpr const char* kMetalComputeCompleteDepthBlendSource = R"MSL(
#include <metal_stdlib>
using namespace metal;


struct MPDRenderPolygon
{
    uint FirstXSpan;
    int YTop, YBot;
    int XMin, XMax;
    int XMinY, XMaxY;
    uint Variant;
    uint Attr;
    float TextureLayer;
};

struct MPDConfig
{
    uint screenWidth;
    uint screenHeight;
    uint tileSize;
    uint tilesPerLine;

    uint binStride;
    uint polygonGroups;
    uint maxWorkTiles;
    uint tileWorkCapacity;

    uint numPolygons;
    uint clearColor;
    uint clearDepth;
    uint clearAttr;

    uint dispCnt;
    uint clearBitmapX;
    uint clearBitmapY;
    uint wBuffer;
};

static inline uint mpd_read8(
    device const uchar* data,
    uint mask,
    uint address)
{
    return uint(data[address & mask]);
}

static inline uint mpd_read16(
    device const uchar* data,
    uint mask,
    uint address)
{
    return mpd_read8(data, mask, address) |
           (mpd_read8(data, mask, address + 1u) << 8u);
}

static inline uint mpd_rgb555_to_rgb6a5(uint value)
{
    uint r = (value << 1u) & 0x3Eu;
    uint g = (value >> 4u) & 0x3Eu;
    uint b = (value >> 9u) & 0x3Eu;
    if (r != 0u) r++;
    if (g != 0u) g++;
    if (b != 0u) b++;
    const uint a = (value & 0x8000u) != 0u ? 31u : 0u;
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

static inline bool mpd_depth_pass(
    uint destinationDepth,
    uint sourceDepth,
    bool equalDepth,
    bool wBuffer)
{
    if (!equalDepth)
        return sourceDepth < destinationDepth;

    const uint difference = destinationDepth > sourceDepth
        ? destinationDepth - sourceDepth
        : sourceDepth - destinationDepth;
    return difference <= (wBuffer ? 0xFFu : 0x200u);
}

static inline bool mpd_plot_translucent(
    thread uint& color,
    thread uint& depth,
    thread uint& attr,
    bool isShadow,
    uint tileColor,
    uint sourceAlpha,
    uint tileDepth,
    uint sourceAttr,
    bool writeDepth)
{
    uint blendAttr =
        (sourceAttr & 0xE0F0u) |
        ((sourceAttr >> 8u) & 0xFF0000u) |
        (1u << 22u) |
        (attr & 0xFF001F0Fu);

    const bool differentId =
        (!isShadow || (attr & (1u << 22u)) != 0u)
            ? (attr & 0x007F0000u) != (blendAttr & 0x007F0000u)
            : (attr & 0x3F000000u) != (sourceAttr & 0x3F000000u);
    if (!differentId)
        return false;

    if (writeDepth)
        depth = tileDepth;

    if ((attr & (1u << 15u)) == 0u)
        blendAttr &= ~(1u << 15u);
    attr = blendAttr;

    uint sourceRB = tileColor & 0x003F003Fu;
    uint sourceG = tileColor & 0x00003F00u;
    const uint destinationRB = color & 0x003F003Fu;
    const uint destinationG = color & 0x00003F00u;
    const uint destinationA = color & 0x1F000000u;

    const uint alpha = (sourceAlpha >> 24u) + 1u;
    if (destinationA != 0u)
    {
        sourceRB =
            ((sourceRB * alpha) +
             (destinationRB * (32u - alpha))) >> 5u;
        sourceG =
            ((sourceG * alpha) +
             (destinationG * (32u - alpha))) >> 5u;
    }

    color =
        (sourceRB & 0x003F003Fu) |
        (sourceG & 0x00003F00u) |
        max(destinationA, sourceAlpha);
    return true;
}

kernel void mp_compute_depth_blend_complete(
    device const uint* fineMask [[buffer(0)]],
    device const uint* workOffsets [[buffer(1)]],
    device const MPDRenderPolygon* polygons [[buffer(2)]],
    device const uint* colorTiles [[buffer(3)]],
    device const uint* depthTiles [[buffer(4)]],
    device const uint* attrTiles [[buffer(5)]],
    device uint* resultColor [[buffer(6)]],
    device uint* resultDepth [[buffer(7)]],
    device uint* resultAttr [[buffer(8)]],
    constant MPDConfig& config [[buffer(10)]],
    device const uchar* textureMemory [[buffer(11)]],
    uint gid [[thread_position_in_grid]])
{
    const uint pixelCount =
        config.screenWidth * config.screenHeight;
    if (gid >= pixelCount)
        return;

    const uint x = gid % config.screenWidth;
    const uint y = gid / config.screenWidth;
    const uint tileX = x / config.tileSize;
    const uint tileY = y / config.tileSize;
    const uint linearTile =
        tileX + tileY * config.tilesPerLine;
    const uint tileInner =
        (y % config.tileSize) * config.tileSize +
        (x % config.tileSize);
    const uint tileArea =
        config.tileSize * config.tileSize;

    uint color0;
    uint color1 = 0u;
    uint depth0;
    uint depth1 = 0u;
    uint attr0 = config.clearAttr;
    uint attr1 = 0u;

    if ((config.dispCnt & (1u << 14u)) != 0u)
    {
        const uint scale =
            max(config.screenWidth / 256u, 1u);
        const uint clearX =
            ((x / scale) + config.clearBitmapX) & 0xFFu;
        const uint clearY =
            ((y / scale) + config.clearBitmapY) & 0xFFu;
        const uint clearPixel =
            clearX + clearY * 256u;

        const uint rawColor = mpd_read16(
            textureMemory,
            0x7FFFFu,
            0x40000u + clearPixel * 2u);
        const uint rawDepth = mpd_read16(
            textureMemory,
            0x7FFFFu,
            0x60000u + clearPixel * 2u);

        color0 = mpd_rgb555_to_rgb6a5(rawColor);
        depth0 =
            ((rawDepth & 0x7FFFu) * 0x200u) +
            0x1FFu;
        attr0 =
            (attr0 & ~0x8000u) |
            (rawDepth & 0x8000u);
    }
    else
    {
        color0 = config.clearColor;
        depth0 = config.clearDepth;
    }

    uint stencil = 0u;
    bool previousWasShadowMask = false;


    for (uint group = 0u;
         group < config.polygonGroups;
         group++)
    {
        const uint maskIndex =
            linearTile * config.binStride + group;
        uint mask = fineMask[maskIndex];
        uint ordinal = 0u;

        while (mask != 0u)
        {
            const uint bit = ctz(mask);
            mask &= ~(1u << bit);

            const uint workIndex =
                workOffsets[maskIndex] + ordinal++;
            if (workIndex >=
                min(config.maxWorkTiles,
                    config.tileWorkCapacity))
            {
                continue;
            }

            const uint polygonIndex =
                group * 32u + bit;
            if (polygonIndex >= config.numPolygons)
                continue;

            const uint tilePixel =
                workIndex * tileArea + tileInner;
            const uint tileColor =
                colorTiles[tilePixel];
            if (tileColor == 0u)
                continue;


            const MPDRenderPolygon polygon =
                polygons[polygonIndex];
            const uint polygonAttr = polygon.Attr;
            const bool isShadowMask =
                (polygonAttr & 0x3F000030u) ==
                0x00000030u;
            const bool previousShadowMask =
                previousWasShadowMask;
            previousWasShadowMask = isShadowMask;
            const bool equalDepth =
                (polygonAttr & (1u << 14u)) != 0u;
            const uint tileDepth =
                depthTiles[tilePixel];
            const uint tileAttr =
                attrTiles[tilePixel];

            uint destinationAttr = attr0;

            if (!isShadowMask)
            {
                const bool isShadow =
                    (polygonAttr & 0x30u) == 0x30u;
                bool writeSecondLayer = false;

                if (isShadow)
                {
                    if (stencil == 0u)
                        continue;
                    if ((stencil & 1u) == 0u)
                        writeSecondLayer = true;
                    if ((stencil & 2u) == 0u)
                        destinationAttr &= ~0x3u;
                }

                uint destinationDepth =
                    writeSecondLayer ? depth1 : depth0;
                if (!mpd_depth_pass(
                        destinationDepth,
                        tileDepth,
                        equalDepth,
                        config.wBuffer != 0u))
                {
                    if ((destinationAttr & 0x3u) == 0u ||
                        writeSecondLayer)
                    {
                        continue;
                    }

                    writeSecondLayer = true;
                    destinationAttr = attr1;
                    if (!mpd_depth_pass(
                            depth1,
                            tileDepth,
                            equalDepth,
                            config.wBuffer != 0u))
                    {
                        continue;
                    }
                }

                const uint sourceAttr =
                    polygonAttr & 0x3F008000u;
                const uint sourceAlpha =
                    tileColor & 0x1F000000u;

                if (sourceAlpha == 0x1F000000u)
                {
                    const uint opaqueAttr =
                        sourceAttr | tileAttr;
                    if (!writeSecondLayer)
                    {
                        if ((opaqueAttr & 0x3u) != 0u)
                        {
                            color1 = color0;
                            depth1 = depth0;
                            attr1 = attr0;
                        }

                        color0 = tileColor;
                        depth0 = tileDepth;
                        attr0 = opaqueAttr;
                    }
                    else
                    {
                        color1 = tileColor;
                        depth1 = tileDepth;
                        attr1 = opaqueAttr;
                    }

                }
                else
                {
                    // The bool results of mpd_plot_translucent only fed the
                    // retired summary counters (secondLayerWrites /
                    // translucentLayers, see the Phase 8V/8W cleanup); the
                    // plots themselves work through the in-out layer
                    // parameters. The counter removal left a dangling
                    // `if (secondAccepted)` here, which made this whole MSL
                    // source fail to compile -- silently forcing every
                    // "Metal Compute Shader" session onto the raster
                    // reference path at CreateComputeFoundation.
                    const bool writeDepth =
                        (polygonAttr & (1u << 11u)) != 0u;

                    if (!writeSecondLayer)
                    {
                        mpd_plot_translucent(
                            color0,
                            depth0,
                            attr0,
                            isShadow,
                            tileColor,
                            sourceAlpha,
                            tileDepth,
                            sourceAttr,
                            writeDepth);
                    }

                    if (writeSecondLayer ||
                        (destinationAttr & 0x3u) != 0u)
                    {
                        mpd_plot_translucent(
                            color1,
                            depth1,
                            attr1,
                            isShadow,
                            tileColor,
                            sourceAlpha,
                            tileDepth,
                            sourceAttr,
                            writeDepth);
                    }
                }
            }
            else
            {
                if (!previousShadowMask)
                    stencil = 0u;

                if (!mpd_depth_pass(
                        depth0,
                        tileDepth,
                        equalDepth,
                        config.wBuffer != 0u))
                {
                    stencil = 0x1u;
                }

                if ((destinationAttr & 0x3u) != 0u &&
                    !mpd_depth_pass(
                        depth1,
                        tileDepth,
                        equalDepth,
                        config.wBuffer != 0u))
                {
                    stencil |= 0x2u;
                }
            }
        }
    }

    const uint layerStride = pixelCount;
    resultColor[gid] = color0;
    resultColor[layerStride + gid] = color1;
    resultDepth[gid] = depth0;
    resultDepth[layerStride + gid] = depth1;
    resultAttr[gid] = attr0;
    resultAttr[layerStride + gid] = attr1;

}
)MSL";
