// MelonPrimeDS -- Metal shader. Extracted from src/GPU3D_MetalComputeFinalPassShaders.inc (kMetalComputeFinalPassSource) by
// PR-14 (MSL asset/metallib). Content is unchanged from the embedded
// R"MSL(...)MSL" literal; only the physical location moved so it can
// be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;


struct MPFFinalConfig
{
    uint screenWidth;
    uint screenHeight;
    uint dispCnt;
    uint clearDepth;
    uint clearAttr;
    uint fogOffset;
    uint fogShift;
    uint fogColor;
};

static inline uint mpf_blend_fog(
    uint color,
    uint depth,
    constant MPFFinalConfig& config,
    device const uint* tables)
{
    uint densityId = 0u;
    uint densityFrac = 0u;
    if (depth >= config.fogOffset)
    {
        depth -= config.fogOffset;
        depth = (depth >> 2u) << config.fogShift;
        densityId = depth >> 17u;
        if (densityId >= 32u)
        {
            densityId = 32u;
            densityFrac = 0u;
        }
        else
        {
            densityFrac = depth & 0x1FFFFu;
        }
    }

    const uint density0 = tables[8u + densityId];
    const uint density1 = tables[8u + min(densityId + 1u, 33u)];
    uint density =
        ((density0 * (0x20000u - densityFrac)) +
         (density1 * densityFrac)) >> 17u;
    density = min(density, 128u);

    const uint colorRB = color & 0x003F003Fu;
    const uint colorGA = (color >> 8u) & 0x003F003Fu;
    const uint fogRB = config.fogColor & 0x003F003Fu;
    const uint fogGA = (config.fogColor >> 8u) & 0x001F003Fu;
    const uint finalRB =
        (((fogRB * density) + (colorRB * (128u - density))) >> 7u) &
        0x003F003Fu;
    const uint finalGA =
        (((fogGA * density) + (colorGA * (128u - density))) >> 7u) &
        0x001F003Fu;

    // DISP3DCNT bit 6: fog affects alpha only.
    return (config.dispCnt & (1u << 6u)) != 0u
        ? ((color & 0x00FFFFFFu) | ((finalGA >> 16u) << 24u))
        : (finalRB | (finalGA << 8u));
}

kernel void mp_compute_final_pass(
    device const uint* resultColor [[buffer(0)]],
    device const uint* resultDepth [[buffer(1)]],
    device const uint* resultAttr [[buffer(2)]],
    device const uint* tables [[buffer(4)]],
    constant MPFFinalConfig& config [[buffer(5)]],
    texture2d<float, access::write> finalTexture [[texture(0)]],
    uint gid [[thread_position_in_grid]])
{
    const uint pixelCount = config.screenWidth * config.screenHeight;
    if (gid >= pixelCount) return;

    const uint x = gid % config.screenWidth;
    const uint y = gid / config.screenWidth;
    const uint second = pixelCount + gid;

    uint color0 = resultColor[gid];
    uint color1 = resultColor[second];
    const uint depth0 = resultDepth[gid];
    const uint depth1 = resultDepth[second];
    uint attr0 = resultAttr[gid];
    const uint attr1 = resultAttr[second];


    // DISP3DCNT bit 5: edge marking.
    if ((config.dispCnt & (1u << 5u)) != 0u &&
        (attr0 & 0xFu) != 0u)
    {
        uint4 otherAttr = uint4(config.clearAttr);
        uint4 otherDepth = uint4(config.clearDepth);
        if (x > 0u)
        {
            otherAttr.x = resultAttr[gid - 1u];
            otherDepth.x = resultDepth[gid - 1u];
        }
        if (x + 1u < config.screenWidth)
        {
            otherAttr.y = resultAttr[gid + 1u];
            otherDepth.y = resultDepth[gid + 1u];
        }
        if (y > 0u)
        {
            otherAttr.z = resultAttr[gid - config.screenWidth];
            otherDepth.z = resultDepth[gid - config.screenWidth];
        }
        if (y + 1u < config.screenHeight)
        {
            otherAttr.w = resultAttr[gid + config.screenWidth];
            otherDepth.w = resultDepth[gid + config.screenWidth];
        }

        const uint polygonId = (attr0 >> 24u) & 0x3Fu;
        const uint4 otherPolygonId = (otherAttr >> 24u) & uint4(0x3Fu);
        const bool edge =
            (polygonId != otherPolygonId.x && depth0 < otherDepth.x) ||
            (polygonId != otherPolygonId.y && depth0 < otherDepth.y) ||
            (polygonId != otherPolygonId.z && depth0 < otherDepth.z) ||
            (polygonId != otherPolygonId.w && depth0 < otherDepth.w);
        if (edge)
        {
            color0 = tables[polygonId >> 3u] | (color0 & 0x1F000000u);
            attr0 = (attr0 & 0xFFFFE0FFu) | 0x00001000u;
        }
    }

    // DISP3DCNT bit 7: fog enable. Attr bit 15 is the per-pixel fog flag.
    if ((config.dispCnt & (1u << 7u)) != 0u)
    {
        if ((attr0 & (1u << 15u)) != 0u)
        {
            color0 = mpf_blend_fog(color0, depth0, config, tables);
        }
        if ((attr0 & 0xFu) != 0u &&
            (attr1 & (1u << 15u)) != 0u)
        {
            color1 = mpf_blend_fog(color1, depth1, config, tables);
        }
    }

    // DISP3DCNT bit 4: anti-aliasing. The low attr bits mark an edge and
    // bits 8..12 hold the 0..31 coverage produced by span rasterisation.
    if ((config.dispCnt & (1u << 4u)) != 0u &&
        (attr0 & 0x3u) != 0u)
    {
        uint coverage = (attr0 >> 8u) & 0x1Fu;
        if (coverage != 0u)
        {
            uint topRB = color0 & 0x003F003Fu;
            uint topG = color0 & 0x00003F00u;
            uint topA = (color0 >> 24u) & 0x1Fu;
            const uint bottomRB = color1 & 0x003F003Fu;
            const uint bottomG = color1 & 0x00003F00u;
            const uint bottomA = (color1 >> 24u) & 0x1Fu;
            coverage++;
            if (bottomA > 0u)
            {
                topRB = (((topRB * coverage) +
                          (bottomRB * (32u - coverage))) >> 5u) &
                        0x003F003Fu;
                topG = (((topG * coverage) +
                         (bottomG * (32u - coverage))) >> 5u) &
                       0x00003F00u;
            }
            topA = ((topA * coverage) +
                    (bottomA * (32u - coverage))) >> 5u;
            color0 = topRB | topG | (topA << 24u);
        }
        else
        {
            color0 = color1;
        }
    }

    const uint r = color0 & 0x3Fu;
    const uint g = (color0 >> 8u) & 0x3Fu;
    const uint b = (color0 >> 16u) & 0x3Fu;
    const uint a = (color0 >> 24u) & 0x1Fu;
    finalTexture.write(
        float4(float(r) / 63.0f,
               float(g) / 63.0f,
               float(b) / 63.0f,
               float(a) / 31.0f),
        uint2(x, y));

}

