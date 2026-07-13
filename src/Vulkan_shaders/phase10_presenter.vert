#version 450

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
layout(push_constant) uniform DirectPush
{
    vec4 transform0; // m0, m1, m2, m3
    vec4 transform1; // m4, m5, panel width, panel height
    vec4 geometry;   // local width, local height, source scale x/y
    vec4 radar;      // source center x/y, radius, opacity
    uvec4 params;    // mode, layer, reserved, reserved
} pc;

layout(location = 0) out vec2 texCoord;

void main()
{
    const vec2 unitPosition[6] = vec2[6](
        vec2(0.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
        vec2(0.0, 0.0),
        vec2(1.0, 1.0),
        vec2(1.0, 0.0));

    vec2 unit = unitPosition[gl_VertexIndex];
    vec2 local = unit * pc.geometry.xy;

    vec2 widget;
    widget.x =
        pc.transform0.x * local.x +
        pc.transform0.z * local.y +
        pc.transform1.x;
    widget.y =
        pc.transform0.y * local.x +
        pc.transform0.w * local.y +
        pc.transform1.y;

    vec2 panel = max(pc.transform1.zw, vec2(1.0));
    vec2 ndc = (widget * 2.0) / panel - 1.0;
    ndc.y *= -1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    texCoord = unit;
}
