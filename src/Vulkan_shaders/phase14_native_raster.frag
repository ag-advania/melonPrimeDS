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
    uint texParam;
    uint clearAttr;
} pc;

layout(location = 0) in vec4 fColor;
layout(location = 1) in vec2 fTexcoord;
layout(location = 2) noperspective in float fDepthLinear;
layout(location = 3) smooth in float fDepthPerspective;
layout(location = 4) flat in uint fPolygonAttr;
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

const float LINEAR_TEXEL_COORD_BIAS = 1.0 / 8.0;

bool usesDsPixelCenteredTranslucentPaletteUi()
{
    uint textureFormat = (pc.texParam >> 26u) & 0x7u;
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    uint blendMode = (fPolygonAttr >> 4u) & 0x3u;
    bool linearW = (pc.drawFlags & 8u) != 0u;
    bool color0Transparent = (pc.texParam & (1u << 29u)) != 0u;
    bool depthWriteDisabled = (fPolygonAttr & (1u << 11u)) == 0u;
    bool clearAlphaZero = ((pc.clearAttr >> 16u) & 0x1Fu) == 0u;
    bool alphaBlendEnabled = (pc.renderDispCnt & (1u << 3u)) != 0u;
    bool repeatS = (pc.texParam & (1u << 16u)) != 0u;
    bool repeatT = (pc.texParam & (1u << 17u)) != 0u;
    bool mirrorS = (pc.texParam & (1u << 18u)) != 0u;
    bool mirrorT = (pc.texParam & (1u << 19u)) != 0u;
    bool menuTexturePage = (pc.texParam & 0xFFFFu) == 0xA3A0u;
    return (pc.drawFlags & 2u) != 0u && pc.textured != 0u &&
        (pc.renderDispCnt & 1u) != 0u && linearW &&
        textureFormat == 3u && color0Transparent && menuTexturePage &&
        depthWriteDisabled && clearAlphaZero && alphaBlendEnabled &&
        blendMode == 0u && polygonAlpha > 0u && polygonAlpha < 31u &&
        !repeatS && !repeatT && !mirrorS && !mirrorT;
}

bool usesPaletteUiAlphaHoleFill()
{
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    return usesDsPixelCenteredTranslucentPaletteUi() && polygonAlpha >= 21u;
}

bool usesCompactOpaqueDepthWritePaletteUi()
{
    uint textureFormat = (pc.texParam >> 26u) & 0x7u;
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    uint blendMode = (fPolygonAttr >> 4u) & 0x3u;
    bool linearW = (pc.drawFlags & 8u) != 0u;
    bool color0Transparent = (pc.texParam & (1u << 29u)) != 0u;
    bool depthWriteEnabled = (fPolygonAttr & (1u << 11u)) != 0u;
    bool clearAlphaZero = ((pc.clearAttr >> 16u) & 0x1Fu) == 0u;
    bool repeatS = (pc.texParam & (1u << 16u)) != 0u;
    bool repeatT = (pc.texParam & (1u << 17u)) != 0u;
    bool mirrorS = (pc.texParam & (1u << 18u)) != 0u;
    bool mirrorT = (pc.texParam & (1u << 19u)) != 0u;
    bool statusGlyphTexturePage = (pc.texParam & 0xFFFFu) == 0x05C0u;
    return (pc.drawFlags & 2u) == 0u && pc.textured != 0u && linearW &&
        textureFormat == 3u && color0Transparent && statusGlyphTexturePage &&
        depthWriteEnabled && clearAlphaZero && blendMode == 0u &&
        polygonAlpha == 31u && !repeatS && !repeatT && !mirrorS && !mirrorT;
}

bool usesHighresOpaqueRepeatedModelTexture()
{
    uint textureFormat = (pc.texParam >> 26u) & 0x7u;
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    uint blendMode = (fPolygonAttr >> 4u) & 0x3u;
    bool linearW = (pc.drawFlags & 8u) != 0u;
    bool color0Transparent = (pc.texParam & (1u << 29u)) != 0u;
    bool repeatS = (pc.texParam & (1u << 16u)) != 0u;
    bool repeatT = (pc.texParam & (1u << 17u)) != 0u;
    bool mirrorS = (pc.texParam & (1u << 18u)) != 0u;
    bool mirrorT = (pc.texParam & (1u << 19u)) != 0u;
    return (pc.drawFlags & 2u) == 0u && pc.textured != 0u && linearW &&
        (textureFormat == 4u || textureFormat == 5u) &&
        !color0Transparent && polygonAlpha == 31u && blendMode == 0u &&
        (repeatS || repeatT || mirrorS || mirrorT);
}

