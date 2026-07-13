#version 450

layout(set = 0, binding = 0) uniform usampler2D ClearBitmapColor;
layout(set = 0, binding = 1) uniform usampler2D ClearBitmapDepth;

layout(push_constant) uniform ClearBitmapPush
{
    vec2 Offset;
    uint OpaquePolyId;
    uint Padding;
} Push;

layout(location = 0) in vec2 fTexcoord;
layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oAttr;
layout(location = 2) out float oDepth;

void main()
{
    vec2 position = fTexcoord + Push.Offset;
    uvec4 color = texture(ClearBitmapColor, position);
    uint depth = texture(ClearBitmapDepth, position).r;

    oColor = vec4(color) / vec4(63.0, 63.0, 63.0, 31.0);
    oAttr = vec4(
        float(Push.OpaquePolyId) / 63.0,
        0.0,
        float(depth >> 24),
        1.0);
    oDepth = float(depth & 0x00FFFFFFu) / 16777216.0;
    gl_FragDepth = oDepth;
}
