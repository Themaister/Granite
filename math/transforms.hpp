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

#pragma once

#include "math.hpp"
#include <vector>

namespace Granite
{
class AABB;

bool compute_plane_reflection(mat4 &projection, mat4 &view, vec3 camera_pos, vec3 center, vec3 normal, vec3 look_up,
                              float radius_up, float radius_other, float &z_near, float z_far);

bool compute_plane_refraction(mat4 &projection, mat4 &view, vec3 camera_pos, vec3 center, vec3 normal, vec3 look_up,
                              float radius_up, float radius_other, float &z_near, float z_far);

void compute_model_transform(mat4 &world, vec3 scale, quat rotation, vec3 translation, const mat4 &parent);

void compute_normal_transform(mat4 &normal, const mat4 &world);

quat rotate_vector(vec3 from, vec3 to);

quat look_at(vec3 direction, vec3 up);

quat look_at_arbitrary_up(vec3 direction);

quat rotate_vector_axis(vec3 from, vec3 to, vec3 axis);

mat4 projection(float fovy, float aspect, float znear, float zfar);

mat4 ortho(const AABB &aabb);

void compute_cube_render_transform(vec3 center, unsigned face, mat4 &projection, mat4 &view, float znear, float zfar);

struct LinearSampler
{
	std::vector<vec3> values;
	vec3 sample(unsigned index, float l, float dt) const;
};

struct CubicSampler
{
	std::vector<vec3> values;
	vec3 sample(unsigned index, float l, float dt) const;
};

struct SlerpSampler
{
	std::vector<quat> values;
	quat sample(unsigned index, float l, float dt) const;
};
}