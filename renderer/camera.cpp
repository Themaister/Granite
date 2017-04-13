#include "camera.hpp"
#include "transforms.hpp"

namespace Granite
{
void Camera::set_depth_range(float znear, float zfar)
{
	this->znear = znear;
	this->zfar = zfar;
}

void Camera::set_aspect(float aspect)
{
	this->aspect = aspect;
}

void Camera::set_fovy(float fovy)
{
	this->fovy = fovy;
}

mat4 Camera::get_view() const
{
	return mat4_cast(rotation) * glm::translate(-position);
}

void Camera::set_position(const vec3 &pos)
{
	position = pos;
}

void Camera::set_rotation(const quat &rot)
{
	rotation = rot;
}

void Camera::look_at(const vec3 &eye, const vec3 &at, const vec3 &up)
{
	position = eye;
	rotation = ::Granite::look_at(at - eye, up);
}

mat4 Camera::get_projection() const
{
	return projection(fovy, aspect, znear, zfar);
}

vec3 Camera::get_front() const
{
	static const vec3 z(0.0f, 0.0f, -1.0f);
	return conjugate(rotation) * z;
}

vec3 Camera::get_right() const
{
	static const vec3 right(1.0f, 0.0f, 0.0f);
	return conjugate(rotation) * right;
}

vec3 Camera::get_up() const
{
	static const vec3 up(0.0f, 1.0f, 0.0f);
	return conjugate(rotation) * up;
}
}