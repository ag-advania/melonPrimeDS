#version 450
#ifdef TOON_HIGHLIGHT_RUNTIME
layout(std140,set=0,binding=0) uniform ToonHighlightConfig
{
    vec4 toon[32];
    uint dispCnt;
    uint mode;
    uint textured;
    uint pad;
} cfg;
#endif

layout(location = 0) in vec4 fColor;
layout(location = 1) flat in uint fPolygonAttr;
#ifdef W_BUFFER
layout(location = 2) in float fDepth;
#endif

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oAttr;

vec4 ApplyToonHighlight(vec4 vertexColor)
{
#ifdef TOON_HIGHLIGHT_RUNTIME
    int index = int(clamp(vertexColor.r, 0.0, 1.0) * 31.0);
    vec4 color = vertexColor;
    if (cfg.mode == 1u)
        color.rgb = cfg.toon[index].rgb;
    else if (cfg.mode == 2u)
        color.rgb = min(vertexColor.rrr + cfg.toon[index].rgb, vec3(1.0));
    return color;
#else
    return vertexColor;
#endif
}

void main()
{
    vec4 color = ApplyToonHighlight(fColor);
    if (color.a < (30.5 / 31.0))
        discard;
    oColor = color;
    oAttr = vec4(
        float((fPolygonAttr >> 24) & 0x3Fu) / 63.0,
        0.0,
        float((fPolygonAttr >> 15) & 0x1u),
        1.0);
#ifdef W_BUFFER
    gl_FragDepth = fDepth;
#endif
}
