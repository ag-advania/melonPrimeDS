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
    if (fColor.a < (0.5 / 31.0) || fColor.a >= (30.5 / 31.0))
        discard;

    oColor = fColor;
    oAttr = vec4(0.0, 0.0, 0.0, 1.0);
#ifdef W_BUFFER
    gl_FragDepth = fDepth;
#endif
}
