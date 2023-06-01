#version 450

layout(location = 0) in vec2 aPos;

void main()
{
#if DGC == 0
    gl_Position = vec4(aPos * 0.2 - 0.5, 0.0, 1.0);
#elif DGC == 1
    gl_Position = vec4(aPos * 0.2 + 0.5, 0.0, 1.0);
#else
    gl_Position = vec4(aPos * 0.2 + vec2(0.5, -0.5), 0.0, 1.0);
#endif
}