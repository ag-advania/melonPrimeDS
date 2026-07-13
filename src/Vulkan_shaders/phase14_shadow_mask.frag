#version 450

layout(push_constant) uniform NativeRasterPush
{
    vec2 screenSize;
    uint textured;
    uint textureMode;
    uint wBuffer;
    uint renderXPos;
    uint renderDispCnt;
    uint drawFlags;
    uint texParam;
    uint clearAttr;
} pc;

layout(location = 2) noperspective in float fDepthLinear;
layout(location = 3) smooth in float fDepthPerspective;

// Shadow masks only update stencil on depth failure. Their texture/color
// attributes do not participate, matching Sapphire and the software renderer.
void main()
{
    gl_FragDepth = pc.wBuffer != 0u
        ? fDepthPerspective
        : fDepthLinear;
}
