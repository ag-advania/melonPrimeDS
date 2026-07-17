#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in float inAlpha;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out float fragAlpha;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragUv = inUv;
    fragAlpha = inAlpha;
}
