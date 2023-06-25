#version 450
#extension GL_EXT_mesh_shader : require

layout(location = 0) out vec4 FragColor;
layout(location = 0) perprimitiveEXT in vec4 vColor;

void main()
{
    FragColor = vColor;
}