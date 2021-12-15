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

#include "transforms.hpp"
#include "aabb.hpp"
#include "simd.hpp"
#include "muglm/matrix_helper.hpp"
#include <assert.h>

namespace Granite
{
bool compute_plane_reflection(mat4 &projection, mat4 &view, vec3 camera_pos, vec3 center, vec3 normal, vec3 look_up,
                              float radius_up, float radius_other, float &z_near, float z_far)
{
	normal = normalize(normal);

	// Reflect the camera position from the plane.
	float over_plane = dot(normal, camera_pos - center);
	if (over_plane <= 0.0f)
		return false;

	camera_pos -= 2.0f * over_plane * normal;

	// The look direction is up through the plane direction.
	// This way we avoid skewed near and far planes (i.e. oblique).
	// Make sure look_up is perpendicular to normal.
	vec3 look_pos_x = normalize(cross(normal, look_up));
	look_up = normalize(cross(look_pos_x, normal));

	view = mat4_cast(look_at(normal, look_up)) * translate(-camera_pos);

	float dist_x = dot(look_pos_x, center - camera_pos);
	float left = dist_x - radius_other;
	float right = dist_x + radius_other;

	float dist_y = dot(look_up, center - camera_pos);
	float bottom = dist_y - radius_up;
	float top = dist_y + radius_up;

	z_near = over_plane;
	projection = frustum(left, right, bottom, top, over_plane, z_far);
	if (z_near >= z_far)
		return false;
	return true;
}

bool compute_plane_refraction(mat4 &projection, mat4 &view, vec3 camera_pos, vec3 center, vec3 normal, vec3 look_up,
                              float radius_up, float radius_other, float &z_near, float z_far)
{
	normal = normalize(normal);

	// Reflect the camera position from the plane.
	float over_plane = dot(normal, camera_pos - center);
	if (over_plane <= 0.0f)
		return false;

	normal = -normal;

	// The look direction is up through the plane direction.
	// This way we avoid skewed near and far planes (i.e. oblique).
	// Make sure look_up is perpendicular to normal.
	vec3 look_pos_x = normalize(cross(normal, look_up));
	look_up = normalize(cross(look_pos_x, normal));

	view = mat4_cast(look_at(normal, look_up)) * translate(-camera_pos);

	float dist_x = dot(look_pos_x, center - camera_pos);
	float left = dist_x - radius_other;
	float right = dist_x + radius_other;

	float dist_y = dot(look_up, center - camera_pos);
	float bottom = dist_y - radius_up;
	float top = dist_y + radius_up;

	z_near = over_plane;
	projection = frustum(left, right, bottom, top, over_plane, z_far);
	if (z_near >= z_far)
		return false;
	return true;
}

void compute_model_transform(mat4 &world, vec3 s, quat rot, vec3 trans, const mat4 &parent)
{
	mat4 model;
	model[3] = vec4(trans, 1.0f);
	SIMD::convert_quaternion_with_scale(&model[0], rot, s);
	SIMD::mul(world, parent, model);
}

void compute_normal_transform(mat4 &normal, const mat4 &world)
{
	normal = mat4(transpose(inverse(mat3(world))));
}

quat rotate_vector(vec3 from, vec3 to)
{
	from = normalize(from);
	to = normalize(to);

	float cos_angle = dot(from, to);
	if (abs(cos_angle) > 0.9999f)
	{
		if (cos_angle > 0.9999f)
			return quat(1.0f, 0.0f, 0.0f, 0.0f);
		else
		{
			vec3 rotation = cross(vec3(1.0f, 0.0f, 0.0f), from);
			if (dot(rotation, rotation) > 0.001f)
				rotation = normalize(rotation);
			else
				rotation = normalize(cross(vec3(0.0f, 1.0f, 0.0f), from));
			return quat(0.0f, rotation);
		}
	}

	vec3 rotation = normalize(cross(from, to));
	vec3 half_vector = normalize(from + to);
	float cos_half_range = clamp(dot(half_vector, from), 0.0f, 1.0f);
	float sin_half_angle = sqrtf(1.0f - cos_half_range * cos_half_range);
	return quat(cos_half_range, rotation * sin_half_angle);
}

quat rotate_vector_axis(vec3 from, vec3 to, vec3 axis)
{
	axis = normalize(axis);
	from = normalize(cross(axis, from));
	to = normalize(cross(axis, to));

	if (dot(to, from) < -0.9999f)
		return quat(0.0f, axis);

	// Rotate CCW or CW, we only find the angle of rotation below.
	float quat_sign = sign(dot(axis, cross(from, to)));

	vec3 half_vector = normalize(from + to);
	float cos_half_range = clamp(dot(half_vector, from), 0.0f, 1.0f);
	float sin_half_angle = quat_sign * sqrtf(1.0f - cos_half_range * cos_half_range);
	return quat(cos_half_range, axis * sin_half_angle);
}

quat look_at(vec3 direction, vec3 up)
{
	static const vec3 z(0.0f, 0.0f, -1.0f);
	static const vec3 y(0.0f, 1.0f, 0.0f);
	direction = normalize(direction);
	vec3 right = cross(direction, up);
	vec3 actual_up = cross(right, direction);
	quat look_transform = rotate_vector(direction, z);
	quat up_transform = rotate_vector_axis(look_transform * actual_up, y, z);
	return up_transform * look_transform;
}

quat look_at_arbitrary_up(vec3 direction)
{
	return rotate_vector(normalize(direction), vec3(0.0f, 0.0f, -1.0f));
}

mat4 projection(float fovy, float aspect, float znear, float zfar)
{
	return perspective(fovy, aspect, znear, zfar);
}

mat4 ortho(const AABB &aabb)
{
	vec3 min = aabb.get_minimum();
	vec3 max = aabb.get_maximum();

	// Flip Z for RH, ortho zNear/zFar is LH style.
	std::swap(max.z, min.z);
	max.z = -max.z;
	min.z = -min.z;

	return muglm::ortho(min.x, max.x, min.y, max.y, min.z, max.z);
}

void compute_cube_render_transform(vec3 center, unsigned face, mat4 &proj, mat4 &view, float znear, float zfar)
{
	static const vec3 dirs[6] = {
		vec3(1.0f, 0.0f, 0.0f),
		vec3(-1.0f, 0.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f),
		vec3(0.0f, 0.0f, -1.0f),
	};

	static const vec3 ups[6] = {
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 0.0f, -1.0f),
		vec3(0.0f, 0.0f, +1.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
	};

	view = mat4_cast(look_at(dirs[face], ups[face])) * translate(-center);
	proj = scale(vec3(-1.0f, 1.0f, 1.0f)) * projection(0.5f * pi<float>(), 1.0f, znear, zfar);
}

vec3 PositionalSampler::sample(unsigned index, float l, float) const
{
	if (l == 0.0f)
		return values[index];
	assert(index + 1 < values.size());
	return mix(values[index], values[index + 1], l);
}

template <typename T>
static T compute_cubic_spline(const std::vector<T> &values, unsigned index, float t, float dt)
{
	assert(3 * index + 4 < values.size());
	T p0 = values[3 * index + 1];

	// For t == 0.0f, the result must be exactly on the point as specified by glTF.
	if (t == 0.0f)
		return p0;

	T m0 = dt * values[3 * index + 2];
	T m1 = dt * values[3 * index + 3];
	T p1 = values[3 * index + 4];

	float t2 = t * t;
	float t3 = t2 * t;

	return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 +
	       (t3 - 2.0f * t2 + t) * m0 +
	       (-2.0f * t3 + 3.0f * t2) * p1 +
	       (t3 - t2) * m1;
}

vec3 PositionalSampler::sample_spline(unsigned index, float t, float dt) const
{
	return compute_cubic_spline(values, index, t, dt);
}

quat SphericalSampler::sample(unsigned index, float l, float) const
{
	if (l == 0.0f)
		return quat(values[index]);
	assert(index + 1 < values.size());
	return slerp(quat(values[index]), quat(values[index + 1]), l);
}

quat SphericalSampler::sample_spline(unsigned index, float t, float dt) const
{
	// CUBICSPLINE for quaternion is defined as simple vec4 interpolation with normalization.
	return normalize(quat(compute_cubic_spline(values, index, t, dt)));
}

// From Shoemake (1985), Bezier by recursive slerps.
// Slow implementation, only used for precomputing a high frame rate animation clip.
static quat quat_bisect(const quat &p, const quat &q)
{
	return quat(normalize(p.as_vec4() + q.as_vec4()));
}

static quat quat_mirror(const quat &p, const quat &q)
{
	float pq = dot(p.as_vec4(), q.as_vec4());
	return quat((2.0f * pq) * q.as_vec4() - p.as_vec4());
}

static quat quat_log(const quat &q)
{
	vec3 v = q.as_vec4().xyz();
	float ac = acos(q.w);
	if (ac > 0.9999f)
		return quat(0.0f, 0.0f, 0.0f, 0.0f);
	else
		return quat(0.0f, normalize(v) * ac);
}

static quat quat_exp(const quat &q)
{
	vec4 v4 = q.as_vec4();
	float l = dot(v4, v4);
	if (l < 0.00000001f)
		return quat(1.0f, 0.0f, 0.0f, 0.0f);
	else
	{
		float vlen = length(v4.xyz());
		vec3 v = normalize(v4.xyz()) * sin(vlen);
		return quat(cos(vlen), v);
	}
}

static quat compute_inner_control_point(const std::vector<vec4> &values, unsigned index)
{
	quat q0 = quat(values[max(int(index) - 1, 0)]);
	quat q1 = quat(values[index]);
	quat q2 = quat(values[min<unsigned>(index + 1, values.size() - 1)]);
#if 0

	quat dbl = quat_mirror(q0, q1);
	quat cp = quat_bisect(dbl, q2);
	return cp;
#else
	quat inv_q1 = conjugate(q1);
	quat exp_value = quat_exp(quat(-0.25f * (quat_log(inv_q1 * q2).as_vec4() + quat_log(inv_q1 * q0).as_vec4())));
	return q1 * exp_value;
#endif
}

quat SphericalSampler::sample_squad(unsigned index, float l, float) const
{
	if (l == 0.0f)
		return quat(values[index]);

	assert(index + 1 < values.size());

	quat q0 = quat(values[index]);
	quat q1 = quat(values[index + 1]);

	quat cp0 = compute_inner_control_point(values, index);
	quat cp1 = compute_inner_control_point(values, index + 1);
	//cp1 = quat_mirror(cp1, q1);

#if 0
	quat l0 = slerp_no_invert(q0, cp0, l);
	quat l1 = slerp_no_invert(cp0, cp1, l);
	quat l2 = slerp_no_invert(cp1, q1, l);
	quat ll0 = slerp_no_invert(l0, l1, l);
	quat ll1 = slerp_no_invert(l1, l2, l);
	return slerp_no_invert(ll0, ll1, l);
#else
	return slerp_no_invert(slerp_no_invert(q0, q1, l), slerp_no_invert(cp0, cp1, l), 2.0f * l * (1.0f - l));
#endif
}
}