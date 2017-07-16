#pragma once

#include "math.hpp"
#include <vector>

namespace Granite
{
class AABB;

bool compute_plane_reflection(mat4 &projection, mat4 &view, vec3 camera_pos, vec3 center, vec3 normal, vec3 look_up,
                              float radius_x, float radius_z, float &z_near, float z_far);

void compute_model_transform(mat4 &world, vec3 scale, quat rotation, vec3 translation, const mat4 &parent);

void compute_normal_transform(mat4 &normal, const mat4 &world);

quat rotate_vector(vec3 from, vec3 to);

quat look_at(vec3 direction, vec3 up);

quat rotate_vector_axis(vec3 from, vec3 to, vec3 axis);

mat4 projection(float fovy, float aspect, float znear, float zfar);

mat4 ortho(const AABB &aabb);

struct LinearSampler
{
	std::vector<vec3> values;
	vec3 sample(unsigned index, float l) const;
};

struct SlerpSampler
{
	std::vector<quat> values;
	quat sample(unsigned index, float l) const;
};
}