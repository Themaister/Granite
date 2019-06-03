#version 450
#extension GL_EXT_multiview : require
layout(location = 0) out vec4 FragColor;

const vec4 colors[4] = vec4[](vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0), vec4(1.0, 0.0, 1.0, 1.0));

void main()
{
    FragColor = colors[gl_ViewIndex];
}