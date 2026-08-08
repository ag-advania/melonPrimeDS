#version 450

// Swapchain presentation for the Vulkan compositor's composed atlas.
//
// The composition itself happens once per emulated frame in
// MelonPrimeVulkanCompositorShader.comp. This shader only places the finished
// atlas on the swapchain, optionally through a scaling filter, and draws the
// Custom HUD overlay and the radar on top. It deliberately has no access to the
// structured planes or to the 3D image: a second composition path here would be
// a second place for the DS composition rules to drift.

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 0, binding = 7, std430) readonly buffer OverlayBuffer
{
    uint overlayPixels[];
};

layout(push_constant) uniform PresenterPushConstants
{
    uint drawMode;
    uint scale;
    uint filtering;
    uint overlayWidth;
    uint overlayHeight;
    float radarOpacity;
    uint radarSourceCenterY;
    uint radarSourceRadius;
} pushConstants;

const uint kFilterXbr2 = 2u;
const uint kFilterHq2x = 3u;
const uint kFilterHq4x = 4u;
const uint kFilterQuilez = 5u;
const uint kFilterLcd = 6u;
const uint kFilterScanlines = 7u;
const uint kRadarPaletteSize = 15u;
const uint kRadarPalette[kRadarPaletteSize] = uint[](
    0xC0F868u, 0xF8A8A8u, 0xE03030u,
    0xA0A0A0u, 0xC8C8C8u, 0x909090u,
    0xF88010u, 0xF8D0A0u, 0xD86800u,
    0x88E008u, 0xC8F880u, 0x68B800u,
    0x1098C8u, 0x28D8F8u, 0xA8A8A8u);

layout(location = 0) in vec2 fragUv;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

vec2 compositeTexelSize()
{
    uint safeScale = max(pushConstants.scale, 1u);
    return vec2(
        1.0 / float(256u * safeScale),
        1.0 / float((384u + 2u) * safeScale)
    );
}

vec2 compositeDsTexelSize()
{
    return compositeTexelSize() * float(max(pushConstants.scale, 1u));
}

vec4 screenUvBounds(bool topScreen)
{
    float gapUv = 1.0 / 386.0;
    float minY = topScreen ? 0.0 : (0.5 + gapUv);
    float maxY = topScreen ? (0.5 - gapUv) : 1.0;
    return vec4(0.0, minY, 1.0, maxY);
}
vec2 clampCompositeUvToScreen(vec2 uv, bool topScreen)
{
    vec2 texel = compositeTexelSize();
    vec4 bounds = screenUvBounds(topScreen);
    return vec2(
        clamp(uv.x, texel.x * 0.5, 1.0 - texel.x * 0.5),
        clamp(uv.y, bounds.y + (texel.y * 0.5), bounds.w - (texel.y * 0.5))
    );
}

vec3 sampleCompositeRgb(vec2 uv, bool topScreen)
{
    return texture(uTexture, clampCompositeUvToScreen(uv, topScreen)).bgr;
}

vec2 screenLocalCoord(vec2 uv, bool topScreen)
{
    vec4 bounds = screenUvBounds(topScreen);
    vec2 clamped = clampCompositeUvToScreen(uv, topScreen);
    return vec2(clamped.x, clamp((clamped.y - bounds.y) / max(bounds.w - bounds.y, 0.0001), 0.0, 1.0));
}

vec2 uvFromScreenLocal(vec2 local, bool topScreen)
{
    vec4 bounds = screenUvBounds(topScreen);
    return vec2(clamp(local.x, 0.0, 1.0), mix(bounds.y, bounds.w, clamp(local.y, 0.0, 1.0)));
}

vec2 uvFromScreenTexel(vec2 texelCoord, bool topScreen)
{
    return uvFromScreenLocal((texelCoord + vec2(0.5)) / vec2(256.0, 192.0), topScreen);
}

vec3 filterQuilez(vec2 uv, bool topScreen)
{
    vec2 size = vec2(256.0, 192.0);
    vec2 local = screenLocalCoord(uv, topScreen);
    vec2 p = (local * size) + vec2(0.5);
    vec2 i = floor(p);
    vec2 f = p - i;
    f = f * f * f * ((f * ((f * 6.0) - vec2(15.0))) + vec2(10.0));
    vec2 filteredLocal = (i + f - vec2(0.5)) / size;
    return sampleCompositeRgb(uvFromScreenLocal(filteredLocal, topScreen), topScreen);
}

