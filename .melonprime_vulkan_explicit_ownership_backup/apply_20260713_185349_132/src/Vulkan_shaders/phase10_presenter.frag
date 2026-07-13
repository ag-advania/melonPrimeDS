#version 450

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
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

vec3 quantizedNativeReference(vec3 color, uint reg)
{
    uvec3 color8 = uvec3(clamp(color, vec3(0.0), vec3(1.0)) * 255.0 + 0.5);
    uvec3 color6 = (color8 * uvec3(63u) + uvec3(127u)) / uvec3(255u);
    uint mode = reg >> 14u;
    uint factor = min(reg & 0x1Fu, 16u);
    if (mode == 1u)
        color6 += ((uvec3(63u) - color6) * factor) >> 4u;
    else if (mode == 2u)
        color6 -= ((color6 * factor + uvec3(15u)) >> 4u);
    uvec3 expanded = (color6 << 2u) | (color6 >> 4u);
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
            vec4 low3D = texture(nativeReference3D, texCoord);
            float nativeOwnership = texture(nativeRasterCoverage, texCoord).a;
            const float opaqueThreshold = 30.5 / 31.0;
            const float tolerance = 2.0 / 255.0;
            vec3 expected = quantizedNativeReference(low3D.rgb, pc.params.w);
            bool software3DOwnsBase =
                all(lessThanEqual(abs(base.rgb - expected), vec3(tolerance)));
            // Coverage 1.0 identifies an opaque native fragment. Values below
            // 0.75 identify translucent/shadow output and require the stricter
            // native-center parity proof below.
            if (nativeOwnership >= 0.75 &&
                high3D.a >= opaqueThreshold && low3D.a >= opaqueThreshold &&
                software3DOwnsBase)
            {
                // The software 3D plane remains the ownership oracle: only
                // replace pixels where the composed engine-A output matches
                // that plane. Do not require the high-resolution raster color
                // to equal its native sample. That legacy partial-renderer
                // guard rejected the very subpixel, fog, edge, and texture
                // differences the Sapphire-compatible raster is meant to
                // preserve.
                outColor = vec4(
                    clamp(applyMasterBrightness(high3D.rgb, pc.params.w), 0.0, 1.0),
                    1.0);
                return;
            }

            // A provisional translucent fragment can be adopted only when
            // the native raster at this DS pixel's center agrees with the
            // Software RGB6A5 oracle. Checking the center avoids rejecting
            // intentional high-resolution subpixel coverage while preventing
            // an alpha/depth mismatch from producing black effect rectangles.
            if (nativeOwnership >= 0.20 && software3DOwnsBase)
            {
                ivec2 referenceSize = textureSize(nativeReference3D, 0);
                ivec2 referencePixel = clamp(
                    ivec2(texCoord * vec2(referenceSize)),
                    ivec2(0),
                    referenceSize - ivec2(1));
                vec2 referenceCenter =
                    (vec2(referencePixel) + vec2(0.5)) / vec2(referenceSize);
                vec4 highCenter = texture(nativeHighResolution3D, referenceCenter);
                const float alphaTolerance = 1.5 / 31.0;
                bool centerRgbMatches = all(lessThanEqual(
                    abs(highCenter.rgb - low3D.rgb), vec3(tolerance)));
                bool centerAlphaMatches =
                    abs(highCenter.a - low3D.a) <= alphaTolerance;
                if (centerRgbMatches && centerAlphaMatches)
                {
                    outColor = vec4(
                        clamp(applyMasterBrightness(high3D.rgb, pc.params.w), 0.0, 1.0),
                        1.0);
                    return;
                }
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
