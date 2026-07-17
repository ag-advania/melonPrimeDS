// MelonPrimeDS -- Metal shader. Extracted from src/GPU_MetalFullGpuMethods.inc (kMetalFullGpuOutputShaderSource) by
// PR-14 (MSL asset/metallib). Content is unchanged from the embedded
// R"MSL(...)MSL" literal; only the physical location moved so it can
// be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;

struct FullGpuVertexOut
{
    float4 position [[position]];
};

vertex FullGpuVertexOut mp_full_gpu_output_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    FullGpuVertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

struct FullGpuOutputConfig
{
    uint2 outputSize;
    uint scale;
    uint outputLayer;
    uint screenSwap[192];
    uint brightnessA[192];
    uint brightnessB[192];
};

static inline float3 mp_full_apply_brightness(float3 color, uint reg)
{
    uint mode = reg >> 14u;
    float factor = float(min(reg & 0x1Fu, 16u)) / 16.0;
    if (mode == 1u)
        return mix(color, float3(1.0), factor);
    if (mode == 2u)
        return color * (1.0 - factor);
    return color;
}

fragment float4 mp_full_gpu_output_fs(
    FullGpuVertexOut in [[stage_in]],
    constant FullGpuOutputConfig& config [[buffer(0)]],
    texture2d<float, access::read> engineA [[texture(0)]],
    texture2d<float, access::read> engineB [[texture(1)]])
{
    uint2 coord = min(
        uint2(in.position.xy),
        config.outputSize - uint2(1u, 1u));
    uint line = min(coord.y / max(config.scale, 1u), 191u);
    bool swap = config.screenSwap[line] != 0u;
    bool selectA = config.outputLayer == 0u ? swap : !swap;
    float4 color = selectA ? engineA.read(coord) : engineB.read(coord);
    uint brightness = selectA
        ? config.brightnessA[line]
        : config.brightnessB[line];
    color.rgb = mp_full_apply_brightness(color.rgb, brightness);
    return float4(clamp(color.rgb, 0.0, 1.0), 1.0);
}
