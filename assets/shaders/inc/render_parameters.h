#ifndef RENDER_PARAMETERS_H
#define RENDER_PARAMETERS_H

#include "../inc/global_bindings.h"

layout(set = 0, binding = BINDING_GLOBAL_TRANSFORM, std140) uniform RenderParameters
{
	mat4 projection;
	mat4 view;
	mat4 view_projection;
	mat4 inv_projection;
	mat4 inv_view;
	mat4 inv_view_projection;
	mat4 local_view_projection;
	mat4 inv_local_view_projection;
	mat4 multiview_view_projection[4];

	vec3 camera_position;
	vec3 camera_front;
	vec3 camera_right;
	vec3 camera_up;

	float z_near;
	float z_far;
} global;

float clip_z_to_linear(float clip_z)
{
	vec2 z = global.inv_projection[2].zw * clip_z + global.inv_projection[3].zw;
	return -z.x / z.y;
}

#endif
