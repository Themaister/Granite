#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out vec2 vQuad;

layout(push_constant, std430) uniform Registers
{
    vec4 color;
    float phase;
} registers;

void main()
{
    vec2 p = Position * vec2(0.2, 0.8);
    p.x += registers.phase;
    gl_Position = vec4(p, 0.0, 1.0);
    vQuad = Position;
}