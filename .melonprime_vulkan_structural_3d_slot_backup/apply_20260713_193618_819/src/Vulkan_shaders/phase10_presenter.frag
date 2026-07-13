#version 450

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
// MELONPRIME_VULKAN_EXPLICIT_3D_OWNERSHIP_V1
layout(set = 0, binding = 0) uniform sampler2DArray screenTexture;
layout(set = 0, binding = 1) uniform sampler2D hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray radarTexture;
layout(set = 0, binding = 3) uniform sampler2D nativeHighResolution3D;
layout(set = 0, binding = 4) uniform sampler2D nativeReference3D;
layout(set = 0, binding = 5) uniform sampler2D nativeRasterCoverage;

layout(push_constant) uniform DirectPush
{
    vec4 transform0;
    vec4 transform1;
    vec4 geometry;
    vec4 radar;
    uvec4 params;
} pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

const vec3 radarPalette[15] = vec3[15](
    vec3(192.0, 248.0, 104.0),
    vec3(248.0, 168.0, 168.0),
    vec3(224.0,  48.0,  48.0),
    vec3(160.0, 160.0, 160.0),
    vec3(200.0, 200.0, 200.0),
    vec3(144.0, 144.0, 144.0),
    vec3(248.0, 128.0,  16.0),
    vec3(248.0, 208.0, 160.0),
    vec3(216.0, 104.0,   0.0),
    vec3(136.0, 224.0,   8.0),
    vec3(200.0, 248.0, 128.0),
    vec3(104.0, 184.0,   0.0),
    vec3( 16.0, 152.0, 200.0),
    vec3( 40.0, 216.0, 248.0),
    vec3(168.0, 168.0, 168.0));

vec3 applyMasterBrightness(vec3 color, uint reg)
{
    uint mode = reg >> 14u;
    float factor = float(min(reg & 0x1Fu, 16u)) / 16.0;
    if (mode == 1u)
        return mix(color, vec3(1.0), factor);
    if (mode == 2u)
        return color * (1.0 - factor);
    return color;
}

// MELONPRIME_VULKAN_STRUCTURED_3D_COMPOSITION_V1
uvec4 packedCompositionBytes(vec4 packed)
{
    return uvec4(clamp(
        floor(packed * 255.0 + 0.5),
        vec4(0.0),
        vec4(255.0)));
}

uint unpackCompositionMetadata(uvec4 packed)
{
    return ((packed.r >> 6u) & 0x3u) |
        (((packed.g >> 6u) & 0x3u) << 2u) |
        (((packed.b >> 6u) & 0x3u) << 4u) |
        ((packed.a & 0x7Fu) << 6u);
}

uvec3 blendColor4(
    uvec3 top, uvec3 bottom, uint eva, uint evb)
{
    eva = min(eva, 16u);
    evb = min(evb, 16u);
    return min(
        uvec3(63u),
        (top * eva + bottom * evb + uvec3(8u)) >> 4u);
}

uvec3 blendColor5(
    uvec3 top, uvec3 bottom, uint alpha5)
{
    uint eva = min(alpha5, 31u) + 1u;
    if (eva == 32u)
        return top;
    uint evb = 32u - eva;
    return min(
        uvec3(63u),
        (top * eva + bottom * evb + uvec3(16u)) >> 5u);
}

uvec3 brightnessUp6(
    uvec3 color, uint factor, uint bias)
{
    factor = min(factor, 16u);
    return color +
        (((uvec3(63u) - color) * factor + uvec3(bias)) >> 4u);
}

uvec3 brightnessDown6(
    uvec3 color, uint factor, uint bias)
{
    factor = min(factor, 16u);
    return color -
        ((color * factor + uvec3(bias)) >> 4u);
}

uvec3 applyComposition6(
    uvec3 top,
    uvec3 under,
    uint mode,
    uint factorA,
    uint factorB,
    uint alpha5)
{
    if (mode == 2u)
        return blendColor5(top, under, alpha5);
    if (mode == 3u)
        return blendColor4(top, under, factorA, factorB);
    if (mode == 4u)
        return brightnessUp6(top, factorA, 8u);
    if (mode == 5u)
        return brightnessDown6(top, factorA, 7u);
    return top;
}

