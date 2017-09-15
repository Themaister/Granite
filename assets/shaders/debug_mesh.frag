#version 450
precision mediump float;

#if HAVE_VERTEX_COLOR
layout(location = 0) in mediump vec4 vColor;
#endif

layout(location = 0) out vec4 FragColor;

void main()
{
#if HAVE_VERTEX_COLOR
    FragColor = vColor;
#else
    FragColor = vec4(1.0);
#endif
}