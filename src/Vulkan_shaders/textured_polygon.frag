#version 450

layout(std140, set = 0, binding = 0) uniform ToonHighlightConfig
{
    vec4 toon[32];
    uint dispCnt;
    uint mode;
    uint textured;
    uint pad;
} cfg;
layout(set = 0, binding = 1) uniform usampler2DArray CurTexture;

layout(location = 0) in vec4 fColor;
layout(location = 1) flat in uint fPolygonAttr;
layout(location = 2) in vec3 fTexcoord;
#ifdef W_BUFFER
layout(location = 3) in float fDepth;
#endif
layout(location = 0) out vec4 oColor;

vec4 Combine(vec4 vertexColor, vec4 textureColor)
{
    int index = int(clamp(vertexColor.r, 0.0, 1.0) * 31.0);
    vec4 working = vertexColor;
    if (cfg.mode == 1u)
        working.rgb = cfg.toon[index].rgb;
    else if (cfg.mode == 2u)
        working.rgb = vertexColor.rrr;

    vec4 color;
#ifdef DECAL_MODE
    color.rgb = textureColor.rgb * textureColor.a + working.rgb * (1.0 - textureColor.a);
    color.a = working.a;
#else
    color = working * textureColor;
#endif
    if (cfg.mode == 2u)
        color.rgb = min(color.rgb + cfg.toon[index].rgb, vec3(1.0));
    return color;
}

void main()
{
    vec4 textureColor = vec4(texture(CurTexture, fTexcoord)) / vec4(63.0, 63.0, 63.0, 31.0);
    vec4 color = Combine(fColor, textureColor);
#ifdef TRANSLUCENT_PASS
    if (color.a < (0.5 / 31.0) || color.a >= (30.5 / 31.0))
        discard;
#else
    if (color.a < (30.5 / 31.0))
        discard;
#endif
    oColor = color;
#ifdef W_BUFFER
    gl_FragDepth = fDepth;
#endif
}