bool usesHighresLinearTextBand(ivec2 textureExtent)
{
    uint textureFormat = (pc.texParam >> 26u) & 0x7u;
    uint polygonAlpha = (fPolygonAttr >> 16u) & 0x1Fu;
    uint blendMode = (fPolygonAttr >> 4u) & 0x3u;
    bool linearW = (pc.drawFlags & 8u) != 0u;
    bool color0Transparent = (pc.texParam & (1u << 29u)) != 0u;
    bool depthWriteEnabled = (fPolygonAttr & (1u << 11u)) != 0u;
    bool depthWriteDisabled = !depthWriteEnabled;
    bool repeatS = (pc.texParam & (1u << 16u)) != 0u;
    bool repeatT = (pc.texParam & (1u << 17u)) != 0u;
    bool mirrorS = (pc.texParam & (1u << 18u)) != 0u;
    bool mirrorT = (pc.texParam & (1u << 19u)) != 0u;
    bool observedTranslucentTextPage =
        (pc.texParam == 0x79df2000u &&
         all(equal(textureExtent, ivec2(256, 64)))) ||
        (pc.texParam == 0x7a5f3000u &&
         all(equal(textureExtent, ivec2(256, 128)))) ||
        (pc.texParam == 0x79df4800u &&
         all(equal(textureExtent, ivec2(256, 64))));
    bool observedOpaqueTextPage =
        pc.texParam == 0x71df2800u &&
        all(equal(textureExtent, ivec2(256, 64)));
    bool commonTextBand = pc.textured != 0u && linearW &&
        color0Transparent && blendMode == 0u &&
        repeatS && repeatT && mirrorS && mirrorT;
    return commonTextBand &&
        (((pc.drawFlags & 2u) != 0u && textureFormat == 6u &&
          depthWriteDisabled && polygonAlpha == 30u &&
          observedTranslucentTextPage) ||
         ((pc.drawFlags & 2u) == 0u && textureFormat == 4u &&
          depthWriteEnabled && polygonAlpha == 31u && observedOpaqueTextPage));
}

vec2 dsPixelCenterDelta()
{
    vec2 renderScale = max(
        pc.screenSize / vec2(256.0, 192.0), vec2(1.0));
    vec2 subpixelOffset = mod(gl_FragCoord.xy - vec2(0.5), renderScale);
    vec2 centerOffset = max((renderScale - vec2(1.0)) * 0.5, vec2(0.0));
    return centerOffset - subpixelOffset;
}

