// MelonPrimeDS -- Metal presenter shaders (screen quad, UI overlay quad,
// radar circle-mask sample). Extracted from the kScreenShaderSource /
// kUiShaderSource / kRadarShaderSource NSString literals in
// src/frontend/qt_sdl/MelonPrimeScreenMetal.mm (PR-14: MSL asset/metallib).
// Content is unchanged from the embedded source; only the physical location
// moved so it can be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float3 texcoord [[attribute(1)]];
};
struct VOut {
    float4 position [[position]];
    float3 texcoord;
};
struct ScreenUniforms {
    float m[6];
    float2 screenSize;
    float yFlipSign;
    float _pad;
};
vertex VOut mp_screen_vs(VertexIn in [[stage_in]],
                         constant ScreenUniforms& u [[buffer(1)]]) {
    float2 p = float2(
        u.m[0] * in.position.x + u.m[2] * in.position.y + u.m[4],
        u.m[1] * in.position.x + u.m[3] * in.position.y + u.m[5]);
    p = ((p * 2.0) / u.screenSize) - 1.0;
    p.y *= u.yFlipSign;
    VOut out;
    out.position = float4(p, 0.0, 1.0);
    out.texcoord = in.texcoord;
    return out;
}
fragment float4 mp_screen_fs(VOut in [[stage_in]],
                             texture2d_array<float> tex [[texture(0)]],
                             sampler samp [[sampler(0)]]) {
    float4 c = tex.sample(samp, in.texcoord.xy, uint(in.texcoord.z + 0.5));
    return float4(c.rgb, 1.0);
}

struct UiVertexIn {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
};
struct UiVOut {
    float4 position [[position]];
    float2 texcoord;
};
struct UiUniforms {
    float4 rect;
    float2 screenSize;
    float yFlipSign;
    float _pad;
};
vertex UiVOut mp_ui_vs(UiVertexIn in [[stage_in]],
                        constant UiUniforms& u [[buffer(1)]]) {
    float2 p = u.rect.xy + in.position * u.rect.zw;
    p = ((p * 2.0) / u.screenSize) - 1.0;
    p.y *= u.yFlipSign;
    UiVOut out;
    out.position = float4(p, 0.0, 1.0);
    out.texcoord = in.texcoord;
    return out;
}
fragment float4 mp_ui_fs(UiVOut in [[stage_in]],
                         texture2d<float> tex [[texture(0)]],
                         sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texcoord);
}

// MELONPRIME_METAL_RADAR_NATIVE_V1 (PR-10): native Metal equivalent of the
// GL-native btmOverlay shader (kBtmOverlayVS/kBtmOverlayFS in main_shaders.h,
// wired up for GL in MelonPrimeHudScreenCppOverlayOfGl.inc). Reuses mp_ui_vs
// (same rect/screenSize/yFlipSign uniform layout as the UI overlay quad) for
// vertex placement; this fragment function samples layer 1 (bottom screen) of
// the renderer's own final MetalTexture directly -- no CPU bottom-screen
// composite/memcpy is ever involved on this path.
struct RadarUniforms {
    float2 srcCenter;
    float srcRadius;
    float opacity;
};
constant float3 kRadarPalette[15] = {
    float3(192,248,104), float3(248,168,168), float3(224, 48, 48),
    float3(160,160,160), float3(200,200,200), float3(144,144,144),
    float3(248,128, 16), float3(248,208,160), float3(216,104,  0),
    float3(136,224,  8), float3(200,248,128), float3(104,184,  0),
    float3( 16,152,200), float3( 40,216,248), float3(168,168,168)
};
fragment float4 mp_radar_fs(UiVOut in [[stage_in]],
                            texture2d_array<float> tex [[texture(0)]],
                            sampler samp [[sampler(0)]],
                            constant RadarUniforms& u [[buffer(0)]]) {
    float2 centered = in.texcoord * 2.0 - 1.0;
    float dist = dot(centered, centered);
    if (dist > 1.0) discard_fragment();
    float alpha = u.opacity * (1.0 - smoothstep(0.95, 1.0, dist));
    float2 srcUV = u.srcCenter + centered * float2(u.srcRadius, u.srcRadius * (256.0 / 192.0));
    float4 pixel = tex.sample(samp, srcUV, uint(1));
    float3 c = round(pixel.rgb * 255.0);
    bool match = false;
    for (int i = 0; i < 15; i++) {
        if (all(c == kRadarPalette[i])) { match = true; break; }
    }
    if (!match) discard_fragment();
    return float4(pixel.rgb, alpha);
}