vec3 filterScanlines(vec2 uv, bool topScreen)
{
    vec3 color = sampleCompositeRgb(uv, topScreen);
    vec2 local = screenLocalCoord(uv, topScreen);
    vec2 omega = vec2(3.1415 * 256.0, 2.0 * 3.1415 * 192.0);
    const float baseBrightness = 0.95;
    const vec2 sineComp = vec2(0.05, 0.15);
    return clamp(color * (baseBrightness + dot(sineComp * sin(local * omega), vec2(1.0))), 0.0, 1.0);
}

vec3 filterLcd(vec2 uv, bool topScreen)
{
    vec3 color = sampleCompositeRgb(uv, topScreen);
    vec2 local = screenLocalCoord(uv, topScreen);
    vec2 angle = local * (3.141592654 * 2.0 * vec2(256.0, 192.0));
    const float brightenScanlines = 16.0;
    const float brightenLcd = 4.0;
    const vec3 offsets = 3.141592654 * vec3(0.5, 0.5 - (2.0 / 3.0), 0.5 - (4.0 / 3.0));
    float yFactor = (brightenScanlines + sin(angle.y)) / (brightenScanlines + 1.0);
    vec3 xFactors = (brightenLcd + sin(angle.x + offsets)) / (brightenLcd + 1.0);
    return clamp(color * yFactor * xFactors, 0.0, 1.0);
}

vec3 filterXbr2(vec2 uv, bool topScreen)
{
    vec2 texel = compositeDsTexelSize();
    vec2 local = screenLocalCoord(uv, topScreen);
    vec2 fp = fract(local * vec2(256.0, 192.0));
    vec2 g1 = vec2(0.0, -texel.y) * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0)
        + vec2(-texel.x, 0.0) * (step(0.5, fp.x) - step(0.5, fp.y));
    vec2 g2 = vec2(0.0, -texel.y) * (step(0.5, fp.y) - step(0.5, fp.x))
        + vec2(-texel.x, 0.0) * (step(0.5, fp.x) + step(0.5, fp.y) - 1.0);
    vec3 c = sampleCompositeRgb(uv + g1 - g2, topScreen);
    vec3 e = sampleCompositeRgb(uv, topScreen);
    vec3 f = sampleCompositeRgb(uv - g2, topScreen);
    vec3 h = sampleCompositeRgb(uv - g1, topScreen);
    vec3 i = sampleCompositeRgb(uv - g1 - g2, topScreen);
    float de = length(e - f) + length(e - h);
    float edge = step(0.015, de) * step(length(h - f), 0.015) * step(0.015, length(h - e));
    vec3 blended = mix(e, mix(f, h, 0.5), 0.5);
    return mix(e, blended, edge * step(length(e - c), length(e - i) + 0.0001));
}

vec3 filterHq2x(vec2 uv, bool topScreen)
{
    vec2 dg1 = 0.5 * compositeDsTexelSize();
    vec2 dg2 = vec2(-dg1.x, dg1.y);
    vec2 dx = vec2(dg1.x, 0.0);
    vec2 dy = vec2(0.0, dg1.y);
    vec3 c00 = sampleCompositeRgb(uv - dg1, topScreen);
    vec3 c10 = sampleCompositeRgb(uv - dy, topScreen);
    vec3 c20 = sampleCompositeRgb(uv - dg2, topScreen);
    vec3 c01 = sampleCompositeRgb(uv - dx, topScreen);
    vec3 c11 = sampleCompositeRgb(uv, topScreen);
    vec3 c21 = sampleCompositeRgb(uv + dx, topScreen);
    vec3 c02 = sampleCompositeRgb(uv + dg2, topScreen);
    vec3 c12 = sampleCompositeRgb(uv + dy, topScreen);
    vec3 c22 = sampleCompositeRgb(uv + dg1, topScreen);
    vec3 dt = vec3(1.0);
    const float mx = 0.325;
    const float k = -0.250;
    const float maxW = 0.25;
    const float minW = -0.05;
    const float lumAdd = 0.25;
    float md1 = dot(abs(c00 - c22), dt);
    float md2 = dot(abs(c02 - c20), dt);
    float w1 = dot(abs(c22 - c11), dt) * md2;
    float w2 = dot(abs(c02 - c11), dt) * md1;
    float w3 = dot(abs(c00 - c11), dt) * md2;
    float w4 = dot(abs(c20 - c11), dt) * md1;
    float t1 = w1 + w3;
    float t2 = w2 + w4;
    float ww = max(t1, t2) + 0.001;
    c11 = (w1 * c00 + w2 * c20 + w3 * c22 + w4 * c02 + ww * c11) / (t1 + t2 + ww);
    float lc1 = k / (0.12 * dot(c10 + c12 + c11, dt) + lumAdd);
    float lc2 = k / (0.12 * dot(c01 + c21 + c11, dt) + lumAdd);
    w1 = clamp(lc1 * dot(abs(c11 - c10), dt) + mx, minW, maxW);
    w2 = clamp(lc2 * dot(abs(c11 - c21), dt) + mx, minW, maxW);
    w3 = clamp(lc1 * dot(abs(c11 - c12), dt) + mx, minW, maxW);
    w4 = clamp(lc2 * dot(abs(c11 - c01), dt) + mx, minW, maxW);
    return clamp(w1 * c10 + w2 * c21 + w3 * c12 + w4 * c01 + (1.0 - w1 - w2 - w3 - w4) * c11, 0.0, 1.0);
}

