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

#include "cpu_rasterizer.hpp"
#include "simd.hpp"

namespace Granite
{
namespace Rasterizer
{
static unsigned get_clip_code_low(float a, float b, float c, float limit)
{
	bool clip_a = a < limit;
	bool clip_b = b < limit;
	bool clip_c = c < limit;
	unsigned clip_code = (unsigned(clip_a) << 0) | (unsigned(clip_b) << 1) | (unsigned(clip_c) << 2);
	return clip_code;
}

// Create a bitmask for which vertices clip outside some boundary.
static unsigned get_clip_code_high(float a, float b, float c, float limit)
{
	bool clip_a = a > limit;
	bool clip_b = b > limit;
	bool clip_c = c > limit;
	unsigned clip_code = (unsigned(clip_a) << 0) | (unsigned(clip_b) << 1) | (unsigned(clip_c) << 2);
	return clip_code;
}

struct Triangle
{
	vec4 vertices[3];
};

struct TriangleSetup
{
	vec3 base;
	vec3 dx;
	vec3 dy;
	vec2 lo;
	vec2 hi;
};

static float cross_2d(const vec2 &a, const vec2 &b)
{
	return a.x * b.y - a.y * b.x;
}

static bool setup_triangle(TriangleSetup &setup, const Triangle &tri, CullMode cull)
{
	vec2 ab = tri.vertices[1].xy() - tri.vertices[0].xy();
	vec2 bc = tri.vertices[2].xy() - tri.vertices[1].xy();
	vec2 ca = tri.vertices[0].xy() - tri.vertices[2].xy();
	float z = cross_2d(ab, -ca);

	if ((cull == CullMode::Front && z >= 0.0f) ||
	    (cull == CullMode::Back && z <= 0.0f) ||
	    (cull == CullMode::Both && z == 0.0f))
	{
		return false;
	}

	float inv_z = 1.0f / z;

	setup.base.x = cross_2d(ab, -tri.vertices[0].xy()) * inv_z;
	setup.base.y = cross_2d(bc, -tri.vertices[1].xy()) * inv_z;
	setup.base.z = cross_2d(ca, -tri.vertices[2].xy()) * inv_z;

	setup.dx.x = -ab.y * inv_z;
	setup.dx.y = -bc.y * inv_z;
	setup.dx.z = -ca.y * inv_z;

	setup.dy.x = ab.x * inv_z;
	setup.dy.y = bc.x * inv_z;
	setup.dy.z = ca.x * inv_z;

	vec2 lo = min(min(tri.vertices[0].xy(), tri.vertices[1].xy()), tri.vertices[2].xy());
	vec2 hi = max(max(tri.vertices[0].xy(), tri.vertices[1].xy()), tri.vertices[2].xy());
	setup.lo = lo;
	setup.hi = hi;

	return true;
}

static void interpolate_vertex(vec4 &v, const vec4 &a, const vec4 &b, float l)
{
	v = mix(a, b, l);
}

static void clip_single_output(Triangle &output, const Triangle &input, unsigned component, float target,
                               unsigned a, unsigned b, unsigned c)
{
	float interpolate_a = (target - input.vertices[a][component]) /
	                      (input.vertices[c][component] - input.vertices[a][component]);
	float interpolate_b = (target - input.vertices[b][component]) /
	                      (input.vertices[c][component] - input.vertices[b][component]);

	interpolate_vertex(output.vertices[a], input.vertices[a], input.vertices[c], interpolate_a);
	interpolate_vertex(output.vertices[b], input.vertices[b], input.vertices[c], interpolate_b);

	output.vertices[a][component] = target;
	output.vertices[b][component] = target;
	output.vertices[c] = input.vertices[c];
}

static void clip_dual_output(Triangle *output, const Triangle &input, unsigned component, float target,
                             unsigned a, unsigned b, unsigned c)
{
	float interpolate_ab = (target - input.vertices[a][component]) /
	                       (input.vertices[b][component] - input.vertices[a][component]);
	float interpolate_ac = (target - input.vertices[a][component]) /
	                       (input.vertices[c][component] - input.vertices[a][component]);

	vec4 ab, ac;
	interpolate_vertex(ab, input.vertices[a], input.vertices[b], interpolate_ab);
	interpolate_vertex(ac, input.vertices[a], input.vertices[c], interpolate_ac);

	// To avoid precision issues in interpolating, we expect the new vertex to be perfectly aligned with the clip plane.
	ab[component] = target;
	ac[component] = target;

	output[0].vertices[0] = ab;
	output[0].vertices[1] = input.vertices[b];
	output[0].vertices[2] = ac;
	output[1].vertices[0] = ac;
	output[1].vertices[1] = input.vertices[b];
	output[1].vertices[2] = input.vertices[c];
}

// Clipping a primitive results in 0, 1 or 2 primitives.
static unsigned clip_component(Triangle *prims, const Triangle &prim, unsigned component,
                               float target, unsigned code)
{
	switch (code)
	{
	case 0:
		// Nothing to clip. 1:1
		prims[0] = prim;
		return 1;

	case 1:
		// Clip vertex A. 2 new primitives.
		clip_dual_output(prims, prim, component, target, 0, 1, 2);
		return 2;

	case 2:
		// Clip vertex B. 2 new primitives.
		clip_dual_output(prims, prim, component, target, 1, 2, 0);
		return 2;

	case 3:
		// Interpolate A and B against C. 1 primitive.
		clip_single_output(prims[0], prim, component, target, 0, 1, 2);
		return 1;

	case 4:
		// Clip vertex C. 2 new primitives.
		clip_dual_output(prims, prim, component, target, 2, 0, 1);
		return 2;

	case 5:
		// Interpolate A and C against B. 1 primitive.
		clip_single_output(prims[0], prim, component, target, 2, 0, 1);
		return 1;

	case 6:
		// Interpolate B and C against A. 1 primitive.
		clip_single_output(prims[0], prim, component, target, 1, 2, 0);
		return 1;

	case 7:
		// All clipped. Discard primitive.
		return 0;

	default:
		return 0;
	}
}

static unsigned clip_triangles(Triangle *outputs, const Triangle *inputs, unsigned count, unsigned component, float target)
{
	unsigned output_count = 0;

	for (unsigned i = 0; i < count; i++)
	{
		unsigned clip_code;
		if (target > 0.0f)
		{
			clip_code = get_clip_code_high(
					inputs[i].vertices[0][component],
					inputs[i].vertices[1][component],
					inputs[i].vertices[2][component],
					target);
		}
		else
		{
			clip_code = get_clip_code_low(
					inputs[i].vertices[0][component],
					inputs[i].vertices[1][component],
					inputs[i].vertices[2][component],
					target);
		}

		unsigned clipped_count = clip_component(outputs, inputs[i], component, target, clip_code);
		output_count += clipped_count;
		outputs += clipped_count;
	}

	return output_count;
}

static unsigned setup_clipped_triangles_clipped_w(TriangleSetup *setup, Triangle &prim, CullMode cull)
{
	// Cull primitives on X/Y early.
	// If all vertices are outside clip-space, we know the primitive is not visible.
	if (prim.vertices[0].x < -prim.vertices[0].w &&
	    prim.vertices[1].x < -prim.vertices[1].w &&
	    prim.vertices[2].x < -prim.vertices[2].w)
	{
		return 0;
	}
	else if (prim.vertices[0].y < -prim.vertices[0].w &&
	         prim.vertices[1].y < -prim.vertices[1].w &&
	         prim.vertices[2].y < -prim.vertices[2].w)
	{
		return 0;
	}
	else if (prim.vertices[0].x > prim.vertices[0].w &&
	         prim.vertices[1].x > prim.vertices[1].w &&
	         prim.vertices[2].x > prim.vertices[2].w)
	{
		return 0;
	}
	else if (prim.vertices[0].y > prim.vertices[0].w &&
	         prim.vertices[1].y > prim.vertices[1].w &&
	         prim.vertices[2].y > prim.vertices[2].w)
	{
		return 0;
	}

	Triangle tmp[2];

	for (auto &vert : prim.vertices)
	{
		float iw = 1.0f / vert.w;
		vert.x *= iw;
		vert.y *= iw;
		vert.z *= iw;
		vert.w = iw;

		vert.x = vert.x * 0.5f + 0.5f;
		vert.y = vert.y * 0.5f + 0.5f;
	}

	// Clip far, before viewport transform.
	unsigned count = clip_triangles(tmp, &prim, 1, 2, +1.0f);

	unsigned output_count = 0;
	for (unsigned i = 0; i < count; i++)
	{
		// Finally, we can perform triangle setup.
		if (setup_triangle(setup[output_count], tmp[i], cull))
			output_count++;
	}

	return output_count;
}

unsigned setup_clipped_triangles(TriangleSetup *setup, const vec4 &a, const vec4 &b, const vec4 &c, CullMode cull)
{
	constexpr float MIN_W = 1.0f / 1024.0f;

	unsigned clip_code_w = get_clip_code_low(a.w, b.w, c.w, MIN_W);
	Triangle clipped_w[2];
	Triangle prim = { a, b, c };
	unsigned clipped_w_count = clip_component(clipped_w, prim, 3, MIN_W, clip_code_w);
	unsigned output_count = 0;

	for (unsigned i = 0; i < clipped_w_count; i++)
	{
		unsigned count = setup_clipped_triangles_clipped_w(setup, clipped_w[i], cull);
		setup += count;
		output_count += count;
	}
	return output_count;
}

void rasterize_conservative_triangles(std::vector<uvec2> &coverage,
                                      const vec4 *clip_positions,
                                      const unsigned *indices, unsigned num_indices,
                                      uvec2 resolution, CullMode cull)
{
	vec2 fresolution = vec2(resolution);
	vec2 inv_resolution = 1.0f / fresolution;

	TriangleSetup setups[4];
	for (unsigned index = 0; index < num_indices; index += 3)
	{
		unsigned count = setup_clipped_triangles(setups,
		                                         clip_positions[indices[index + 0]],
		                                         clip_positions[indices[index + 1]],
		                                         clip_positions[indices[index + 2]], cull);

		for (unsigned i = 0; i < count; i++)
		{
			auto &setup = setups[i];
			ivec2 lo = ivec2(setup.lo * fresolution);
			ivec2 hi = ivec2(setup.hi * fresolution);
			lo = max(lo, ivec2(0));
			hi = min(hi, ivec2(resolution) - 1);

			vec3 base = setup.base + setup.dx * float(lo.x) * inv_resolution.x + setup.dy * float(lo.y) * inv_resolution.y;
			base += select(vec3(0.0f), setup.dx * inv_resolution.x, greaterThan(setup.dx, vec3(0.0f)));
			base += select(vec3(0.0f), setup.dy * inv_resolution.y, greaterThan(setup.dy, vec3(0.0f)));

			const vec3 step_x = setup.dx * inv_resolution.x;
			const vec3 step_y = setup.dy * inv_resolution.y;

			for (int y = lo.y; y <= hi.y; y++)
			{
				vec3 step = base;
				for (int x = lo.x; x <= hi.x; x++)
				{
					if (all(greaterThan(step, vec3(0.0f))))
						coverage.emplace_back(x, y);
					step += step_x;
				}
				base += step_y;
			}
		}
	}
}

void transform_vertices(vec4 *clip_position, const vec4 *positions, unsigned num_positions, const mat4 &mvp)
{
	for (unsigned i = 0; i < num_positions; i++)
		SIMD::mul(clip_position[i], mvp, positions[i]);
}
}
}
