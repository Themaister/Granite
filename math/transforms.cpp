#include "transforms.hpp"

namespace Granite
{
void compute_model_transform(mat4 &world, mat4 &normal, vec3 scale, quat rotation, vec3 translation, const mat4 &parent)
{
	mat4 S = glm::scale(scale);
	mat4 R = mat4_cast(rotation);
	mat4 T = glm::translate(translation);

	mat4 model = R * S;
	world = parent * T * model;
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
	float cos_half_range = max(dot(half_vector, from), 0.0f);
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

	vec3 half_vector = normalize(from + to);
	float cos_half_range = max(dot(half_vector, from), 0.0f);
	float sin_half_angle = sqrtf(1.0f - cos_half_range * cos_half_range);
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

mat4 projection(float fovy, float aspect, float znear, float zfar)
{
	return glm::scale(vec3(1.0f, -1.0f, 1.0f)) * glm::perspective(fovy, aspect, znear, zfar);
}

vec3 LinearSampler::sample(float t) const
{
	assert(timestamps.size() == values.size());
	if (t >= get_length())
		return values.back();
	else if (t <= timestamps.front())
		return values.front();

	unsigned index = 0;
	while (t > timestamps[index])
		index++;

	assert(index > 0);
	assert(index < timestamps.size());
	unsigned prev_index = index - 1;

	float l = (t - timestamps[prev_index]) / (timestamps[index] - timestamps[prev_index]);
	return values[prev_index] * (1.0f - l) + l * values[index];
}

quat SlerpSampler::sample(float t) const
{
	assert(timestamps.size() == values.size());
	if (t >= get_length())
		return values.back();
	else if (t <= timestamps.front())
		return values.front();

	unsigned index = 0;
	while (t > timestamps[index])
		index++;

	assert(index > 0);
	assert(index < timestamps.size());
	unsigned prev_index = index - 1;

	float l = (t - timestamps[prev_index]) / (timestamps[index] - timestamps[prev_index]);
	return slerp(values[prev_index], values[index], l);
}
}