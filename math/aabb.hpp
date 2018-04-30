/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "math.hpp"
#include "muglm/func.hpp"

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

	const vec3 &get_minimum() const
	{
		return minimum;
	}

	const vec3 &get_maximum() const
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