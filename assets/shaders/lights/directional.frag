#version 450

#include "lighting.h"

layout(input_attachment_index = 0, set = 0, binding = 1) uniform subpassInput BaseColor;
layout(input_attachment_index = 1, set = 0, binding = 2) uniform subpassInput Normal;
layout(input_attachment_index = 2, set = 0, binding = 3) uniform subpassInput PBR;
layout(input_attachment_index = 3, set = 0, binding = 4) uniform subpassInput Depth;

layout(location = 0) out vec3 FragColor;
layout(location = 0) in vec4 vClip;
layout(location = 1) in vec4 vShadowClip;
layout(location = 2) in vec4 vShadowNearClip;

layout(std430, push_constant) uniform Registers
{
    vec4 inverse_view_projection_col2;
    vec4 shadow_projection_col2;
    vec4 shadow_projection_near_col2;
    vec3 direction;
    float inv_cutoff_distance;
    vec3 color;
	float environment_intensity;
    vec3 camera_pos;
	float environment_mipscale;
	vec3 camera_front;
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
    vec4 clip_shadow_near = vShadowNearClip + depth * registers.shadow_projection_near_col2;
    vec4 clip_shadow = vShadowClip + depth * registers.shadow_projection_col2;

    FragColor = compute_lighting(
		MaterialProperties(base_color_ambient.rgb, N, mr.x, mr.y * 0.9 + 0.1, base_color_ambient.a),
		LightInfo(pos, registers.camera_pos, registers.camera_front,
				registers.direction, registers.color,
				clip_shadow_near, clip_shadow, registers.inv_cutoff_distance),
		EnvironmentInfo(registers.environment_intensity, registers.environment_mipscale));
}
