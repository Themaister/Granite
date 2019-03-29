#version 450

layout(push_constant) uniform Registers
{
    mat4 vp;
} registers;

layout(location = 0) in vec4 Position;

void main()
{
    gl_Position = registers.vp * Position;
}