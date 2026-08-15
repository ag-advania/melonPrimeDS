/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef MAIN_SHADERS_H
#define MAIN_SHADERS_H

const char* kScreenVS = R"(#version 140

uniform vec2 uScreenSize;
uniform mat2x3 uTransform;

in vec2 vPosition;
in vec3 vTexcoord;

smooth out vec3 fTexcoord;

void main()
{
    vec4 fpos;

    fpos.xy = vec3(vPosition, 1.0) * uTransform;

    fpos.xy = ((fpos.xy * 2.0) / uScreenSize) - 1.0;
    fpos.y *= -1;
    fpos.z = 0.0;
    fpos.w = 1.0;

    gl_Position = fpos;
    fTexcoord = vTexcoord;
}
)";

const char* kScreenFS = R"(#version 140

uniform sampler2DArray ScreenTex;

smooth in vec3 fTexcoord;

out vec4 oColor;

void main()
{
    vec4 pixel = texture(ScreenTex, fTexcoord);

    oColor = vec4(pixel.rgb, 1.0);
}
)";

#ifdef MELONPRIME_CUSTOM_HUD
const char* kBtmOverlayVS = R"(#version 140

uniform vec2 uScreenSize;

in vec2 vPosition;
in vec2 vTexcoord;

smooth out vec2 fTexcoord;

void main()
{
    vec4 fpos;
    fpos.xy = ((vPosition * 2.0) / uScreenSize) - 1.0;
    fpos.y *= -1;
    fpos.z = 0.0;
    fpos.w = 1.0;
    gl_Position = fpos;
    fTexcoord = vTexcoord;
}
)";

const char* kBtmOverlayFS = R"(#version 140

uniform sampler2DArray ScreenTex;
uniform float uOpacity;
uniform vec2 uSrcCenter;   // center of source region in normalized [0,1] coords
uniform float uSrcRadius;  // radius of source region in normalized coords (relative to width)

smooth in vec2 fTexcoord;

out vec4 oColor;

// OPT-SH1: Radar palette as uniform array + loop instead of 15 chained || comparisons.
// GPU compilers unroll small constant-bound loops, enabling SIMD-friendly execution
// without warp/wavefront divergence from deeply nested short-circuit evaluation.
// Palette data is uploaded once at init from Screen.cpp (see initOpenGL OPT-SH1 block).
// Hunter-specific radar dot colors are intentionally excluded — see Screen.cpp comments.
const int PALETTE_SIZE = 15;
uniform vec3 uPalette[PALETTE_SIZE];

vec4 radarColorKey(vec4 pixel)
{
    // The software renderer expands the DS 6-bit channels to 8-bit values,
    // which can populate bits [2:0]. Compare in the same 5-bit color-key
    // space as the CPU, Vulkan, DX12, and Metal radar paths. Keep this mask
    // in sync with MelonPrime::kRadarPaletteQuantizationMask.
    uvec3 color = uvec3(round(clamp(pixel.rgb, vec3(0.0), vec3(1.0)) * 255.0));
    color &= uvec3(0xF8u);
    for (int i = 0; i < PALETTE_SIZE; i++) {
        if (all(equal(color, uvec3(uPalette[i]))))
            return vec4(pixel.rgb, 1.0);
    }
    return vec4(0.0);
}

void main()
{
    // Circle clipping: discard pixels outside unit circle
    vec2 centered = fTexcoord * 2.0 - 1.0;
    float dist = dot(centered, centered);
    if (dist > 1.0) discard;

    // Smooth edge antialiasing
    float alpha = uOpacity * (1.0 - smoothstep(0.95, 1.0, dist));

    // Remap texcoords to sample from circular source region
    vec2 srcUV = uSrcCenter + centered * vec2(uSrcRadius, uSrcRadius * (256.0 / 192.0));

    // Color-key each source texel before filtering, matching the CPU path that
    // makes non-palette pixels transparent before QPainter scales the crop.
    // Filtering first shrinks palette pixels at native Software resolution,
    // because their blended edges no longer pass an exact palette comparison.
    ivec2 textureExtent = textureSize(ScreenTex, 0).xy;
    vec2 texelPosition = srcUV * vec2(textureExtent) - vec2(0.5);
    ivec2 texelBase = ivec2(floor(texelPosition));
    vec2 texelWeight = fract(texelPosition);
    ivec2 texelMax = textureExtent - ivec2(1);
    ivec2 texel00 = clamp(texelBase, ivec2(0), texelMax);
    ivec2 texel10 = clamp(texelBase + ivec2(1, 0), ivec2(0), texelMax);
    ivec2 texel01 = clamp(texelBase + ivec2(0, 1), ivec2(0), texelMax);
    ivec2 texel11 = clamp(texelBase + ivec2(1, 1), ivec2(0), texelMax);

    vec4 keyedTop = mix(
        radarColorKey(texelFetch(ScreenTex, ivec3(texel00, 1), 0)),
        radarColorKey(texelFetch(ScreenTex, ivec3(texel10, 1), 0)),
        texelWeight.x);
    vec4 keyedBottom = mix(
        radarColorKey(texelFetch(ScreenTex, ivec3(texel01, 1), 0)),
        radarColorKey(texelFetch(ScreenTex, ivec3(texel11, 1), 0)),
        texelWeight.x);
    vec4 keyedPixel = mix(keyedTop, keyedBottom, texelWeight.y);
    if (keyedPixel.a <= 0.0) discard;

    // keyedPixel.rgb is premultiplied by its interpolated key coverage. Convert
    // back to straight color because this pass uses GL_SRC_ALPHA blending.
    oColor = vec4(keyedPixel.rgb / keyedPixel.a, alpha * keyedPixel.a);
}
)";
#endif

#endif // MAIN_SHADERS_H
