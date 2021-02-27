#version 450

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec4 vColor;

void main()
{
    FragColor = vColor;
}