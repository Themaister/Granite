#pragma once

#include "math.hpp"

namespace Granite
{
struct RenderParameters
{
	mat4 projection;
	mat4 view;
	mat4 view_projection;
	mat4 inv_projection;
	mat4 inv_view;
	mat4 inv_view_projection;
	mat4 inv_local_view_projection;

	alignas(vec4) vec3 camera_position;
	alignas(vec4) vec3 camera_front;
	alignas(vec4) vec3 camera_right;
	alignas(vec4) vec3 camera_up;
};
}