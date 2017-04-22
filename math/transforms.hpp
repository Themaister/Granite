#pragma once

#include "math.hpp"
#include <vector>

namespace Granite
{
void
compute_model_transform(mat4 &world, mat4 &normal, vec3 scale, quat rotation, vec3 translation, const mat4 &parent);

quat rotate_vector(vec3 from, vec3 to);

quat look_at(vec3 direction, vec3 up);

quat rotate_vector_axis(vec3 from, vec3 to, vec3 axis);

mat4 projection(float fovy, float aspect, float znear, float zfar);

struct LinearSampler
{
	std::vector<float> timestamps;
	std::vector<vec3> values;

	bool is_valid() const
	{
		return !timestamps.empty();
	}

	float get_length() const
	{
		return timestamps.empty() ? 0.0f : timestamps.back();
	}

	vec3 sample(float t) const;
};

struct SlerpSampler
{
	std::vector<float> timestamps;
	std::vector<quat> values;

	bool is_valid() const
	{
		return !timestamps.empty();
	}

	float get_length() const
	{
		return timestamps.empty() ? 0.0f : timestamps.back();
	}

	quat sample(float t) const;
};
}