#version 450

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
layout(set = 0, binding = 0) uniform usampler2DArray nativeTexture;
layout(std140, set = 0, binding = 1) uniform NativeRasterToonTable
{
    uvec4 toon[32];
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
layout(location = 2) out float outDepth;

struct Color6A5
{
    int r;
    int g;
    int b;
    int a;
};

int clamp6(int value)
{
    return clamp(value, 0, 63);
}

int clamp5(int value)
{
    return clamp(value, 0, 31);
}

Color6A5 unpackToonColor(uint shadeIndex)
{
    uvec4 packed = toonConfig.toon[min(shadeIndex, 31u)];
    Color6A5 color;
    color.r = int(packed.r & 0x3Fu);
    color.g = int(packed.g & 0x3Fu);
    color.b = int(packed.b & 0x3Fu);
    color.a = 31;
    return color;
}

Color6A5 sampleTexture()
{
    uvec4 texel = texture(nativeTexture, vec3(fTexcoord, 0.0));
    Color6A5 color;
    color.r = int(texel.r & 0x3Fu);
    color.g = int(texel.g & 0x3Fu);
    color.b = int(texel.b & 0x3Fu);
    color.a = int(texel.a & 0x1Fu);
    return color;
}

vec4 encodeColor(Color6A5 color)
{
    return vec4(
        float(clamp6(color.r)) * (1.0 / 63.0),
        float(clamp6(color.g)) * (1.0 / 63.0),
        float(clamp6(color.b)) * (1.0 / 63.0),
        float(clamp5(color.a)) * (1.0 / 31.0));
}

void main()
{
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    Color6A5 color;
    color.r = clamp6(int(clamp(fColor.r * 63.0 + 0.5, 0.0, 63.0)));
    color.g = clamp6(int(clamp(fColor.g * 63.0 + 0.5, 0.0, 63.0)));
    color.b = clamp6(int(clamp(fColor.b * 63.0 + 0.5, 0.0, 63.0)));
    color.a = int(polygonAlpha);

    int highlightShade = color.r;
    bool highlight = (pc.renderDispCnt & (1u << 1u)) != 0u;
    if (pc.textureMode == 2u)
    {
        if (highlight)
        {
            color.g = color.r;
            color.b = color.r;
            highlightShade = color.r;
        }
        else
        {
            Color6A5 toonColor = unpackToonColor(uint(clamp(color.r >> 1, 0, 31)));
            color.r = toonColor.r;
            color.g = toonColor.g;
            color.b = toonColor.b;
        }
    }

    bool textureMapsEnabled = (pc.renderDispCnt & (1u << 0u)) != 0u;
    if (textureMapsEnabled && pc.textured != 0u)
    {
        Color6A5 texel = sampleTexture();
        if (pc.textureMode == 1u)
        {
            if (texel.a >= 31)
            {
                color.r = texel.r;
                color.g = texel.g;
                color.b = texel.b;
            }
            else if (texel.a > 0)
            {
                color.r = clamp6(((texel.r * texel.a) +
                    (color.r * (31 - texel.a))) >> 5);
                color.g = clamp6(((texel.g * texel.a) +
                    (color.g * (31 - texel.a))) >> 5);
                color.b = clamp6(((texel.b * texel.a) +
                    (color.b * (31 - texel.a))) >> 5);
            }
        }
        else
        {
            color.r = clamp6((((texel.r + 1) * (color.r + 1)) - 1) >> 6);
            color.g = clamp6((((texel.g + 1) * (color.g + 1)) - 1) >> 6);
            color.b = clamp6((((texel.b + 1) * (color.b + 1)) - 1) >> 6);
            color.a = clamp5((((texel.a + 1) * (color.a + 1)) - 1) >> 5);
        }
    }

    if (pc.textureMode == 2u && highlight)
    {
        Color6A5 toonColor = unpackToonColor(
            uint(clamp(highlightShade >> 1, 0, 31)));
        color.r = clamp6(color.r + toonColor.r);
        color.g = clamp6(color.g + toonColor.g);
        color.b = clamp6(color.b + toonColor.b);
    }

    bool wireframe = (pc.drawFlags & 1u) != 0u;
    bool translucentPass = (pc.drawFlags & 2u) != 0u;
    if (wireframe)
        color.a = 31;

    uint alpha5 = uint(clamp5(color.a));
    uint alphaRef = (pc.drawFlags >> 8u) & 0x1Fu;
    if (alpha5 <= alphaRef)
        discard;

    bool edgeMarkPass = (pc.drawFlags & 4u) != 0u;
    if (edgeMarkPass)
    {
        if (alpha5 != 31u)
            discard;
        outColor = vec4(0.0);
        outAttr = vec4(0.0, 1.0, 0.0, 1.0);
        outDepth = pc.wBuffer != 0u ? fDepth : gl_FragCoord.z;
        return;
    }

    if (translucentPass)
    {
        if (alpha5 == 0u || alpha5 == 31u)
            discard;
    }
    else if (alpha5 != 31u)
    {
        discard;
    }

    outColor = encodeColor(color);
    if (translucentPass)
        outAttr = vec4(0.0, 0.0, 0.0, 1.0);
    else
        outAttr = vec4(
            float((fPolygonAttr >> 24u) & 0x3Fu) / 63.0,
            0.0,
            float((fPolygonAttr >> 15u) & 0x1u),
            1.0);
    outDepth = pc.wBuffer != 0u ? fDepth : gl_FragCoord.z;
    gl_FragDepth = outDepth;
}
