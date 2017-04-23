#include "aabb.hpp"

namespace Granite
{
AABB AABB::transform(const mat4 &m) const
{
	vec3 m0 = vec3(FLT_MAX);
	vec3 m1 = vec3(FLT_MIN);

	for (unsigned i = 0; i < 8; i++)
	{
		vec3 v = (m * vec4(get_corner(i), 1.0f)).xyz();
		m0 = min(v, m0);
		m1 = max(v, m1);
	}

	return AABB(m0, m1);
}

vec3 AABB::get_corner(unsigned i) const
{
	float x = i & 1 ? maximum.x : minimum.x;
	float y = i & 2 ? maximum.y : minimum.y;
	float z = i & 4 ? maximum.z : minimum.z;
	return vec3(x, y, z);
}

void AABB::expand(const AABB &aabb)
{
	minimum = min(minimum, aabb.minimum);
	maximum = max(maximum, aabb.maximum);
}

vec3 AABB::get_center() const
{
	return minimum + (maximum - minimum) * vec3(0.5f);
}

float AABB::get_radius() const
{
	return 0.5f * distance(minimum, maximum);
}
}