#version 450

layout(set = 0, binding = 0) uniform usampler2DArray frameTexture;
layout(push_constant) uniform PresenterPush
{
    uint layer;
    uint flipX;
    uint flipY;
    uint reserved;
} pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    ivec2 size = textureSize(frameTexture, 0).xy;
    vec2 uv = clamp(texCoord, vec2(0.0), vec2(0.999999));
    if (pc.flipX != 0u) uv.x = 1.0 - uv.x;
    if (pc.flipY != 0u) uv.y = 1.0 - uv.y;
    ivec2 coordinate = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));
    uvec4 pixel = texelFetch(frameTexture, ivec3(coordinate, int(pc.layer)), 0);
    outColor = vec4(pixel) / 255.0;
}
