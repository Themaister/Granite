#include "camera.hpp"
#include "transforms.hpp"
#include "vulkan_events.hpp"
#include "input.hpp"

using namespace Vulkan;

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

FPSCamera::FPSCamera()
{
	EventManager::get_global().register_handler(MouseMoveEvent::type_id, &FPSCamera::on_mouse_move, this);
	EventManager::get_global().register_handler(InputStateEvent::type_id, &FPSCamera::on_input_state, this);
	EventManager::get_global().register_latch_handler(SwapchainParameterEvent::type_id, &FPSCamera::on_swapchain, &FPSCamera::on_swapchain, this);
}

void FPSCamera::on_swapchain(const Event &e)
{
	auto &state = e.as<SwapchainParameterEvent>();
	set_aspect(state.get_aspect_ratio());
}

bool FPSCamera::on_input_state(const Event &e)
{
	auto &state = e.as<InputStateEvent>();

	if (state.get_key_pressed(Key::W))
		position += 3.0f * get_front() * float(state.get_delta_time());
	else if (state.get_key_pressed(Key::S))
		position -= 3.0f * get_front() * float(state.get_delta_time());
	if (state.get_key_pressed(Key::D))
		position += 3.0f * get_right() * float(state.get_delta_time());
	else if (state.get_key_pressed(Key::A))
		position -= 3.0f * get_right() * float(state.get_delta_time());
	return true;
}

bool FPSCamera::on_mouse_move(const Event &e)
{
	auto &m = e.as<MouseMoveEvent>();
	if (!m.get_mouse_button_pressed(MouseButton::Right))
		return true;

	auto dx = float(m.get_delta_x());
	auto dy = float(m.get_delta_y());
	quat pitch = angleAxis(dy * 0.02f, vec3(1.0f, 0.0f, 0.0f));
	quat yaw = angleAxis(dx * 0.02f, vec3(0.0f, 1.0f, 0.0f));
	rotation = normalize(pitch * rotation * yaw);

	return true;
}
}