vec3 filterHq4x(vec2 uv, bool topScreen)
{
    vec2 dg1 = 0.5 * compositeDsTexelSize();
    vec2 dg2 = vec2(-dg1.x, dg1.y);
    vec2 sd1 = dg1 * 0.5;
    vec2 sd2 = dg2 * 0.5;
    vec2 ddx = vec2(dg1.x, 0.0);
    vec2 ddy = vec2(0.0, dg1.y);
    vec3 c = sampleCompositeRgb(uv, topScreen);
    vec3 i1 = sampleCompositeRgb(uv - sd1, topScreen);
    vec3 i2 = sampleCompositeRgb(uv - sd2, topScreen);
    vec3 i3 = sampleCompositeRgb(uv + sd1, topScreen);
    vec3 i4 = sampleCompositeRgb(uv + sd2, topScreen);
    vec3 o1 = sampleCompositeRgb(uv - dg1, topScreen);
    vec3 o3 = sampleCompositeRgb(uv + dg1, topScreen);
    vec3 o2 = sampleCompositeRgb(uv - dg2, topScreen);
    vec3 o4 = sampleCompositeRgb(uv + dg2, topScreen);
    vec3 s1 = sampleCompositeRgb(uv - ddy, topScreen);
    vec3 s2 = sampleCompositeRgb(uv + ddx, topScreen);
    vec3 s3 = sampleCompositeRgb(uv + ddy, topScreen);
    vec3 s4 = sampleCompositeRgb(uv - ddx, topScreen);
    vec3 dt = vec3(1.0);
    const float mx = 1.00;
    const float k = -1.10;
    const float maxW = 0.75;
    const float minW = 0.03;
    const float lumAdd = 0.33;
    float ko1 = dot(abs(o1 - c), dt);
    float ko2 = dot(abs(o2 - c), dt);
    float ko3 = dot(abs(o3 - c), dt);
    float ko4 = dot(abs(o4 - c), dt);
    float k1 = min(dot(abs(i1 - i3), dt), max(ko1, ko3));
    float k2 = min(dot(abs(i2 - i4), dt), max(ko2, ko4));
    float w1 = k2;
    if (ko3 < ko1)
        w1 *= ko3 / max(ko1, 0.000001);
    float w2 = k1;
    if (ko4 < ko2)
        w2 *= ko4 / max(ko2, 0.000001);
    float w3 = k2;
    if (ko1 < ko3)
        w3 *= ko1 / max(ko3, 0.000001);
    float w4 = k1;
    if (ko2 < ko4)
        w4 *= ko2 / max(ko4, 0.000001);
    c = (w1 * o1 + w2 * o2 + w3 * o3 + w4 * o4 + 0.001 * c) / (w1 + w2 + w3 + w4 + 0.001);
    w1 = k * dot(abs(i1 - c) + abs(i3 - c), dt) / (0.125 * dot(i1 + i3, dt) + lumAdd);
    w2 = k * dot(abs(i2 - c) + abs(i4 - c), dt) / (0.125 * dot(i2 + i4, dt) + lumAdd);
    w3 = k * dot(abs(s1 - c) + abs(s3 - c), dt) / (0.125 * dot(s1 + s3, dt) + lumAdd);
    w4 = k * dot(abs(s2 - c) + abs(s4 - c), dt) / (0.125 * dot(s2 + s4, dt) + lumAdd);
    w1 = clamp(w1 + mx, minW, maxW);
    w2 = clamp(w2 + mx, minW, maxW);
    w3 = clamp(w3 + mx, minW, maxW);
    w4 = clamp(w4 + mx, minW, maxW);
    return clamp((w1 * (i1 + i3) + w2 * (i2 + i4) + w3 * (s1 + s3) + w4 * (s2 + s4) + c) / (2.0 * (w1 + w2 + w3 + w4) + 1.0), 0.0, 1.0);
}

