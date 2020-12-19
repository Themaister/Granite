#version 450
layout(location = 0) in vec4 Position;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vClip;

layout(std140, set = 1, binding = 0) uniform Registers
{
    mat4 shadow_matrix;
    mat4 inv_view_projection;
    vec4 inv_z_transform;
    vec2 noise_scale;
} registers;

void main()
{
    vUV = 0.5 * Position.xy + 0.5;
    vClip = registers.inv_view_projection * vec4(Position.xy, 0.0, 1.0);
    gl_Position = Position;
}
