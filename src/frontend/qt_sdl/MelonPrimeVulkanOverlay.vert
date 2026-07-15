#version 450

layout(push_constant) uniform OverlayPushConstants
{
    vec2 surfaceSize;
    vec2 drawOrigin;
    vec2 drawSize;
} pushConstants;

layout(location = 0) out vec2 fragUv;

vec2 positions[6] = vec2[](
    vec2(0.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0));

void main()
{
    vec2 local = positions[gl_VertexIndex];
    vec2 pixel = pushConstants.drawOrigin + local * pushConstants.drawSize;
    vec2 ndc = (pixel / pushConstants.surfaceSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUv = local;
}
