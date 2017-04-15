#version 310 es

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

layout(location = 0) in vec2 Position;
layout(location = 0) out highp vec3 vDirection;

void main()
{
    gl_Position = vec4(Position, 1.0, 1.0);
    vDirection = (global.inv_local_view_projection * vec4(Position, 0.0, 1.0)).xyz;
}