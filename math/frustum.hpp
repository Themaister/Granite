#pragma once

#include "math.hpp"
#include "aabb.hpp"

namespace Granite
{
class Frustum
{
public:
	void build_planes(const mat4& inv_view_projection);
	bool intersects(const AABB &aabb) const;

	vec3 get_coord(float dx, float dy, float dz) const;

private:
	vec4 planes[6];
	mat4 inv_view_projection;
};
}