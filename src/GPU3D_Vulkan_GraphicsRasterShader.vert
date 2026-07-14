#version 450

layout(location = 0) in vec4 vPosition;
layout(location = 1) in vec2 vTexcoord;
layout(location = 2) in uvec4 vColor;
layout(location = 3) in uvec4 vTriInfo0In;
layout(location = 4) in uvec3 vTriInfo1In;

layout(push_constant) uniform PushConsts
{
    uint width;
    uint height;
    uint clearColor;
    uint clearDepth;
    uint triangleCount;
    uint dispCnt;
    uint alphaRef;
    uint fogColor;
    uint fogOffset;
    uint fogShift;
    uint clearAttr;
    uint fogDensityPacked[9];
    uint edgeColorPacked[8];
    uint variantKey;
    uint passIndex;
    uint triangleBase;
    uint depthBlendMode;
} pc;

layout(location = 0) smooth out vec3 fColor;
layout(location = 1) smooth out vec2 fTexcoord;
layout(location = 2) noperspective out float fDepthLinear;
layout(location = 3) smooth out float fDepthPerspective;
layout(location = 4) flat out uvec4 fTriInfo0;
layout(location = 5) flat out uvec4 fTriInfo1;

void main()
{
    float x = vPosition.x;
    float y = vPosition.y;
    float z = vPosition.z;
    float reciprocalW = vPosition.w;
    float depth = clamp(z * (1.0 / 16777216.0), 0.0, 1.0);

    float rawW = reciprocalW > 0.000001 ? (1.0 / reciprocalW) : 1.0;
    float clipW = rawW * (1.0 / 65536.0);
    vec2 screenSize = vec2(max(pc.width, 1u), max(pc.height, 1u));
    vec2 ndcXY = ((vec2(x, y) * 2.0) / screenSize) - 1.0;

    gl_Position = vec4(ndcXY * clipW, depth * clipW, clipW);

    fDepthLinear = depth;
    fDepthPerspective = depth;
    fColor = vec3(vColor.rgb) * (1.0 / 255.0);
    fTexcoord = vTexcoord * (1.0 / 16.0);
    fTriInfo0 = vTriInfo0In;
    fTriInfo1 = uvec4(vTriInfo1In, 0u);
}
