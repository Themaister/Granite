#version 450

layout(push_constant) uniform Registers { uint index; };
layout(set = 0, binding = 0) buffer SSBO { uint data[]; };

void main()
{
    atomicAdd(data[index], 1u);
    gl_Position = vec4(-1.0);
}