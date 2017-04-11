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
};
}