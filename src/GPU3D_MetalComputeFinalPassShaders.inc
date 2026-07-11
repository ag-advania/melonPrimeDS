// MELONPRIME_METAL_COMPUTE_FINAL_PASS_V1
// GPU-resident DS final pass: edge marking, fog, anti-aliasing and GPU diff diagnostics.
static constexpr const char* kMetalComputeFinalPassSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

constant uint MPF_SummaryPixels = 0u;
constant uint MPF_SummaryEdgePixels = 1u;
constant uint MPF_SummaryFogTopPixels = 2u;
constant uint MPF_SummaryFogSecondPixels = 3u;
constant uint MPF_SummaryAAPixels = 4u;
constant uint MPF_SummaryNonzeroAlpha = 5u;
constant uint MPF_SummaryColorHash = 6u;
constant uint MPF_SummaryReserved = 7u;
constant uint MPF_SummaryWords = 8u;

constant uint MPF_DiffPixels = 0u;
constant uint MPF_DiffDifferent = 1u;
constant uint MPF_DiffExact = 2u;
constant uint MPF_DiffMaxChannelDelta = 3u;
constant uint MPF_DiffSumChannelDelta = 4u;
constant uint MPF_DiffComputeHash = 5u;
constant uint MPF_DiffReferenceHash = 6u;
constant uint MPF_DiffVerticalFlipDifferent = 7u;
constant uint MPF_DiffWords = 8u;

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

kernel void mp_compute_clear_final_pass_summary(
    device atomic_uint* summary [[buffer(0)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid < MPF_SummaryWords)
        atomic_store_explicit(&summary[gid], 0u, memory_order_relaxed);
}

kernel void mp_compute_clear_final_diff_summary(
    device atomic_uint* summary [[buffer(0)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid < MPF_DiffWords)
        atomic_store_explicit(&summary[gid], 0u, memory_order_relaxed);
}

kernel void mp_compute_final_pass(
    device const uint* resultColor [[buffer(0)]],
    device const uint* resultDepth [[buffer(1)]],
    device const uint* resultAttr [[buffer(2)]],
    device atomic_uint* summary [[buffer(3)]],
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

    bool edgeApplied = false;
    bool fogTopApplied = false;
    bool fogSecondApplied = false;
    bool aaApplied = false;

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
            edgeApplied = true;
        }
    }

    // DISP3DCNT bit 7: fog enable. Attr bit 15 is the per-pixel fog flag.
    if ((config.dispCnt & (1u << 7u)) != 0u)
    {
        if ((attr0 & (1u << 15u)) != 0u)
        {
            color0 = mpf_blend_fog(color0, depth0, config, tables);
            fogTopApplied = true;
        }
        if ((attr0 & 0xFu) != 0u &&
            (attr1 & (1u << 15u)) != 0u)
        {
            color1 = mpf_blend_fog(color1, depth1, config, tables);
            fogSecondApplied = true;
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
        aaApplied = true;
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

    atomic_fetch_add_explicit(&summary[MPF_SummaryPixels], 1u, memory_order_relaxed);
    if (edgeApplied)
        atomic_fetch_add_explicit(&summary[MPF_SummaryEdgePixels], 1u, memory_order_relaxed);
    if (fogTopApplied)
        atomic_fetch_add_explicit(&summary[MPF_SummaryFogTopPixels], 1u, memory_order_relaxed);
    if (fogSecondApplied)
        atomic_fetch_add_explicit(&summary[MPF_SummaryFogSecondPixels], 1u, memory_order_relaxed);
    if (aaApplied)
        atomic_fetch_add_explicit(&summary[MPF_SummaryAAPixels], 1u, memory_order_relaxed);
    if (a != 0u)
        atomic_fetch_add_explicit(&summary[MPF_SummaryNonzeroAlpha], 1u, memory_order_relaxed);
    atomic_fetch_xor_explicit(
        &summary[MPF_SummaryColorHash],
        color0 + gid * 0x9E3779B9u,
        memory_order_relaxed);
}

static inline uint4 mpf_quantize_rgba8(float4 value)
{
    return uint4(clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

kernel void mp_compute_compare_final(
    device atomic_uint* summary [[buffer(0)]],
    constant MPFFinalConfig& config [[buffer(1)]],
    texture2d<float, access::read> computeTexture [[texture(0)]],
    texture2d<float, access::read> referenceTexture [[texture(1)]],
    uint gid [[thread_position_in_grid]])
{
    const uint pixelCount = config.screenWidth * config.screenHeight;
    if (gid >= pixelCount) return;
    const uint x = gid % config.screenWidth;
    const uint y = gid / config.screenWidth;
    const uint2 coordinate = uint2(x, y);
    const uint2 flippedCoordinate = uint2(x, config.screenHeight - 1u - y);

    const uint4 compute = mpf_quantize_rgba8(computeTexture.read(coordinate));
    const uint4 reference = mpf_quantize_rgba8(referenceTexture.read(coordinate));
    const uint4 flipped = mpf_quantize_rgba8(referenceTexture.read(flippedCoordinate));
    const uint4 delta = uint4(
        abs(int(compute.r) - int(reference.r)),
        abs(int(compute.g) - int(reference.g)),
        abs(int(compute.b) - int(reference.b)),
        abs(int(compute.a) - int(reference.a)));
    const uint4 flippedDelta = uint4(
        abs(int(compute.r) - int(flipped.r)),
        abs(int(compute.g) - int(flipped.g)),
        abs(int(compute.b) - int(flipped.b)),
        abs(int(compute.a) - int(flipped.a)));
    const uint maxDelta = max(max(delta.r, delta.g), max(delta.b, delta.a));
    const uint sumDelta = delta.r + delta.g + delta.b + delta.a;
    const bool different = maxDelta > 2u;
    const bool flippedDifferent =
        max(max(flippedDelta.r, flippedDelta.g),
            max(flippedDelta.b, flippedDelta.a)) > 2u;

    atomic_fetch_add_explicit(&summary[MPF_DiffPixels], 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(
        &summary[different ? MPF_DiffDifferent : MPF_DiffExact],
        1u,
        memory_order_relaxed);
    atomic_fetch_max_explicit(&summary[MPF_DiffMaxChannelDelta], maxDelta, memory_order_relaxed);
    atomic_fetch_add_explicit(&summary[MPF_DiffSumChannelDelta], sumDelta, memory_order_relaxed);
    atomic_fetch_xor_explicit(
        &summary[MPF_DiffComputeHash],
        compute.r | (compute.g << 8u) | (compute.b << 16u) | (compute.a << 24u),
        memory_order_relaxed);
    atomic_fetch_xor_explicit(
        &summary[MPF_DiffReferenceHash],
        reference.r | (reference.g << 8u) | (reference.b << 16u) | (reference.a << 24u),
        memory_order_relaxed);
    if (flippedDifferent)
        atomic_fetch_add_explicit(
            &summary[MPF_DiffVerticalFlipDifferent], 1u, memory_order_relaxed);
}
)MSL";
