#version 450

// Ported from Sapphire's GPU3D_Vulkan_GraphicsEdgeFogShader.frag.
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

float unpackFogDensity(uint index)
{
    uint clampedIndex = min(index, 33u);
    uint packedWord = pc.fogDensityPacked[clampedIndex / 4u];
    uint packedShift = (clampedIndex % 4u) * 8u;
    return float((packedWord >> packedShift) & 0xFFu);
}

float calculateFogDensity(float depth)
{
    int idepth = int(depth * 16777216.0);
    int densityid;
    int densityfrac;
    if (idepth < int(pc.fogOffset))
    {
        densityid = 0;
        densityfrac = 0;
    }
    else
    {
        uint udepth = uint(idepth) - pc.fogOffset;
        udepth = (udepth >> 2u) << pc.fogShift;
        densityid = int(udepth >> 17u);
        if (densityid >= 32)
        {
            densityid = 32;
            densityfrac = 0;
        }
        else
        {
            densityfrac = int(udepth & 0x1FFFFu);
        }
    }
    return mix(
        unpackFogDensity(uint(densityid)),
        unpackFogDensity(uint(densityid + 1)),
        float(densityfrac) / 131072.0) * (1.0 / 128.0);
}

vec3 unpackFogColor()
{
    return vec3(
        float(pc.fogColor & 0x1Fu),
        float((pc.fogColor >> 5u) & 0x1Fu),
        float((pc.fogColor >> 10u) & 0x1Fu)) * (1.0 / 31.0);
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    ivec2 maximum = ivec2(int(pc.width) - 1, int(pc.height) - 1);
    vec4 depth = texelFetch(DepthBuffer, coord, 0);
    vec4 attr = texelFetch(AttrBuffer, coord, 0);

    float edgeAlpha = 0.0;
    vec3 edgeColor = vec3(0.0);
    int polyid = int(attr.r * 63.0);
    if (attr.g != 0.0)
    {
        ivec2 up = clamp(coord + ivec2(0, -1), ivec2(0), maximum);
        ivec2 down = clamp(coord + ivec2(0, 1), ivec2(0), maximum);
        ivec2 left = clamp(coord + ivec2(-1, 0), ivec2(0), maximum);
        ivec2 right = clamp(coord + ivec2(1, 0), ivec2(0), maximum);
        if (isgood(texelFetch(AttrBuffer, up, 0),
                   texelFetch(DepthBuffer, up, 0).r, polyid, depth.r) ||
            isgood(texelFetch(AttrBuffer, down, 0),
                   texelFetch(DepthBuffer, down, 0).r, polyid, depth.r) ||
            isgood(texelFetch(AttrBuffer, left, 0),
                   texelFetch(DepthBuffer, left, 0).r, polyid, depth.r) ||
            isgood(texelFetch(AttrBuffer, right, 0),
                   texelFetch(DepthBuffer, right, 0).r, polyid, depth.r))
        {
            edgeColor = unpackEdgeColor(pc.edgeColorPacked[uint(polyid) >> 3u]);
            edgeAlpha = (pc.dispCnt & (1u << 4u)) != 0u ? 0.5 : 1.0;
        }
    }

    float fogDensity = attr.b != 0.0
        ? calculateFogDensity(depth.r)
        : 0.0;
    vec3 premultiplied =
        edgeColor * edgeAlpha * (1.0 - fogDensity) +
        unpackFogColor() * fogDensity;
    float alpha = edgeAlpha + fogDensity - edgeAlpha * fogDensity;
    oColor = vec4(premultiplied, alpha);
}
