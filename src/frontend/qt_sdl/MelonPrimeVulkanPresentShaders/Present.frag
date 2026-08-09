// MelonPrime Vulkan presenter -- quad fragment stage.
//
// A straight textured quad. The two pipelines built from this module differ only
// in their blend state, not in their code:
//
//   * opaque  -- the DS screens. The composed frame carries no meaningful alpha
//                (the compositor writes BGRX), so blending is disabled and the
//                Tint alpha is ignored.
//   * blended -- the Custom HUD overlay and the OSD strip. Both come from
//                QImage::Format_ARGB32_Premultiplied, so the blend factors are
//                ONE / ONE_MINUS_SRC_ALPHA, matching ScreenPanelGL exactly.
//
// Regenerate with tools/vulkan/compile-present-shaders.py.

#version 450

layout(set = 0, binding = 0) uniform sampler2D Source;

layout(push_constant) uniform PresentPush
{
    vec4 Axis;
    vec4 Origin;
    vec4 UvRect;
    vec4 Tint;
} pc;

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = texture(Source, vUV) * pc.Tint;
}
