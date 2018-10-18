#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float depth;
} registers;

void main()
{
    FragColor = vec4(registers.color, 1.0);
    gl_FragDepth = registers.depth;
}