uvec3 applyMasterBrightness6(
    uvec3 color, uint reg)
{
    uint mode = reg >> 14u;
    uint factor = min(reg & 0x1Fu, 16u);
    if (mode == 1u)
        return brightnessUp6(color, factor, 0u);
    if (mode == 2u)
        return brightnessDown6(color, factor, 15u);
    return color;
}

vec3 expandColor6(uvec3 color)
{
    uvec3 expanded = (color << 2u) | (color >> 4u);
    return vec3(expanded) / 255.0;
}

void main()
{
    uint mode = pc.params.x;
    vec2 sourceScale = pc.geometry.zw;

    if (mode == 0u)
    {
        vec2 uv = clamp(
            texCoord * sourceScale,
            vec2(0.0),
            max(sourceScale - vec2(0.000001), vec2(0.0)));
        vec4 base = texture(screenTexture, vec3(uv, float(pc.params.y)));

        // params.z is zero when native high-resolution raster is unavailable;
        // otherwise it is EngineAScreen + 1. params.w is MasterBrightnessA.
        if (pc.params.z != 0u && pc.params.y + 1u == pc.params.z)
        {
            vec4 high3D = texture(nativeHighResolution3D, texCoord);
            float nativeOwnership = texture(nativeRasterCoverage, texCoord).a;

            ivec2 compositionSize = textureSize(nativeReference3D, 0);
            ivec2 compositionPixel = clamp(
                ivec2(texCoord * vec2(compositionSize)),
                ivec2(0),
                compositionSize - ivec2(1));
            uvec4 packedComposition = packedCompositionBytes(
                texelFetch(nativeReference3D, compositionPixel, 0));
            uint metadata = unpackCompositionMetadata(packedComposition);
            uint compositionMode = metadata & 0x7u;
            uint factorA = (metadata >> 3u) & 0x1Fu;
            uint factorB = (metadata >> 8u) & 0x1Fu;

            // The Software 2D renderer now publishes the exact operation used
            // when BG0/3D occupied the top slot. Native high-resolution opaque,
            // translucent, shadow, EVA/EVB, and brightness output can therefore
            // be adopted without comparing it against a low-resolution RGB oracle.
            if (compositionMode != 0u && nativeOwnership >= 0.20)
            {
                uvec3 top6 = uvec3(clamp(
                    floor(high3D.rgb * 63.0 + 0.5),
                    vec3(0.0),
                    vec3(63.0)));
                uvec3 under6 = packedComposition.rgb & uvec3(0x3Fu);
                uint alpha5 = uint(clamp(
                    floor(high3D.a * 31.0 + 0.5),
                    0.0,
                    31.0));
                uvec3 composed6 = applyComposition6(
                    top6,
                    under6,
                    compositionMode,
                    factorA,
                    factorB,
                    alpha5);
                composed6 = applyMasterBrightness6(
                    composed6, pc.params.w);
                outColor = vec4(expandColor6(composed6), 1.0);
                return;
            }
        }

        outColor = vec4(base.rgb, 1.0);
        return;
    }

    if (mode == 1u)
    {
        vec2 uv = clamp(
            texCoord * sourceScale,
            vec2(0.0),
            max(sourceScale - vec2(0.000001), vec2(0.0)));
        outColor = texture(hudTexture, uv);
        return;
    }

    vec2 centered = texCoord * 2.0 - 1.0;
    float distanceSquared = dot(centered, centered);
    if (distanceSquared > 1.0)
        discard;

    float alpha = pc.radar.w *
        (1.0 - smoothstep(0.95, 1.0, distanceSquared));
    vec2 sourceUv = pc.radar.xy + centered *
        vec2(pc.radar.z, pc.radar.z * (256.0 / 192.0));
    sourceUv *= sourceScale;

    vec4 pixel = texture(radarTexture, vec3(sourceUv, float(pc.params.y)));
    vec3 quantized = round(pixel.rgb * 255.0);

    bool paletteMatch = false;
    for (int index = 0; index < 15; ++index)
    {
        if (all(equal(quantized, radarPalette[index])))
        {
            paletteMatch = true;
            break;
        }
    }
    if (!paletteMatch)
        discard;

    outColor = vec4(pixel.rgb * alpha, alpha);
}
