#version 450
layout(location = 0) in vec2 Position;
layout(location = 0) out vec4 vClip;

layout(std430, push_constant) uniform Registers
{
    mat4 inverse_view_projection;
    vec3 camera_pos;
    vec3 color;
    float falloff;
} registers;

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vClip = registers.inverse_view_projection * vec4(Position, 0.0, 1.0);
}