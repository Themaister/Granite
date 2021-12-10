#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    uint value;
} registers;

layout(constant_id = 1) const uint USE_SPEC_CONSTANT = 0u;
layout(constant_id = 2) const uint VALUE = 0u;

void main()
{
    uint value = registers.value;
    if (USE_SPEC_CONSTANT != 0u)
        value = VALUE;

    switch (value & 3u)
    {
        case 0: FragColor = vec4(1.0, 0.0, 0.0, 1.0); break;
        case 1: FragColor = vec4(0.0, 1.0, 0.0, 1.0); break;
        case 2: FragColor = vec4(0.0, 0.0, 1.0, 1.0); break;
        case 3: FragColor = vec4(1.0, 0.0, 1.0, 1.0); break;
        default: FragColor = vec4(0.0); break;
    }
}