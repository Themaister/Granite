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

	void set_position(const vec3 &pos);
	void set_rotation(const quat &rot);
	void look_at(const vec3 &eye, const vec3 &at, const vec3 &up);

private:
	vec3 position;
	quat rotation;
	float fovy;
	float aspect;
	float znear;
	float zfar;
};
}