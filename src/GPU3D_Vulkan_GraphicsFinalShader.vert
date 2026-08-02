#version 450

void main()
{
    vec2 position = vec2(-1.0, -1.0);
    if (gl_VertexIndex == 1)
        position = vec2(3.0, -1.0);
    else if (gl_VertexIndex == 2)
        position = vec2(-1.0, 3.0);

    gl_Position = vec4(position, 0.0, 1.0);
}
