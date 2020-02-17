#version 450

layout(location = 0) in vec2 Position;
layout(location = 1) in vec4 Color;
layout(location = 0) out vec4 vColor;

layout(push_constant, std430) uniform Registers
{
    vec2 scale;
    vec2 offset;
} registers;

void main()
{
    gl_Position = vec4(registers.scale * Position + registers.offset, 0.0, 1.0);
    vColor = Color;
}
