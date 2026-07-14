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

float unpackFogDensity(uint index)
{
    uint clampedIndex = min(index, 33u);
    uint packedWord = pc.fogDensityPacked[clampedIndex / 4u];
    uint packedShift = (clampedIndex % 4u) * 8u;
    return float((packedWord >> packedShift) & 0xFFu);
}

vec4 calculateFog(float depth)
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
    float density = mix(density0, density1, float(densityfrac) / 131072.0) * (1.0 / 128.0);
    return vec4(density);
}

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 ret = vec4(0.0);
    vec4 depth = texelFetch(DepthBuffer, coord, 0);
    vec4 attr = texelFetch(AttrBuffer, coord, 0);
    if (attr.b != 0.0)
        ret = calculateFog(depth.r);
    oColor = ret;
}
