#include "transforms.hpp"
#include "aabb.hpp"

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

	view = mat4_cast(look_at(normal, look_up)) * glm::translate(-camera_pos);

	float dist_x = dot(look_pos_x, center - camera_pos);
	float left = dist_x - radius_other;
	float right = dist_x + radius_other;

	float dist_y = dot(look_up, center - camera_pos);
	float bottom = dist_y - radius_up;
	float top = dist_y + radius_up;

	z_near = over_plane;
	projection = glm::scale(vec3(1.0f, -1.0f, 1.0f)) * glm::frustum(left, right, bottom, top, over_plane, z_far);
	if (z_near >= z_far)
		return false;
	return true;
}

void compute_model_transform(mat4 &world, vec3 scale, quat rotation, vec3 translation, const mat4 &parent)
{
	mat4 S = glm::scale(scale);
	mat4 R = mat4_cast(rotation);
	mat4 T = glm::translate(translation);

	mat4 model = R * S;
	world = parent * T * model;
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

mat4 projection(float fovy, float aspect, float znear, float zfar)
{
	return glm::scale(vec3(1.0f, -1.0f, 1.0f)) * glm::perspective(fovy, aspect, znear, zfar);
}

mat4 ortho(const AABB &aabb)
{
	vec3 min = aabb.get_minimum();
	vec3 max = aabb.get_maximum();
	return glm::scale(vec3(1.0f, -1.0f, 1.0f)) * glm::ortho(min.x, max.x, min.y, max.y, min.z, max.z);
}

vec3 LinearSampler::sample(unsigned index, float l) const
{
	if (l == 0.0f)
		return values[index];
	return values[index] * (1.0f - l) + l * values[index + 1];
}

quat SlerpSampler::sample(unsigned index, float l) const
{
	if (l == 0.0f)
		return values[index];
	return slerp(values[index], values[index + 1], l);
}
}