vec3 applyCompositePostFilter(vec2 uv, bool topScreen)
{
    if (pushConstants.filtering == kFilterQuilez)
        return filterQuilez(uv, topScreen);
    if (pushConstants.filtering == kFilterXbr2)
        return filterXbr2(uv, topScreen);
    if (pushConstants.filtering == kFilterHq2x)
        return filterHq2x(uv, topScreen);
    if (pushConstants.filtering == kFilterHq4x)
        return filterHq4x(uv, topScreen);
    if (pushConstants.filtering == kFilterLcd)
        return filterLcd(uv, topScreen);
    if (pushConstants.filtering == kFilterScanlines)
        return filterScanlines(uv, topScreen);
    return sampleCompositeRgb(uv, topScreen);
}

vec3 sampleRadarRgb(vec2 uv)
{
    ivec2 textureExtent = textureSize(uTexture, 0);
    vec2 texelPosition = uv * vec2(textureExtent) - vec2(0.5);
    ivec2 base = ivec2(floor(texelPosition));
    vec2 fraction = fract(texelPosition);
    ivec2 maximum = textureExtent - ivec2(1);
    ivec2 p00 = clamp(base, ivec2(0), maximum);
    ivec2 p10 = clamp(base + ivec2(1, 0), ivec2(0), maximum);
    ivec2 p01 = clamp(base + ivec2(0, 1), ivec2(0), maximum);
    ivec2 p11 = clamp(base + ivec2(1, 1), ivec2(0), maximum);
    vec3 top = mix(texelFetch(uTexture, p00, 0).bgr,
        texelFetch(uTexture, p10, 0).bgr, fraction.x);
    vec3 bottom = mix(texelFetch(uTexture, p01, 0).bgr,
        texelFetch(uTexture, p11, 0).bgr, fraction.x);
    return mix(top, bottom, fraction.y);
}

bool isRadarPaletteColor(vec3 rgb)
{
    // Vulkan composition expands the DS 6-bit channels to 8-bit. Quantize
    // back to the 5-bit-aligned values used by the established OpenGL palette.
    uvec3 color = uvec3(round(rgb * 255.0)) & uvec3(0xf8u);
    uint packed = (color.r << 16u) | (color.g << 8u) | color.b;
    for (uint index = 0u; index < kRadarPaletteSize; ++index)
    {
        if (packed == kRadarPalette[index])
            return true;
    }
    return false;
}


void main()
{
    // Radar: sampled out of the composed atlas and masked to the game's own
    // radar palette, so it follows whatever the compositor produced.
    if (pushConstants.drawMode == 8u)
    {
        vec2 centered = fragUv * 2.0 - 1.0;
        float distanceSquared = dot(centered, centered);
        if (distanceSquared > 1.0)
            discard;
        float sourceRadius = float(pushConstants.radarSourceRadius);
        vec2 sourceUv = vec2(
            (128.0 + centered.x * sourceRadius) / 256.0,
            (194.0 + float(pushConstants.radarSourceCenterY)
                + centered.y * sourceRadius) / 386.0);
        vec3 rgb = sampleRadarRgb(sourceUv);
        if (!isRadarPaletteColor(rgb))
            discard;
        float alpha = pushConstants.radarOpacity
            * (1.0 - smoothstep(0.95, 1.0, distanceSquared));
        outColor = vec4(rgb * alpha, alpha);
        return;
    }

    // Custom HUD / OSD overlay, uploaded as premultiplied BGRA.
    if (pushConstants.drawMode == 7u)
    {
        uint x = min(uint(fragUv.x * float(pushConstants.overlayWidth)), pushConstants.overlayWidth - 1u);
        uint y = min(uint(fragUv.y * float(pushConstants.overlayHeight)), pushConstants.overlayHeight - 1u);
        uint pixel = overlayPixels[y * pushConstants.overlayWidth + x];
        outColor = vec4(
            float((pixel >> 16u) & 0xffu),
            float((pixel >> 8u) & 0xffu),
            float(pixel & 0xffu),
            float((pixel >> 24u) & 0xffu)) / 255.0;
        return;
    }

    // Post-process scaling filters, applied per screen so they never sample
    // across the seam between the two LCDs.
    if (pushConstants.drawMode == 4u || pushConstants.drawMode == 5u)
    {
        bool topScreen = pushConstants.drawMode == 4u;
        outColor = vec4(applyCompositePostFilter(fragUv, topScreen), fragAlpha);
        return;
    }

    // Default screen blit. The compositor stores BGR in the RGBA slots.
    vec4 sampledColor = texture(uTexture, fragUv);
    outColor = vec4(sampledColor.bgr, fragAlpha);
}
