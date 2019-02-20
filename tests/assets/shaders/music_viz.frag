#version 450
layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float inv_count;
} registers;

void main()
{
    FragColor = vec4(registers.color, 1.0);
}