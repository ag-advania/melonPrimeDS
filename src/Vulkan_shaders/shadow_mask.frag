#version 450

#ifdef W_BUFFER
layout(location = 2) in float fDepth;
#endif

void main()
{
#ifdef W_BUFFER
    gl_FragDepth = fDepth;
#endif
}
