#pragma once

#include "math.hpp"

namespace Granite
{
class AABB
{
public:
	AABB(vec3 minimum, vec3 maximum)
		: minimum(minimum), maximum(maximum)
	{
	}

	AABB() = default;

	vec3 get_corner(unsigned i) const;
	vec3 get_center() const;
	float get_radius() const;
	AABB transform(const mat4 &m) const;

private:
	vec3 minimum;
	vec3 maximum;
};
}