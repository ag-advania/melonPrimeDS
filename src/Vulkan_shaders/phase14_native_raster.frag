#version 450

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
layout(set = 0, binding = 0) uniform usampler2DArray nativeTexture;

layout(push_constant) uniform NativeRasterPush
{
    vec2 screenSize;
    uint textured;
    uint textureMode;
    uint wBuffer;
    uint renderXPos;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) in vec4 fColor;
layout(location = 1) in vec2 fTexcoord;
layout(location = 2) in float fDepth;
layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = fColor;
    if (pc.textured != 0u)
    {
        vec4 textureColor = vec4(texture(nativeTexture, vec3(fTexcoord, 0.0))) /
            vec4(63.0, 63.0, 63.0, 31.0);
        if (pc.textureMode == 1u)
        {
            color.rgb = textureColor.rgb * textureColor.a +
                color.rgb * (1.0 - textureColor.a);
        }
        else
        {
            color *= textureColor;
        }
    }

    if (color.a < (30.5 / 31.0))
        discard;

    outColor = color;
    gl_FragDepth = pc.wBuffer != 0u ? fDepth : gl_FragCoord.z;
}
