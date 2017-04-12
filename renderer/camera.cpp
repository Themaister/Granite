#include "camera.hpp"

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
	return glm::translate(-position) * mat4_cast(rotation);
}

mat4 Camera::get_projection() const
{
	return glm::perspective(fovy, aspect, znear, zfar);
}

vec3 Camera::get_front() const
{
	auto d = get_view()[2].xyz();
	return -d;
}

vec3 Camera::get_right() const
{
	auto d = get_view()[0].xyz();
	return d;
}

vec3 Camera::get_up() const
{
	auto d = get_view()[1].xyz();
	return d;
}
}