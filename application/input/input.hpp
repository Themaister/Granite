/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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

#include "enum_cast.hpp"
#include "event.hpp"
#include <stdint.h>
#include "math.hpp"
#include <limits>

namespace Granite
{
enum class JoypadKey
{
	Left,
	Right,
	Up,
	Down,
	A,
	B,
	X,
	Y,
	L1,
	L2,
	L3,
	R1,
	R2,
	R3,
	Start,
	Select,
	Count
};

enum class JoypadAxis
{
	LeftX,
	LeftY,
	RightX,
	RightY,
	Count
};

enum class JoypadKeyState
{
	Pressed,
	Released,
	Count
};

enum class Key
{
	Unknown,
	A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	Return,
	LeftCtrl,
	LeftAlt,
	LeftShift,
	Space,
	Escape,
	Count
};

enum class MouseButton
{
	Left,
	Middle,
	Right,
	Count
};

enum class KeyState
{
	Pressed,
	Released,
	Repeat,
	Count
};
static_assert(Util::ecast(Key::Count) <= 64, "Cannot have more than 64 keys for bit-packing.");

struct TouchState
{
	enum { PointerCount = 16 };
	struct Pointer
	{
		unsigned id;
		float start_x;
		float start_y;
		float last_x;
		float last_y;
		float x;
		float y;
	};
	Pointer pointers[PointerCount] = {};
	unsigned active_pointers = 0;
};

struct JoypadState
{
	bool is_button_pressed(JoypadKey key) const
	{
		return (button_mask & (1u << Util::ecast(key))) != 0;
	}

	float get_axis(JoypadAxis a) const
	{
		return axis[Util::ecast(a)];
	}

	float axis[Util::ecast(JoypadAxis::Count)];
	uint32_t button_mask = 0;
};
static_assert(Util::ecast(JoypadKey::Count) <= 32, "Cannot have more than 32 joypad buttons.");

class InputTracker
{
public:
	void key_event(Key key, KeyState state);
	void mouse_button_event(MouseButton button, double x, double y, bool pressed);
	void mouse_button_event(MouseButton button, bool pressed);
	void mouse_move_event_absolute(double x, double y);
	void mouse_move_event_relative(double x, double y);
	void dispatch_current_state(double delta_time);
	void orientation_event(quat rot);
	void joypad_key_state(unsigned index, JoypadKey key, JoypadKeyState state);
	void joyaxis_state(unsigned index, JoypadAxis axis, float value);

	void on_touch_down(unsigned id, float x, float y);
	void on_touch_move(unsigned id, float x, float y);
	void on_touch_up(unsigned id, float x, float y);

	void mouse_enter(double x, double y);
	void mouse_leave();

	bool key_pressed(Key key) const
	{
		return (key_state & (1ull << Util::ecast(key))) != 0;
	}

	bool mouse_button_pressed(MouseButton button) const
	{
		return (mouse_button_state & (1ull << Util::ecast(button))) != 0;
	}

	void dispatch_touch_gesture();

	void set_axis_deadzone(float deadzone)
	{
		axis_deadzone = deadzone;
	}

	void set_relative_mouse_rect(double x, double y, double width, double height)
	{
		mouse_relative_range_x = x;
		mouse_relative_range_y = y;
		mouse_relative_range_width = width;
		mouse_relative_range_height = height;
	}

	void set_relative_mouse_speed(double speed_x, double speed_y)
	{
		mouse_speed_x = speed_x;
		mouse_speed_y = speed_y;
	}

private:
	uint64_t key_state = 0;
	uint8_t mouse_button_state = 0;
	bool mouse_active = false;

	double last_mouse_x = 0.0;
	double last_mouse_y = 0.0;
	double mouse_relative_range_x = 0.0;
	double mouse_relative_range_y = 0.0;
	double mouse_relative_range_width = std::numeric_limits<double>::max();
	double mouse_relative_range_height = std::numeric_limits<double>::max();
	double mouse_speed_x = 1.0;
	double mouse_speed_y = 1.0;

	enum { TouchCount = 16 };
	enum { Joypads = 8 };

	JoypadState joypads[Joypads] = {};
	TouchState touch;

	float axis_deadzone = 0.3f;
};

class TouchGestureEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(TouchGestureEvent)

	TouchGestureEvent(const TouchState &state)
		: state(state)
	{
	}

	const TouchState &get_state() const
	{
		return state;
	}

private:
	const TouchState &state;
};

class TouchDownEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(TouchDownEvent)

	TouchDownEvent(unsigned index, unsigned id, float x, float y)
		: index(index), id(id), x(x), y(y)
	{
	}

	float get_x() const
	{
		return x;
	}

	float get_y() const
	{
		return y;
	}

	unsigned get_index() const
	{
		return index;
	}

	unsigned get_id() const
	{
		return id;
	}

private:
	unsigned index, id;
	float x, y;
};

class TouchUpEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(TouchUpEvent)

	TouchUpEvent(unsigned id, float x, float y, float start_x, float start_y)
		: id(id), x(x), y(y), start_x(start_x), start_y(start_y)
	{
	}

	float get_x() const
	{
		return x;
	}

	float get_y() const
	{
		return y;
	}

	float get_start_x() const
	{
		return start_x;
	}

	float get_start_y() const
	{
		return start_y;
	}

	unsigned get_id() const
	{
		return id;
	}

private:
	unsigned id;
	float x, y;
	float start_x, start_y;
};

class JoypadButtonEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(JoypadButtonEvent)

	JoypadButtonEvent(unsigned index, JoypadKey key, JoypadKeyState state)
		: index(index), key(key), state(state)
	{
	}

	unsigned get_index() const
	{
		return index;
	}

	JoypadKey get_key() const
	{
		return key;
	}

	JoypadKeyState get_state() const
	{
		return state;
	}

private:
	unsigned index;
	JoypadKey key;
	JoypadKeyState state;
};

class JoypadAxisEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(JoypadAxisEvent)

	JoypadAxisEvent(unsigned index, JoypadAxis axis, float value)
			: index(index), axis(axis), value(value)
	{
	}

	unsigned get_index() const
	{
		return index;
	}

	JoypadAxis get_axis() const
	{
		return axis;
	}

	float get_value() const
	{
		return value;
	}

private:
	unsigned index;
	JoypadAxis axis;
	float value;
};

class KeyboardEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(KeyboardEvent)

	KeyboardEvent(Key key, KeyState state)
		: key(key), state(state)
	{
	}

	Key get_key() const
	{
		return key;
	}

	KeyState get_key_state() const
	{
		return state;
	}

private:
	Key key;
	KeyState state;
};

class OrientationEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(OrientationEvent)
	OrientationEvent(quat rot)
		: rot(rot)
	{
	}

	const quat &get_rotation() const
	{
		return rot;
	}

private:
	quat rot;
};

class MouseButtonEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(MouseButtonEvent)

	MouseButtonEvent(MouseButton button, double abs_x, double abs_y, bool pressed)
		: button(button), abs_x(abs_x), abs_y(abs_y), pressed(pressed)
	{
	}

	MouseButton get_button() const
	{
		return button;
	}

	double get_abs_x() const
	{
		return abs_x;
	}

	double get_abs_y() const
	{
		return abs_y;
	}

	bool get_pressed() const
	{
		return pressed;
	}

private:
	MouseButton button;
	double abs_x;
	double abs_y;
	bool pressed;
};

class MouseMoveEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(MouseMoveEvent);

	MouseMoveEvent(double delta_x, double delta_y, double abs_x, double abs_y,
	               uint64_t key_mask, uint8_t btn_mask)
		: delta_x(delta_x), delta_y(delta_y), abs_x(abs_x), abs_y(abs_y), key_mask(key_mask), btn_mask(btn_mask)
	{
	}

	bool get_mouse_button_pressed(MouseButton button) const
	{
		return (btn_mask & (1 << Util::ecast(button))) != 0;
	}

	bool get_key_pressed(Key key) const
	{
		return (key_mask & (1ull << Util::ecast(key))) != 0;
	}

	double get_delta_x() const
	{
		return delta_x;
	}

	double get_delta_y() const
	{
		return delta_y;
	}

	double get_abs_x() const
	{
		return abs_x;
	}

	double get_abs_y() const
	{
		return abs_y;
	}

private:
	double delta_x, delta_y, abs_x, abs_y;
	uint64_t key_mask;
	uint8_t btn_mask;
};

class JoypadStateEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(JoypadStateEvent)

	JoypadStateEvent(const JoypadState *states, unsigned count, double delta_time)
		: states(states), count(count), delta_time(delta_time)
	{
	}

	unsigned get_num_indices() const
	{
		return count;
	}

	const JoypadState &get_state(unsigned index) const
	{
		return states[index];
	}

	double get_delta_time() const
	{
		return delta_time;
	}

private:
	const JoypadState *states;
	unsigned count;
	double delta_time;
};

class InputStateEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(InputStateEvent)

	InputStateEvent(double abs_x, double abs_y, double delta_time, uint64_t key_mask, uint8_t btn_mask, bool mouse_active)
		: abs_x(abs_x), abs_y(abs_y), delta_time(delta_time), key_mask(key_mask), btn_mask(btn_mask), mouse_active(mouse_active)
	{
	}

	double get_delta_time() const
	{
		return delta_time;
	}

	bool get_mouse_active() const
	{
		return mouse_active;
	}

	bool get_mouse_button_pressed(MouseButton button) const
	{
		return (btn_mask & (1 << Util::ecast(button))) != 0;
	}

	bool get_key_pressed(Key key) const
	{
		return (key_mask & (1ull << Util::ecast(key))) != 0;
	}

	double get_mouse_x() const
	{
		return abs_x;
	}

	double get_mouse_y() const
	{
		return abs_y;
	}

private:
	double abs_x, abs_y;
	double delta_time;
	uint64_t key_mask;
	uint8_t btn_mask;
	bool mouse_active;
};


}