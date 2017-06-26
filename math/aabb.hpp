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

	vec3 get_coord(float dx, float dy, float dz) const;
	AABB transform(const mat4 &m) const;

	void expand(const AABB &aabb);

	vec3 get_minimum() const
	{
		return minimum;
	}

	vec3 get_maximum() const
	{
		return maximum;
	}

	vec3 get_corner(unsigned i) const
	{
		float x = i & 1 ? maximum.x : minimum.x;
		float y = i & 2 ? maximum.y : minimum.y;
		float z = i & 4 ? maximum.z : minimum.z;
		return vec3(x, y, z);
	}

	vec3 get_center() const
	{
		return minimum + (maximum - minimum) * vec3(0.5f);
	}

	float get_radius() const
	{
		return 0.5f * distance(minimum, maximum);
	}

private:
	vec3 minimum;
	vec3 maximum;
};
}