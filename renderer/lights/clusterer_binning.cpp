/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "clusterer_binning.hpp"
#include "render_context.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
static void compute_spot_points_and_planes(vec3 spot_points[5], vec4 spot_planes[5], const mat4 &model)
{
	spot_points[0] = (model * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz();
	spot_points[1] = (model * vec4(-1.0f, +1.0f, -1.0f, 1.0f)).xyz();
	spot_points[2] = (model * vec4(+1.0f, +1.0f, -1.0f, 1.0f)).xyz();
	spot_points[3] = (model * vec4(+1.0f, -1.0f, -1.0f, 1.0f)).xyz();
	spot_points[4] = (model * vec4(-1.0f, -1.0f, -1.0f, 1.0f)).xyz();

	vec3 top_plane = normalize(cross(spot_points[1] - spot_points[0], spot_points[2] - spot_points[0]));
	vec3 right_plane = normalize(cross(spot_points[2] - spot_points[0], spot_points[3] - spot_points[0]));
	vec3 bottom_plane = normalize(cross(spot_points[3] - spot_points[0], spot_points[4] - spot_points[0]));
	vec3 left_plane = normalize(cross(spot_points[4] - spot_points[0], spot_points[1] - spot_points[0]));

	spot_planes[0] = vec4(top_plane, -dot(top_plane, spot_points[0]));
	spot_planes[1] = vec4(right_plane, -dot(right_plane, spot_points[0]));
	spot_planes[2] = vec4(bottom_plane, -dot(bottom_plane, spot_points[0]));
	spot_planes[3] = vec4(left_plane, -dot(left_plane, spot_points[0]));

	vec3 back_plane = normalize(cross(spot_points[1] - spot_points[2], spot_points[3] - spot_points[2]));
	spot_planes[4] = vec4(back_plane, -dot(back_plane, spot_points[1]));
}

// Slow reference culler.
bool frustum_intersects_spot_light(const RenderContext &context, const vec2 &clip_lo, const vec2 &clip_hi, const mat4 &model)
{
	auto &front = context.get_render_parameters().camera_front;
	auto &pos = context.get_render_parameters().camera_position;
	float x_scale = context.get_render_parameters().inv_projection[0][0];
	float y_scale = context.get_render_parameters().inv_projection[1][1];
	auto right = context.get_render_parameters().camera_right * x_scale;
	auto down = context.get_render_parameters().camera_up * y_scale;

	// PERF: These planes can be precomputed.
	vec3 tl = front + clip_lo.x * right + clip_lo.y * down;
	vec3 tr = front + clip_hi.x * right + clip_lo.y * down;
	vec3 bl = front + clip_lo.x * right + clip_hi.y * down;
	vec3 br = front + clip_hi.x * right + clip_hi.y * down;
	vec3 right_plane = normalize(cross(tr, br));
	vec3 top_plane = normalize(cross(tl, tr));
	vec3 left_plane = normalize(cross(bl, tl));
	vec3 bottom_plane = normalize(cross(br, bl));

	// PERF: Can be precomputed per spot light.
	vec3 spot_points[5];
	vec4 spot_planes[5];
	compute_spot_points_and_planes(spot_points, spot_planes, model);

	// Clip the Z planes to the range of the spot light.
	// PERF: Z planes can be computed once per spot light.
	float max_z_plane = context.get_render_parameters().z_near;
	float min_z_plane = context.get_render_parameters().z_far;
	for (auto &spot : spot_points)
	{
		float z_dist = dot(spot - pos, front);
		min_z_plane = min(min_z_plane, z_dist);
		max_z_plane = max(max_z_plane, z_dist);
	}

	// PERF: X/Y planes can be pre-computed.
	const vec4 planes[6] = {
		vec4(right_plane, -dot(right_plane, pos)),
		vec4(top_plane, -dot(top_plane, pos)),
		vec4(left_plane, -dot(left_plane, pos)),
		vec4(bottom_plane, -dot(bottom_plane, pos)),
		vec4(front, -dot(front, pos + front * min_z_plane)),
		vec4(-front, -dot(-front, pos + front * max_z_plane)),
	};

	// Try to cull per plane first.
	// PERF: We can even amortize here, where X planes are tested separately from Y planes.
	// To test a concrete frustum, we can compare results with bitwise operations.
	for (auto &plane : planes)
	{
		bool inside = false;
		for (auto &spot : spot_points)
		{
			float r = dot(vec4(spot, 1.0f), plane);
			if (r > 0.0f)
			{
				inside = true;
				break;
			}
		}

		// Found non-intersecting case!
		if (!inside)
			return false;
	}

	// Now we can try the other approach, test all points of the frustum against the spot light planes.
	// This one is hard to precompute.
	// We can precompute pos + front * {min,max}_z_plane.
	// But tl, tr, bl, br depend on the frustum itself.
	// We might be able to split the sum to amortize the ALU cost.
	// We're also only going to use these points in a dot product, so rather than computing the full points,
	// we might be able to get away with precomputing some dot-product factors and turn this into a simple FMA.
	// E.g.: dot(vec4(point, 1.0f), plane) can be split into:
	//
	// plane.w +                       (Precomputed per spot light)
	// dot(pos, plane.xyz) +           (Precomputed per spot light)
	// dot((front + tl), plane.xyz) *  (Dynamic)
	// min_z_plane                     (Precomputed per spot light)
	vec3 frustum_points[8];
	frustum_points[0] = pos + tl * min_z_plane;
	frustum_points[1] = pos + tl * max_z_plane;
	frustum_points[2] = pos + tr * min_z_plane;
	frustum_points[3] = pos + tr * max_z_plane;
	frustum_points[4] = pos + bl * min_z_plane;
	frustum_points[5] = pos + bl * max_z_plane;
	frustum_points[6] = pos + br * min_z_plane;
	frustum_points[7] = pos + br * max_z_plane;

	for (auto &plane : spot_planes)
	{
		bool inside = false;
		for (auto &point : frustum_points)
		{
			float r = dot(vec4(point, 1.0f), plane);
			if (r > 0.0f)
			{
				inside = true;
				break;
			}
		}

		// Found non-intersecting case!
		if (!inside)
			return false;
	}

	return true;
}

vec2 spot_light_z_range(const RenderContext &context, const mat4 &model)
{
	auto &pos = context.get_render_parameters().camera_position;
	auto &front = context.get_render_parameters().camera_front;

	float lo = std::numeric_limits<float>::infinity();
	float hi = -lo;

	vec3 base_pos = model[3].xyz();
	vec3 x_off = model[0].xyz();
	vec3 y_off = model[1].xyz();
	vec3 z_off = -model[2].xyz();

	vec3 z_base = base_pos + z_off;

	const vec3 world_pos[5] = {
		base_pos,
		z_base + x_off + y_off,
		z_base - x_off + y_off,
		z_base + x_off - y_off,
		z_base - x_off - y_off,
	};

	for (auto &p : world_pos)
	{
		float z = dot(p - pos, front);
		lo = muglm::min(z, lo);
		hi = muglm::max(z, hi);
	}

	return vec2(lo, hi);
}
}
