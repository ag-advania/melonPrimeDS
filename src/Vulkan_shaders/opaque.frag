#version 450

layout(location = 0) in vec4 fColor;
layout(location = 1) flat in uint fPolygonAttr;
#ifdef W_BUFFER
layout(location = 2) in float fDepth;
#endif

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oAttr;

void main()
{
    if (fColor.a < (30.5 / 31.0))
        discard;

    oColor = fColor;
    oAttr = vec4(
        float((fPolygonAttr >> 24) & 0x3Fu) / 63.0,
        0.0,
        float((fPolygonAttr >> 15) & 0x1u),
        1.0);
#ifdef W_BUFFER
    gl_FragDepth = fDepth;
#endif
}
