#pragma once

#include "math.hpp"

namespace Granite
{
class Camera
{
public:
	mat4 get_projection() const;
	mat4 get_view() const;

	void set_depth_range(float znear, float zfar);
	void set_fovy(float fovy);
	void set_aspect(float aspect);

	vec3 get_front() const;
	vec3 get_right() const;
	vec3 get_up() const;
	const quat &get_rotation() const
	{
		return rotation;
	}

	void set_position(const vec3 &pos);
	void set_rotation(const quat &rot);
	void look_at(const vec3 &eye, const vec3 &at, const vec3 &up = vec3(0.0f, 1.0f, 0.0f));

	float get_znear() const
	{
		return znear;
	}

	float get_zfar() const
	{
		return zfar;
	}

private:
	vec3 position = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
	float fovy = glm::half_pi<float>();
	float aspect = 16.0f / 9.0f;
	float znear = 1.0f;
	float zfar = 1000.0f;
};
}