#version 450

// Ported from Sapphire's GPU3D_Vulkan_GraphicsEdgeShader.frag.
layout(set = 0, binding = 0) uniform sampler2D AttrBuffer;
layout(set = 0, binding = 1) uniform sampler2D DepthBuffer;

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

bool isgood(vec4 attr, float depth, int refPolyID, float refDepth)
{
    int polyid = int(attr.r * 63.0);
    return polyid != refPolyID && refDepth < depth;
}

vec3 unpackEdgeColor(uint packedColor)
{
    return vec3(
        float(packedColor & 0x3Fu),
        float((packedColor >> 8u) & 0x3Fu),
        float((packedColor >> 16u) & 0x3Fu)) * (1.0 / 63.0);
}

vec3 edgeColorForPolyId(int polyid)
{
    if ((pc.variantKey & 0x80000000u) != 0u &&
        uint(polyid) == (pc.variantKey & 0x3Fu))
    {
        return unpackEdgeColor(pc.triangleBase);
    }
    return unpackEdgeColor(pc.edgeColorPacked[uint(polyid) >> 3u]);
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    ivec2 maximum = ivec2(int(pc.width) - 1, int(pc.height) - 1);
    vec4 attr = texelFetch(AttrBuffer, coord, 0);
    float depth = texelFetch(DepthBuffer, coord, 0).r;
    int polyid = int(attr.r * 63.0);
    vec4 result = vec4(0.0);

    if (attr.g != 0.0)
    {
        ivec2 up = clamp(coord + ivec2(0, -1), ivec2(0), maximum);
        ivec2 down = clamp(coord + ivec2(0, 1), ivec2(0), maximum);
        ivec2 left = clamp(coord + ivec2(-1, 0), ivec2(0), maximum);
        ivec2 right = clamp(coord + ivec2(1, 0), ivec2(0), maximum);
        if (isgood(texelFetch(AttrBuffer, up, 0),
                   texelFetch(DepthBuffer, up, 0).r, polyid, depth) ||
            isgood(texelFetch(AttrBuffer, down, 0),
                   texelFetch(DepthBuffer, down, 0).r, polyid, depth) ||
            isgood(texelFetch(AttrBuffer, left, 0),
                   texelFetch(DepthBuffer, left, 0).r, polyid, depth) ||
            isgood(texelFetch(AttrBuffer, right, 0),
                   texelFetch(DepthBuffer, right, 0).r, polyid, depth))
        {
            result.rgb = edgeColorForPolyId(polyid);
            result.a = (pc.dispCnt & (1u << 4u)) != 0u ? 0.5 : 1.0;
        }
    }
    oColor = result;
}
