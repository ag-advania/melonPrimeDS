#version 450

layout(set = 0, binding = 0) uniform sampler2D uHudTexture;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(uHudTexture, fragUv);
}
