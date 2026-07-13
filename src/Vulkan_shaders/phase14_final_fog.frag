#version 450

// Ported from Sapphire's GPU3D_Vulkan_GraphicsFogShader.frag.
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

float unpackFogDensity(uint index)
{
    uint clampedIndex = min(index, 33u);
    uint packedWord = pc.fogDensityPacked[clampedIndex / 4u];
    uint packedShift = (clampedIndex % 4u) * 8u;
    return float((packedWord >> packedShift) & 0xFFu);
}

float calculateFog(float depth)
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

void main()
{
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec4 attr = texelFetch(AttrBuffer, coord, 0);
    float density = attr.b != 0.0
        ? calculateFog(texelFetch(DepthBuffer, coord, 0).r)
        : 0.0;
    oColor = vec4(density);
}
