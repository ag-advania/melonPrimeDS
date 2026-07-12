#version 450

layout(push_constant) uniform TexturedPush
{
    vec2 ScreenSize;
    uint RenderDispCnt;
    uint Reserved;
} Push;

layout(location = 0) in uvec4 Packed0;
layout(location = 1) in uint PackedFlags;
layout(location = 2) in uint TextureLayer;
layout(location = 3) in uint TextureSize;

layout(location = 0) out vec4 fColor;
layout(location = 1) flat out uint fPolygonAttr;
layout(location = 2) out vec3 fTexcoord;
#ifdef W_BUFFER
layout(location = 3) out float fDepth;
#endif

void main()
{
    uvec4 position = uvec4(
        Packed0.x & 0xFFFFu,
        Packed0.x >> 16,
        Packed0.y & 0xFFFFu,
        Packed0.y >> 16);
    uint zshift = (PackedFlags >> 16) & 0x1Fu;
    float depth = float(position.z << zshift) / 16777216.0;
    float w = float(position.w) / 65536.0;

    vec4 clip;
    clip.xy = (vec2(position.xy) * 2.0 / Push.ScreenSize) - 1.0;
#ifdef W_BUFFER
    clip.z = 0.0;
    fDepth = depth;
#else
    clip.z = depth;
#endif
    clip.w = w;
    clip.xyz *= w;
    gl_Position = clip;

    uvec4 color = uvec4(
        Packed0.z & 0xFFu,
        (Packed0.z >> 8) & 0xFFu,
        (Packed0.z >> 16) & 0xFFu,
        (Packed0.z >> 24) & 0xFFu);
    fColor = vec4(color) / vec4(255.0, 255.0, 255.0, 31.0);
    fPolygonAttr = PackedFlags;

    ivec2 texel16 = ivec2(
        int(Packed0.w << 16) >> 16,
        int(Packed0.w) >> 16);
    uvec2 size = max(uvec2(TextureSize & 0xFFFFu, TextureSize >> 16), uvec2(1u));
    fTexcoord = vec3(vec2(texel16) / (16.0 * vec2(size)), float(TextureLayer & 0xFFFFu));
}
