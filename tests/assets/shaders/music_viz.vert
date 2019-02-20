#version 450

layout(location = 0) in float vMusic;
layout(push_constant, std430) uniform Registers
{
    vec3 color;
    float inv_count;
} registers;

void main()
{
    float x = float(gl_VertexIndex) * registers.inv_count;
    float y = -vMusic;
    x = 2.0 * x - 1.0;
    gl_Position = vec4(x, 16.0 * y, 0.0, 1.0);
}