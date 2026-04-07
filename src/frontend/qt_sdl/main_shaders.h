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
    bool match =
        // 529cd6, 6582b1 = nuxus radar blue
        // c == vec3(104.0, 224.0, 40.0)  || // 68E028 - green Samus radar?
        // c == vec3(248.0, 248.0, 88.0)  || // F8F858 - yellow, kanden radar
        // c == vec3(248.0, 112.0, 56.0)  || // F87038 - orange Spire radar?
        // c == vec3(224.0, 16.0,  24.0)  || // E01018 - red trace radar?
        // c == vec3(80.0,  152.0, 208.0) || // 5098D0 - blue Noxus radar
        // c == vec3(208.0, 240.0, 160.0) || // D0F0A0 - pale green Sylux radar?
        // c == vec3(208.0, 152.0, 56.0)  || // D09838 - amber Weavel radar?

        c == vec3(192.0, 248.0, 104.0) || // C0F868 - yellow-green
        c == vec3(248.0, 168.0, 168.0) || // F8A8A8 - pink, node red middle
        c == vec3(224.0, 48.0,  48.0)  || // E03030 - node red outer and center
        // c == vec3(248.0, 248.0, 152.0)  || // F8F898 - center of kanden radar
        c == vec3(160.0, 160.0, 160.0)  || // A0A0A0 - octolith gray top
        c == vec3(200.0, 200.0, 200.0)  || // C8C8C8 - octolith gray center
        c == vec3(144.0, 144.0, 144.0)  || // 909090 - octolith gray bottom
        c == vec3(248.0, 128.0, 16.0)   || // F88010 - octolith orange top
        c == vec3(248.0, 208.0, 160.0)  || // F8D0A0 - octolith orange center
        c == vec3(216.0, 104.0, 0.0)    || // D86800 - octolith orange bottom
        c == vec3(136.0, 224.0, 8.0)    || // 88E008 - octolith green top
        c == vec3(200.0, 248.0, 128.0)  || // C8F880 - octolith green center
        c == vec3(104.0, 184.0, 0.0)    || // 68B800 - octolith green bottom
        c == vec3(16.0,  152.0, 200.0)  || // 1098C8 - node blue outer and center
        c == vec3(40.0,  216.0, 248.0)  || // 28D8F8 - node blue middle
        c == vec3(168.0, 168.0, 168.0);   // A8A8A8 - node gray

    if (!match) discard;

    oColor = vec4(pixel.rgb, alpha);
}
)";
#endif

#endif // MAIN_SHADERS_H
