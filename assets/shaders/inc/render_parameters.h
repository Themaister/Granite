#ifndef RENDER_PARAMETERS_H
#define RENDER_PARAMETERS_H
layout(set = 0, binding = 0, std140) uniform RenderParameters
{
	mat4 projection;
	mat4 view;
	mat4 view_projection;
	mat4 inv_projection;
	mat4 inv_view;
	mat4 inv_view_projection;
	mat4 inv_local_view_projection;

	vec3 camera_position;
	vec3 camera_front;
	vec3 camera_right;
	vec3 camera_up;
} global;

#endif
