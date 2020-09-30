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

#include "frustum.hpp"

namespace Granite
{

// For reference, should always use SIMD-version.
bool Frustum::intersects_slow(const AABB &aabb) const
{
	for (auto &plane : planes)
	{
		bool intersects_plane = false;
		for (unsigned i = 0; i < 8; i++)
		{
			if (dot(vec4(aabb.get_corner(i), 1.0f), plane) >= 0.0f)
			{
				intersects_plane = true;
				break;
			}
		}

		if (!intersects_plane)
			return false;
	}

	return true;
}

bool Frustum::intersects_sphere(const AABB &aabb) const
{
	vec4 center(aabb.get_center(), 1.0f);
	float radius = aabb.get_radius();

	for (auto &plane : planes)
		if (dot(plane, center) < -radius)
			return false;

	return true;
}

vec3 Frustum::get_coord(float dx, float dy, float dz) const
{
	vec4 clip = vec4(2.0f * dx - 1.0f, 2.0f * dy - 1.0f, dz, 1.0f);
	clip = inv_view_projection * clip;
	return clip.xyz() / clip.w;
}

vec4 Frustum::get_bounding_sphere(const mat4 &inv_projection, const mat4 &inv_view)
{
	// Make sure that radius is numerically stable throughout, since we use that as a snapping factor potentially.
	// Use the inverse projection to create the radius.

	const auto get_coord = [&](float x, float y, float z) -> vec3 {
		vec4 clip = vec4(x, y, z, 1.0f);
		clip = inv_projection * clip;
		return clip.xyz() / clip.w;
	};

	vec3 center_near = get_coord(0.0f, 0.0f, 0.0f);
	vec3 center_far = get_coord(0.0f, 0.0f, 1.0f);

	vec3 near_pos = get_coord(-1.0f, -1.0f, 0.0f);
	vec3 far_pos = get_coord(+1.0f, +1.0f, 1.0f);

	float C = length(center_far - center_near);
	float N = dot(near_pos - center_near, near_pos - center_near);
	float F = dot(far_pos - center_far, far_pos - center_far);

	// Solve the equation:
	// n^2 + x^2 == f^2 + (C - x)^2 =>
	// N + x^2 == F + C^2 - 2Cx + x^2.
	// x = (F - N + C^2) / 2C
	float center_distance = (F - N + C * C) / (2.0f * C);
	float radius = muglm::sqrt(center_distance * center_distance + N);
	vec3 view_space_center = center_near + center_distance * normalize(center_far - center_near);
	vec3 center = (inv_view * vec4(view_space_center, 1.0f)).xyz();
	return vec4(center, radius);
}

void Frustum::build_planes(const mat4 &inv_view_projection_)
{
	inv_view_projection = inv_view_projection_;
	static const vec4 tln(-1.0f, -1.0f, 0.0f, 1.0f);
	static const vec4 tlf(-1.0f, -1.0f, 1.0f, 1.0f);
	static const vec4 bln(-1.0f, +1.0f, 0.0f, 1.0f);
	static const vec4 blf(-1.0f, +1.0f, 1.0f, 1.0f);
	static const vec4 trn(+1.0f, -1.0f, 0.0f, 1.0f);
	static const vec4 trf(+1.0f, -1.0f, 1.0f, 1.0f);
	static const vec4 brn(+1.0f, +1.0f, 0.0f, 1.0f);
	static const vec4 brf(+1.0f, +1.0f, 1.0f, 1.0f);
	static const vec4 c(0.0f, 0.0f, 0.5f, 1.0f);

	const auto project = [](const vec4 &v) {
		return v.xyz() / vec3(v.w);
	};

	vec3 TLN = project(inv_view_projection * tln);
	vec3 BLN = project(inv_view_projection * bln);
	vec3 BLF = project(inv_view_projection * blf);
	vec3 TRN = project(inv_view_projection * trn);
	vec3 TRF = project(inv_view_projection * trf);
	vec3 BRN = project(inv_view_projection * brn);
	vec3 BRF = project(inv_view_projection * brf);
	vec4 center = inv_view_projection * c;

	vec3 l = normalize(cross(BLF - BLN, TLN - BLN));
	vec3 r = normalize(cross(TRF - TRN, BRN - TRN));
	vec3 n = normalize(cross(BLN - BRN, TRN - BRN));
	vec3 f = normalize(cross(TRF - BRF, BLF - BRF));
	vec3 t = normalize(cross(TLN - TRN, TRF - TRN));
	vec3 b = normalize(cross(BRF - BRN, BLN - BRN));

	planes[0] = vec4(l, -dot(l, BLN));
	planes[1] = vec4(r, -dot(r, TRN));
	planes[2] = vec4(n, -dot(n, BRN));
	planes[3] = vec4(f, -dot(f, BRF));
	planes[4] = vec4(t, -dot(t, TRN));
	planes[5] = vec4(b, -dot(b, BRN));

	// Winding order checks.
	for (auto &p : planes)
		if (dot(center, p) < 0.0f)
			p = -p;
}
}
