#version 450

layout(set = 0, binding = 3) uniform sampler2D AttrBuffer;
layout(set = 0, binding = 4) uniform sampler2D DepthBuffer;

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

bool isless(float a, float b)
{
    return a < b;
}

bool isgood(vec4 attr, float depth, int refPolyID, float refDepth)
{
    int polyid = int(attr.r * 63.0);
    if (polyid != refPolyID && isless(refDepth, depth))
        return true;
    return false;
}

vec3 unpackEdgeColor(uint packedColor)
{
    float r = float(packedColor & 0x3Fu) * (1.0 / 63.0);
    float g = float((packedColor >> 8u) & 0x3Fu) * (1.0 / 63.0);
    float b = float((packedColor >> 16u) & 0x3Fu) * (1.0 / 63.0);
    return vec3(r, g, b);
}

vec3 edgeColorForPolyId(int polyid)
{
    if ((pc.variantKey & 0x80000000u) != 0u && uint(polyid) == (pc.variantKey & 0x3Fu))
        return unpackEdgeColor(pc.triangleBase);
    return unpackEdgeColor(pc.edgeColorPacked[uint(polyid) >> 3u]);
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

    float density0 = unpackFogDensity(uint(densityid));
    float density1 = unpackFogDensity(uint(densityid + 1));
    return mix(density0, density1, float(densityfrac) / 131072.0) * (1.0 / 128.0);
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
    int scale = 1;

    vec4 depth = texelFetch(DepthBuffer, coord, 0);
    vec4 attr = texelFetch(AttrBuffer, coord, 0);

    float edgeAlpha = 0.0;
    vec3 edgeColor = vec3(0.0);
    int polyid = int(attr.r * 63.0);

    if (attr.g != 0.0)
    {
        vec4 depthU = texelFetch(DepthBuffer, coord + ivec2(0, -scale), 0);
        vec4 attrU = texelFetch(AttrBuffer, coord + ivec2(0, -scale), 0);
        vec4 depthD = texelFetch(DepthBuffer, coord + ivec2(0, scale), 0);
        vec4 attrD = texelFetch(AttrBuffer, coord + ivec2(0, scale), 0);
        vec4 depthL = texelFetch(DepthBuffer, coord + ivec2(-scale, 0), 0);
        vec4 attrL = texelFetch(AttrBuffer, coord + ivec2(-scale, 0), 0);
        vec4 depthR = texelFetch(DepthBuffer, coord + ivec2(scale, 0), 0);
        vec4 attrR = texelFetch(AttrBuffer, coord + ivec2(scale, 0), 0);

        if (isgood(attrU, depthU.r, polyid, depth.r)
            || isgood(attrD, depthD.r, polyid, depth.r)
            || isgood(attrL, depthL.r, polyid, depth.r)
            || isgood(attrR, depthR.r, polyid, depth.r))
        {
            edgeColor = edgeColorForPolyId(polyid);
            edgeAlpha = ((pc.dispCnt & (1u << 4u)) != 0u) ? 0.5 : 1.0;
        }
    }

    float fogDensity = attr.b != 0.0 ? calculateFogDensity(depth.r) : 0.0;
    vec3 premultiplied =
        edgeColor * edgeAlpha * (1.0 - fogDensity)
        + unpackFogColor() * fogDensity;
    float alpha = edgeAlpha + fogDensity - (edgeAlpha * fogDensity);
    oColor = vec4(premultiplied, alpha);
}
