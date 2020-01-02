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
#include "event.hpp"
#include "application_wsi_events.hpp"
#include "input.hpp"

namespace Granite
{
class Camera
{
public:
	virtual ~Camera() = default;

	mat4 get_projection() const;

	mat4 get_view() const;

	void set_depth_range(float znear, float zfar);

	void set_ortho(bool enable = true, float height = 0.0f);

	void set_fovy(float fovy);

	float get_fovy() const
	{
		return fovy;
	}

	void set_aspect(float aspect);

	float get_aspect() const
	{
		return aspect;
	}

	vec3 get_front() const;

	vec3 get_right() const;

	vec3 get_up() const;

	vec3 get_position() const;

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

	bool get_ortho() const
	{
		return ortho;
	}

	float get_ortho_height() const
	{
		return ortho_height;
	}

	void set_transform(const mat4 &m);

protected:
	vec3 position = vec3(0.0f);
	quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
	float fovy = 0.5f * half_pi<float>();
	float aspect = 16.0f / 9.0f;
	float znear = 1.0f;
	float zfar = 1000.0f;
	float transform_z_scale = 1.0f;
	bool ortho = false;
	float ortho_height = 0.0f;
};

class FPSCamera : public Camera, public EventHandler
{
public:
	FPSCamera();
private:
	bool on_mouse_move(const MouseMoveEvent &e);
	bool on_input_state(const InputStateEvent &e);
	bool on_orientation(const OrientationEvent &e);
	bool on_touch_down(const TouchDownEvent &e);
	bool on_touch_up(const TouchUpEvent &e);
	void on_swapchain(const Vulkan::SwapchainParameterEvent &e);
	bool on_joypad_state(const JoypadStateEvent &e);
	bool on_joy_button(const JoypadButtonEvent &e);
	bool on_joy_axis(const JoypadAxisEvent &e);

	unsigned pointer_count = 0;
	bool ignore_orientation = false;
};
}
