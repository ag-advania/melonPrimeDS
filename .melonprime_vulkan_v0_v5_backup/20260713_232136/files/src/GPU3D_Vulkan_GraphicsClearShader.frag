#version 450

layout(set = 0, binding = 5, std430) readonly buffer ClearBuffer
{
    uint clearWords[];
};

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

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oAttr;
layout(location = 2) out float oDepthValue;

uvec2 resolveDsCoord()
{
    uint width = max(pc.width, 1u);
    uint height = max(pc.height, 1u);
    uint x = min(uint(gl_FragCoord.x), width - 1u);
    uint y = min(uint(gl_FragCoord.y), height - 1u);
    return uvec2((x * 256u) / width, (y * 192u) / height);
}

vec4 unpackNormalizedRgba8(uint packedColor)
{
    return vec4(
        float(packedColor & 0xFFu),
        float((packedColor >> 8u) & 0xFFu),
        float((packedColor >> 16u) & 0xFFu),
        float((packedColor >> 24u) & 0xFFu)) * (1.0 / 255.0);
}

void main()
{
    const uint pixelCount = 256u * 192u;
    const uvec2 dsCoord = resolveDsCoord();
    const uint index = dsCoord.y * 256u + dsCoord.x;

    const uint packedColor = clearWords[index];
    const uint packedAttr = clearWords[pixelCount + index];
    const float depth = uintBitsToFloat(clearWords[(pixelCount * 2u) + index]);

    oColor = unpackNormalizedRgba8(packedColor);
    oAttr = unpackNormalizedRgba8(packedAttr);
    oDepthValue = depth;
    gl_FragDepth = depth;
}
