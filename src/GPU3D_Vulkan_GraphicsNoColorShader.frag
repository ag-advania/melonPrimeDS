#version 450

layout(constant_id = 0) const uint WRITE_FRAG_DEPTH = 0u;
layout(constant_id = 1) const uint EDGE_MARK_PASS = 0u;

layout(location = 2) smooth in float fDepth;
layout(location = 1) out vec4 oAttr;
layout(location = 2) out float oDepthValue;

void main()
{
    if (EDGE_MARK_PASS != 0u)
        oAttr = vec4(0.0, 1.0, 0.0, 1.0);

    oDepthValue = fDepth;

    if (WRITE_FRAG_DEPTH != 0u)
        gl_FragDepth = fDepth;
}
