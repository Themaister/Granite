#pragma once

#include "math.hpp"
#include "aabb.hpp"

namespace Granite
{
class Frustum
{
public:
	void build_planes(const mat4& inv_projection);
	bool intersects(const AABB &aabb) const;

private:
	vec4 planes[6];
};
}