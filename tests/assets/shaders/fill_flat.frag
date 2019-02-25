#version 450

#pragma optimize off

layout(std140, set = 0, binding = 0) uniform UBO
{
    vec4 color;
} ubos[4];

layout(push_constant, std430) uniform Registers
{
    int index;
} registers;

#if OUTPUT_COMPONENTS == 1
layout(location = 0) out float FragColor;
void main() { FragColor = ubos[registers.index].color.x * 0.2; }
#elif OUTPUT_COMPONENTS == 2
layout(location = 0) out vec2 FragColor;
void main() { FragColor = ubos[registers.index].color.xy * vec2(0.2); }
#elif OUTPUT_COMPONENTS == 3
layout(location = 0) out vec3 FragColor;
void main() { FragColor = ubos[registers.index].color.xyz * vec3(0.2); }
#elif OUTPUT_COMPONENTS == 4
layout(location = 0) out vec4 FragColor;
void main() { FragColor = ubos[registers.index].color * vec4(0.2); }
#else
#error "Invalid number of components."
#endif
