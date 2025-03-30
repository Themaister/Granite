#version 450

layout(push_constant) uniform Regs { uint burn_count; };
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(1, 2, 3, 4);
    for (uint i = 0; i < burn_count; i++)
        FragColor = sin(FragColor);
}