int wrapTexelCoord(int coord, int size, bool repeat, bool mirror)
{
    if (size <= 0)
        return 0;
    if (repeat)
    {
        if (mirror)
        {
            if ((coord & size) != 0)
                coord = (size - 1) - (coord & (size - 1));
            else
                coord &= size - 1;
        }
        else
        {
            coord &= size - 1;
        }
    }
    else
    {
        coord = clamp(coord, 0, size - 1);
    }
    return coord;
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
    ivec2 textureExtent = max(textureSize(nativeTexture, 0).xy, ivec2(1));
    bool repeatS = (pc.texParam & (1u << 16u)) != 0u;
    bool repeatT = (pc.texParam & (1u << 17u)) != 0u;
    bool mirrorS = (pc.texParam & (1u << 18u)) != 0u;
    bool mirrorT = (pc.texParam & (1u << 19u)) != 0u;
    vec2 texcoord = fTexcoord;
    if (usesDsPixelCenteredTranslucentPaletteUi() ||
        usesCompactOpaqueDepthWritePaletteUi())
    {
        vec2 centerDelta = dsPixelCenterDelta();
        texcoord += dFdx(fTexcoord) * centerDelta.x +
            dFdy(fTexcoord) * centerDelta.y;
    }
    else if ((pc.drawFlags & 8u) != 0u &&
             (repeatS || repeatT || mirrorS || mirrorT) &&
             !usesHighresOpaqueRepeatedModelTexture() &&
             !usesHighresLinearTextBand(textureExtent))
    {
        vec2 renderScale = max(
            pc.screenSize / vec2(256.0, 192.0), vec2(1.0));
        vec2 subpixelOffset = mod(
            gl_FragCoord.xy - vec2(0.5), renderScale);
        texcoord += dFdx(fTexcoord) * -subpixelOffset.x +
            dFdy(fTexcoord) * -subpixelOffset.y;
        texcoord -= vec2(LINEAR_TEXEL_COORD_BIAS);
    }

    int sampleS = wrapTexelCoord(
        int(floor(texcoord.x)), textureExtent.x, repeatS, mirrorS);
    int sampleT = wrapTexelCoord(
        int(floor(texcoord.y)), textureExtent.y, repeatT, mirrorT);
    uvec4 texel = texelFetch(
        nativeTexture, ivec3(sampleS, sampleT, 0), 0);
    if (usesPaletteUiAlphaHoleFill() && (texel.a & 0x1Fu) == 0u)
    {
        int leftS = wrapTexelCoord(
            sampleS - 1, textureExtent.x, repeatS, mirrorS);
        int rightS = wrapTexelCoord(
            sampleS + 1, textureExtent.x, repeatS, mirrorS);
        int upT = wrapTexelCoord(
            sampleT - 1, textureExtent.y, repeatT, mirrorT);
        int downT = wrapTexelCoord(
            sampleT + 1, textureExtent.y, repeatT, mirrorT);
        uvec4 leftTexel = texelFetch(
            nativeTexture, ivec3(leftS, sampleT, 0), 0);
        uvec4 rightTexel = texelFetch(
            nativeTexture, ivec3(rightS, sampleT, 0), 0);
        uvec4 upTexel = texelFetch(
            nativeTexture, ivec3(sampleS, upT, 0), 0);
        uvec4 downTexel = texelFetch(
            nativeTexture, ivec3(sampleS, downT, 0), 0);
        if ((leftTexel.a & 0x1Fu) != 0u)
            texel = leftTexel;
        else if ((rightTexel.a & 0x1Fu) != 0u)
            texel = rightTexel;
        else if ((upTexel.a & 0x1Fu) != 0u)
            texel = upTexel;
        else if ((downTexel.a & 0x1Fu) != 0u)
            texel = downTexel;
    }
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

vec4 encodeColorDsTranslucentBlendAlpha(Color6A5 color)
{
    return vec4(
        float(clamp6(color.r)) * (1.0 / 63.0),
        float(clamp6(color.g)) * (1.0 / 63.0),
        float(clamp6(color.b)) * (1.0 / 63.0),
        float(clamp(color.a + 1, 0, 32)) * (1.0 / 32.0));
}

void main()
{
    float fragmentDepth = pc.wBuffer != 0u
        ? fDepthPerspective
        : fDepthLinear;
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
    {
        color.a = 31;
        // Sapphire preserves otherwise invisible alpha-zero helper geometry as
        // white boundary lines. The CPU enables this only for its exact
        // untextured, mode-zero, flag-free polygon case.
        if ((pc.drawFlags & 16u) != 0u)
        {
            color.r = 63;
            color.g = 63;
            color.b = 63;
        }
    }

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
        outDepth = fragmentDepth;
        gl_FragDepth = fragmentDepth;
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

    outColor = translucentPass && usesPaletteUiAlphaHoleFill()
        ? encodeColorDsTranslucentBlendAlpha(color)
        : encodeColor(color);
    if (translucentPass)
        // Attribute alpha is also the hybrid presenter's ownership class.
        // The pipeline MAX-blends this with destination alpha, so effects over
        // native opaque geometry retain ownership 1.0 while effects over the
        // clear plane remain provisional and preserve Software composition.
        outAttr = vec4(0.0, 0.0, 0.0, 0.25);
    else
        outAttr = vec4(
            float((fPolygonAttr >> 24u) & 0x3Fu) / 63.0,
            0.0,
            float((fPolygonAttr >> 15u) & 0x1u),
            1.0);
    outDepth = fragmentDepth;
    gl_FragDepth = outDepth;
}
