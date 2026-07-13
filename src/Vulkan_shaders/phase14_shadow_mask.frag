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
} pc;

layout(location = 2) in float fDepth;

// Shadow masks only update stencil on depth failure. Their texture/color
// attributes do not participate, matching Sapphire and the software renderer.
void main()
{
    if (pc.wBuffer != 0u)
        gl_FragDepth = fDepth;
}
