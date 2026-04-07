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
    vec4 pixel = texture(ScreenTex, vec3(srcUV.x, srcUV.y, 1.0));

    // Color filter: keep only exact radar palette colors, discard others
    vec3 c = round(pixel.rgb * 255.0);
    bool match = false;
    for (int i = 0; i < PALETTE_SIZE; i++) {
        if (c == uPalette[i]) { match = true; break; }
    }

    if (!match) discard;

    oColor = vec4(pixel.rgb, alpha);
}
)";
#endif

#endif // MAIN_SHADERS_H
