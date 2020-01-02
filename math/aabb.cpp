/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#include "aabb.hpp"
#include <float.h>

namespace Granite
{
AABB AABB::transform(const mat4 &m) const
{
	vec3 m0 = vec3(FLT_MAX);
	vec3 m1 = vec3(-FLT_MAX);

	for (unsigned i = 0; i < 8; i++)
	{
		vec3 c = get_corner(i);
		vec4 t = m * vec4(c, 1.0f);
		vec3 v = t.xyz();
		m0 = min(v, m0);
		m1 = max(v, m1);
	}

	return AABB(m0, m1);
}

vec3 AABB::get_coord(float dx, float dy, float dz) const
{
	return mix(minimum.v3, maximum.v3, vec3(dx, dy, dz));
}

void AABB::expand(const AABB &aabb)
{
	minimum.v3 = min(minimum.v3, aabb.minimum.v3);
	maximum.v3 = max(maximum.v3, aabb.maximum.v3);
}

}