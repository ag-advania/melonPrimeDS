#version 450

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
layout(push_constant) uniform NativeRasterPush
{
    vec2 screenSize;
    uint textured;
    uint textureMode;
    uint wBuffer;
    uint renderXPos;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) in uvec4 packed0;
layout(location = 1) in uint packedFlags;
layout(location = 2) in uint textureLayer;
layout(location = 3) in uint textureSize;

layout(location = 0) out vec4 fColor;
layout(location = 1) out vec2 fTexcoord;
layout(location = 2) out float fDepth;
layout(location = 3) flat out uint fPolygonAttr;

void main()
{
    uvec4 position = uvec4(
        packed0.x & 0xFFFFu,
        packed0.x >> 16,
        packed0.y & 0xFFFFu,
        packed0.y >> 16);
    uint zshift = (packedFlags >> 16) & 0x1Fu;
    float depth = float(position.z << zshift) / 16777216.0;
    float w = float(position.w) / 65536.0;

    // Sapphire's shared accelerated frontend keeps screen coordinates in
    // 12.4 fixed point. Preserve the fractional coverage at every scale.
    vec2 rasterPosition = vec2(position.xy) * (1.0 / 16.0);
    if (pc.renderXPos != 0u)
    {
        int nativeDelta = (pc.renderXPos & 0x100u) != 0u
            ? 512 - int(pc.renderXPos)
            : -int(pc.renderXPos);
        rasterPosition.x += float(nativeDelta) * (pc.screenSize.x / 256.0);
    }

    vec4 clip;
    clip.xy = (rasterPosition * 2.0 / pc.screenSize) - 1.0;
    clip.z = pc.wBuffer != 0u ? 0.0 : depth;
    clip.w = w;
    clip.xyz *= w;
    gl_Position = clip;
    fDepth = depth;

    uvec4 color = uvec4(
        packed0.z & 0xFFu,
        (packed0.z >> 8) & 0xFFu,
        (packed0.z >> 16) & 0xFFu,
        (packed0.z >> 24) & 0xFFu);
    fColor = vec4(color) / vec4(255.0, 255.0, 255.0, 31.0);
    fPolygonAttr = packedFlags;

    ivec2 texel16 = ivec2(
        int(packed0.w << 16) >> 16,
        int(packed0.w) >> 16);
    uvec2 size = max(
        uvec2(textureSize & 0xFFFFu, textureSize >> 16),
        uvec2(1u));
    fTexcoord = vec2(texel16) / (16.0 * vec2(size));
}
