#version 450

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
layout(set = 0, binding = 0) uniform sampler2DArray screenTexture;
layout(set = 0, binding = 1) uniform sampler2D hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray radarTexture;

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
        vec4 pixel = texture(
            screenTexture,
            vec3(uv, float(pc.params.y)));
        outColor = vec4(pixel.rgb, 1.0);
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

    float alpha =
        pc.radar.w *
        (1.0 - smoothstep(0.95, 1.0, distanceSquared));
    vec2 sourceUv =
        pc.radar.xy +
        centered *
        vec2(pc.radar.z, pc.radar.z * (256.0 / 192.0));
    sourceUv *= sourceScale;

    vec4 pixel = texture(
        radarTexture,
        vec3(sourceUv, float(pc.params.y)));
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
