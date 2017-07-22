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
	bool intersects_fast(const AABB &aabb) const;

	vec3 get_coord(float dx, float dy, float dz) const;

	static vec4 get_bounding_sphere(const mat4 &inv_projection, const mat4 &inv_view);

private:
	vec4 planes[6];
	mat4 inv_view_projection;
};
}