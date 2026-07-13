#version 450

// Full-screen triangle used to clear only stencil bit 7 while preserving the
// lower polygon-ID and translucent-ID bits.
void main()
{
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(-1.0,  3.0),
        vec2( 3.0, -1.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
