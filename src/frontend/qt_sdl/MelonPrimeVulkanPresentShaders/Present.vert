// MelonPrime Vulkan presenter -- quad vertex stage.
//
// One shader draws every layer the presenter composes (game screens, Custom HUD
// overlay, OSD strip). There is no vertex buffer: the quad corners come from
// gl_VertexIndex and the placement comes from a push-constant affine transform,
// so a layer is one vkCmdPushConstants + one vkCmdDraw(4) with no per-frame
// buffer traffic at all.
//
// The affine transform is exactly the 2x3 matrix ScreenLayout produces
// (ScreenPanel::screenMatrix), pre-multiplied on the CPU by the source rect and
// the device pixel ratio. That is what keeps rotation, gap, screen swap,
// integer scaling and letterboxing identical to the OpenGL and software panels:
// the maths is not reimplemented here, only applied.
//
// Regenerate with tools/vulkan/compile-present-shaders.py.

#version 450

layout(push_constant) uniform PresentPush
{
    // xy = the quad's X axis in destination pixels, zw = its Y axis.
    vec4 Axis;
    // xy = quad origin in destination pixels, zw = destination size in pixels.
    vec4 Origin;
    // Source rect in normalized texture coordinates: (u0, v0, u1, v1).
    vec4 UvRect;
    // Per-layer colour multiplier. Alpha carries the radar/overlay opacity.
    vec4 Tint;
} pc;

layout(location = 0) out vec2 vUV;

void main()
{
    // Triangle strip corner order: (0,0) (1,0) (0,1) (1,1).
    const vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));

    const vec2 pixel = pc.Origin.xy + pc.Axis.xy * corner.x + pc.Axis.zw * corner.y;

    // Vulkan clip space is y-down relative to OpenGL, which is what the DS
    // screen layout already assumes, so no y flip is applied here.
    gl_Position = vec4((pixel / pc.Origin.zw) * 2.0 - 1.0, 0.0, 1.0);

    vUV = mix(pc.UvRect.xy, pc.UvRect.zw, corner);
}
