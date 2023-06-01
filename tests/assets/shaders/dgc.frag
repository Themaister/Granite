#version 450

layout(location = 0) out vec4 FragColor;

void main()
{
#if DGC == 0
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
#elif DGC == 1
    FragColor = vec4(0.0, 1.0, 0.0, 1.0);
#else
    FragColor = vec4(0.0, 0.0, 1.0, 1.0);
#endif
}