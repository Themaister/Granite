#version 450

#include "clusterer.h"

layout(input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput BaseColor;
layout(input_attachment_index = 1, set = 0, binding = 2) uniform subpassInput Normal;
layout(input_attachment_index = 2, set = 0, binding = 3) uniform subpassInput PBR;
layout(input_attachment_index = 3, set = 0, binding = 4) uniform subpassInput Depth;

layout(location = 0) out vec3 FragColor;
layout(location = 0) in vec4 vClip;

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_view_projection_col2;
    vec3 camera_pos;
} registers;

void main()
{
    // Load material information.
    float depth = subpassLoad(Depth).x;
    vec2 mr = subpassLoad(PBR).xy;
    vec4 base_color_ambient = subpassLoad(BaseColor);
    vec3 N = subpassLoad(Normal).xyz * 2.0 - 1.0;

    // Reconstruct positions.
    vec4 clip = vClip + depth * registers.inverse_view_projection_col2;
    vec3 pos = clip.xyz / clip.w;

    FragColor = compute_cluster_light(
		MaterialProperties(base_color_ambient.rgb, N, mr.x, mr.y, base_color_ambient.a, 1.0),
		pos, registers.camera_pos);
}
