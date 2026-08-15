// MelonPrime Vulkan presenter -- quad fragment stage.
//
// A straight textured quad. The two pipelines built from this module differ only
// in their blend state, not in their code:
//
//   * opaque  -- the DS screens. The composed frame carries no meaningful alpha
//                (the compositor writes BGRX), so blending is disabled and the
//                Tint alpha is ignored.
//   * blended -- the Custom HUD overlay and the OSD strip. Both come from
//                QImage::Format_ARGB32_Premultiplied, so the blend factors are
//                ONE / ONE_MINUS_SRC_ALPHA, matching ScreenPanelGL exactly.
//
// Regenerate with tools/vulkan/compile-present-shaders.py.

#version 450

layout(set = 0, binding = 0) uniform sampler2D Source;

layout(push_constant) uniform PresentPush
{
    vec4 Axis;
    vec4 Origin;
    vec4 UvRect;
    vec4 Tint;
} pc;

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

const uint RadarPaletteSize = 15u;
const uint RadarPalette[RadarPaletteSize] = uint[](
    0xC0F868u, 0xF8A8A8u, 0xE03030u,
    0xA0A0A0u, 0xC8C8C8u, 0x909090u,
    0xF88010u, 0xF8D0A0u, 0xD86800u,
    0x88E008u, 0xC8F880u, 0x68B800u,
    0x1098C8u, 0x28D8F8u, 0xA8A8A8u);

bool IsRadarPaletteColor(vec3 rgb)
{
    uvec3 color = uvec3(round(rgb * 255.0)) & uvec3(0xf8u);
    uint packed = (color.r << 16u) | (color.g << 8u) | color.b;
    for (uint index = 0u; index < RadarPaletteSize; ++index)
    {
        if (packed == RadarPalette[index])
            return true;
    }
    return false;
}

void main()
{
    // Tint.a < 0 selects the GPU-native Custom HUD radar. Tint.xyz carry
    // opacity, native source center Y and native source radius respectively.
    if (pc.Tint.a < 0.0)
    {
        vec2 centered = vUV * 2.0 - 1.0;
        float distanceSquared = dot(centered, centered);
        if (distanceSquared > 1.0)
            discard;

        vec2 sourceUv = vec2(
            (128.0 + centered.x * pc.Tint.z) / 256.0,
            (pc.Tint.y + centered.y * pc.Tint.z) / 192.0);
        vec3 rgb = texture(Source, sourceUv).rgb;
        if (!IsRadarPaletteColor(rgb))
            discard;

        float alpha = pc.Tint.x * (1.0 - smoothstep(0.95, 1.0, distanceSquared));
        FragColor = vec4(rgb * alpha, alpha);
        return;
    }

    FragColor = texture(Source, vUV) * pc.Tint;
}
