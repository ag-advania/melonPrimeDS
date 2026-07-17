// MelonPrimeDS -- Metal shader. Extracted from src/GPU_Metal.mm (kMetalVisibleOutputShaderSource) by
// PR-14 (MSL asset/metallib). Content is unchanged from the embedded
// R"MSL(...)MSL" literal; only the physical location moved so it can
// be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;

struct VisibleOutputVertex
{
    float4 position [[position]];
};

vertex VisibleOutputVertex mp_visible_output_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    VisibleOutputVertex out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

struct VisibleOutputConfig
{
    uint2 outputSize;
    uint scale;
    uint outputLayer;
    uint engineALayer;
    uint useHighResolution3D;
    uint masterBrightnessA;
    uint masterBrightnessB;
};

static inline float3 mp_apply_master_brightness(float3 color, uint reg)
{
    uint mode = reg >> 14;
    float factor = float(min(reg & 0x1Fu, 16u)) / 16.0;
    if (mode == 1u)
        return mix(color, float3(1.0), factor);
    if (mode == 2u)
        return color * (1.0 - factor);
    return color;
}

// Reproduce the exact Metal readback -> DS 6-bit -> CPU master-brightness ->
// BGRA8 expansion path. The final CPU composite can then be compared against
// the native 3D sample in the same quantized color space.
static inline float3 mp_cpu_native_3d_reference(
    float3 color,
    uint reg)
{
    uint3 color8 = uint3(
        clamp(color, float3(0.0), float3(1.0)) * 255.0 + 0.5);
    uint3 color6 =
        (color8 * uint3(63u) + uint3(127u)) / uint3(255u);

    uint mode = reg >> 14u;
    uint factor = min(reg & 0x1Fu, 16u);
    if (mode == 1u)
    {
        color6 +=
            ((uint3(63u) - color6) * factor) >> 4u;
    }
    else if (mode == 2u)
    {
        color6 -=
            ((color6 * factor + uint3(15u)) >> 4u);
    }

    uint3 expanded = (color6 << 2u) | (color6 >> 4u);
    return float3(expanded) / 255.0;
}

fragment float4 mp_visible_output_fs(
    VisibleOutputVertex in [[stage_in]],
    constant VisibleOutputConfig& config [[buffer(0)]],
    texture2d_array<float, access::read> cpuComposite [[texture(0)]],
    texture2d<float, access::read> highResolution3D [[texture(1)]],
    texture2d<float, access::read> nativeResolution3D [[texture(2)]])
{
    uint2 outputCoord = min(uint2(in.position.xy), config.outputSize - uint2(1u, 1u));
    uint divisor = max(config.scale, 1u);
    uint2 nativeCoord = min(outputCoord / divisor, uint2(255u, 191u));
    float4 base = cpuComposite.read(nativeCoord, config.outputLayer);

    if (config.useHighResolution3D == 0u || config.scale <= 1u ||
        config.outputLayer != config.engineALayer)
    {
        return float4(base.rgb, 1.0);
    }

    uint2 highCoord = min(outputCoord,
        uint2(highResolution3D.get_width() - 1u,
              highResolution3D.get_height() - 1u));
    float4 high3D = highResolution3D.read(highCoord);
    float4 low3D = nativeResolution3D.read(nativeCoord);

    uint brightness = (config.outputLayer == config.engineALayer)
        ? config.masterBrightnessA
        : config.masterBrightnessB;
    float3 brightHigh3D =
        mp_apply_master_brightness(high3D.rgb, brightness);
    float lowAlpha = clamp(low3D.a, 0.0, 1.0);
    float highAlpha = clamp(high3D.a, 0.0, 1.0);

    // Mode 1 is the normal ownership-gated path. Only replace a subpixel when:
    //   1. native and high-resolution 3D are both fully opaque, and
    //   2. the completed CPU screen pixel matches the exact native 3D color.
    // A BG/OBJ HUD, reticle, window, blend or brightness effect changes the CPU
    // result, so that pixel remains untouched. This prevents opaque 3D clear
    // pixels from overwriting the already-correct Software 2D composite.
    if (config.useHighResolution3D == 1u)
    {
        constexpr float opaqueThreshold = 30.5 / 31.0;
        if (lowAlpha < opaqueThreshold || highAlpha < opaqueThreshold)
            return float4(base.rgb, 1.0);

        float3 expectedNative3D =
            mp_cpu_native_3d_reference(low3D.rgb, brightness);
        constexpr float ownershipTolerance = 2.0 / 255.0;
        if (any(abs(base.rgb - expectedNative3D) >
                float3(ownershipTolerance)))
        {
            return float4(base.rgb, 1.0);
        }

        return float4(clamp(brightHigh3D, 0.0, 1.0), 1.0);
    }

    // Mode 2 is retained only for A/B diagnostics. It reproduces the previous
    // unconditional replacement behavior and may hide native 2D overlays.
    low3D.rgb = mp_apply_master_brightness(low3D.rgb, brightness);
    float3 background = base.rgb;
    if (lowAlpha < (254.0 / 255.0))
    {
        float denominator = max(1.0 - lowAlpha, 1.0 / 255.0);
        background = clamp(
            (base.rgb - low3D.rgb * lowAlpha) / denominator,
            0.0,
            1.0);
    }

    float3 result = mix(background, brightHigh3D, highAlpha);
    return float4(clamp(result, 0.0, 1.0), 1.0);
}


