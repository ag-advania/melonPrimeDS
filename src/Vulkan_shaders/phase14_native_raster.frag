#version 450

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
layout(set = 0, binding = 0) uniform usampler2DArray nativeTexture;
layout(std140, set = 0, binding = 1) uniform NativeRasterToonTable
{
    vec4 toon[32];
} toonConfig;

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

layout(location = 0) in vec4 fColor;
layout(location = 1) in vec2 fTexcoord;
layout(location = 2) in float fDepth;
layout(location = 3) flat in uint fPolygonAttr;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAttr;

void main()
{
    vec4 color = fColor;
    int toonIndex = int(clamp(fColor.r, 0.0, 1.0) * 31.0);
    bool highlight = (pc.renderDispCnt & (1u << 1u)) != 0u;
    if (pc.textureMode == 2u)
    {
        color.rgb = highlight
            ? fColor.rrr
            : toonConfig.toon[toonIndex].rgb;
    }

    if (pc.textured != 0u)
    {
        vec4 textureColor = vec4(texture(nativeTexture, vec3(fTexcoord, 0.0))) /
            vec4(63.0, 63.0, 63.0, 31.0);
        if ((pc.textureMode & 1u) != 0u)
        {
            color.rgb = textureColor.rgb * textureColor.a +
                color.rgb * (1.0 - textureColor.a);
        }
        else
        {
            color *= textureColor;
        }
    }


    if (pc.textureMode == 2u && highlight)
        color.rgb = min(color.rgb + toonConfig.toon[toonIndex].rgb, vec3(1.0));

    bool wireframe = (pc.drawFlags & 1u) != 0u;
    bool translucentPass = (pc.drawFlags & 2u) != 0u;
    if (wireframe)
        color.a = 1.0;

    uint alpha5 = uint(clamp(color.a, 0.0, 1.0) * 31.0 + 0.5);
    uint alphaRef = (pc.drawFlags >> 8u) & 0x1Fu;
    if (alpha5 <= alphaRef)
        discard;

    if (translucentPass)
    {
        if (alpha5 == 0u || alpha5 == 31u)
            discard;
    }
    else if (alpha5 != 31u)
    {
        discard;
    }

    outColor = color;
    if (translucentPass)
        outAttr = vec4(
            0.0,
            0.0,
            float((fPolygonAttr >> 15u) & 0x1u),
            1.0);
    else
        outAttr = vec4(
            float((fPolygonAttr >> 24u) & 0x3Fu) / 63.0,
            0.0,
            float((fPolygonAttr >> 15u) & 0x1u),
            1.0);
    gl_FragDepth = pc.wBuffer != 0u ? fDepth : gl_FragCoord.z;
}
