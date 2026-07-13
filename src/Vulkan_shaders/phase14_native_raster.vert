#version 450

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
layout(push_constant) uniform NativeRasterPush
{
    vec2 screenSize;
    uint textured;
    uint textureMode;
    uint wBuffer;
    uint renderXPos;
    uint renderDispCnt;
    uint drawFlags;
    uint texParam;
    uint clearAttr;
} pc;

layout(location = 0) in uvec4 packed0;
layout(location = 1) in uint packedFlags;
layout(location = 2) in uint textureLayer;
layout(location = 3) in uint textureSize;
layout(location = 4) in uint fullDepth;
layout(location = 5) in uint fullW;

layout(location = 0) out vec4 fColor;
layout(location = 1) out vec2 fTexcoord;
layout(location = 2) noperspective out float fDepthLinear;
layout(location = 3) smooth out float fDepthPerspective;
layout(location = 4) flat out uint fPolygonAttr;

void main()
{
    uvec4 position = uvec4(
        packed0.x & 0xFFFFu,
        packed0.x >> 16,
        packed0.y & 0xFFFFu,
        packed0.y >> 16);
    float depth = clamp(float(fullDepth) / 16777216.0, 0.0, 1.0);
    float w = float(max(fullW, 1u)) / 65536.0;

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
    fDepthLinear = depth;
    fDepthPerspective = depth;

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
    fTexcoord = vec2(texel16) * (1.0 / 16.0);
}
