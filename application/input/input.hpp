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
	East,
	South,
	West,
	North,
	LeftShoulder,
	RightShoulder,
	LeftThumb,
	RightThumb,
	Start,
	Select,
	Count,
	Unknown
};

enum class JoypadAxis
{
	LeftX,
	LeftY,
	RightX,
	RightY,
	LeftTrigger,
	RightTrigger,
	Count,
	Unknown
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
	Left, Right, Up, Down,
	_1, _2, _3, _4, _5, _6, _7, _8, _9, _0,
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

	unsigned width;
	unsigned height;
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

	float axis[Util::ecast(JoypadAxis::Count)] = {};
	uint32_t button_mask = 0;
};
static_assert(Util::ecast(JoypadKey::Count) <= 32, "Cannot have more than 32 joypad buttons.");

class InputTracker;

class JoypadRemapper
{
public:
	struct ButtonMap : Util::IntrusiveHashMapEnabled<ButtonMap>
	{
		JoypadKey key;
		JoypadAxis axis;
	};

	struct AxisMap : Util::IntrusiveHashMapEnabled<AxisMap>
	{
		JoypadAxis axis;
		JoypadKey neg_edge;
		JoypadKey pos_edge;
		float axis_mod;
	};

	const ButtonMap *map_button(unsigned code) const;
	const AxisMap *map_axis(unsigned code) const;

	void register_button(unsigned code, JoypadKey key, JoypadAxis axis);
	void register_axis(unsigned code,
	                   JoypadAxis axis, float axis_mod,
	                   JoypadKey neg_edge, JoypadKey pos_edge);

	void button_event(InputTracker &tracker, unsigned index, unsigned code, bool pressed);
	void axis_event(InputTracker &tracker, unsigned index, unsigned code, float value);

	void reset();

private:
	Util::IntrusiveHashMap<ButtonMap> button_map;
	Util::IntrusiveHashMap<AxisMap> axis_map;
};

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

	void joypad_key_state_raw(unsigned index, unsigned code, bool pressed);
	void joyaxis_state_raw(unsigned index, unsigned code, float value);

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

	void enable_joypad(unsigned index);
	void disable_joypad(unsigned index);
	int find_vacant_joypad_index() const;

	void set_touch_resolution(unsigned width, unsigned height)
	{
		touch.width = width;
		touch.height = height;
	}

	JoypadRemapper &get_joypad_remapper(unsigned index)
	{
		assert(index < Joypads);
		return remappers[index];
	}

	enum { TouchCount = 16 };
	enum { Joypads = 8 };

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

	uint8_t active_joypads = 0;
	JoypadState joypads[Joypads] = {};
	JoypadRemapper remappers[Joypads];
	TouchState touch;

	float axis_deadzone = 0.3f;
};
class JoypadConnectionEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(JoypadConnectionEvent)
	JoypadConnectionEvent(unsigned index_, bool connected_)
	    : index(index_), connected(connected_)
	{
	}

	unsigned get_index() const
	{
		return index;
	}

	bool is_connected() const
	{
		return connected;
	}

private:
	unsigned index;
	bool connected;
};

class TouchGestureEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(TouchGestureEvent)

	explicit TouchGestureEvent(const TouchState &state_)
		: state(state_)
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

	TouchDownEvent(unsigned index_, unsigned id_,
	               float x_, float y_,
	               unsigned screen_width_, unsigned screen_height_)
		: index(index_), id(id_),
		  x(x_), y(y_),
		  width(screen_width_), height(screen_height_)
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

	unsigned get_screen_width() const
	{
		return width;
	}

	unsigned get_screen_height() const
	{
		return height;
	}

private:
	unsigned index, id;
	float x, y;
	unsigned width, height;
};

class TouchUpEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(TouchUpEvent)

	TouchUpEvent(unsigned id_, float x_, float y_,
	             float start_x_, float start_y_,
	             unsigned screen_width_, unsigned screen_height_)
		: id(id_), x(x_), y(y_),
		  start_x(start_x_), start_y(start_y_),
		  width(screen_width_), height(screen_height_)
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

	unsigned get_screen_width() const
	{
		return width;
	}

	unsigned get_screen_height() const
	{
		return height;
	}

private:
	unsigned id;
	float x, y;
	float start_x, start_y;
	unsigned width, height;
};

class JoypadButtonEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(JoypadButtonEvent)

	JoypadButtonEvent(unsigned index_, JoypadKey key_, JoypadKeyState state_)
		: index(index_), key(key_), state(state_)
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

	JoypadAxisEvent(unsigned index_, JoypadAxis axis_, float value_)
		: index(index_), axis(axis_), value(value_)
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

	KeyboardEvent(Key key_, KeyState state_)
		: key(key_), state(state_)
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
	explicit OrientationEvent(const quat &rot_)
		: rot(rot_)
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

	MouseButtonEvent(MouseButton button_, double abs_x_, double abs_y_, bool pressed_)
		: button(button_), abs_x(abs_x_), abs_y(abs_y_), pressed(pressed_)
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

	MouseMoveEvent(double delta_x_, double delta_y_, double abs_x_, double abs_y_,
	               uint64_t key_mask_, uint8_t btn_mask_)
		: delta_x(delta_x_), delta_y(delta_y_),
		  abs_x(abs_x_), abs_y(abs_y_),
		  key_mask(key_mask_), btn_mask(btn_mask_)
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

	JoypadStateEvent(uint8_t active_mask_, const JoypadState *states_,
	                 unsigned count_, double delta_time_)
		: states(states_), count(count_), delta_time(delta_time_), active_mask(active_mask_)
	{
	}

	bool is_connected(unsigned index) const
	{
		if (index >= count)
			return false;
		return (active_mask & (1u << index)) != 0;
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
	uint8_t active_mask;
};

class InputStateEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(InputStateEvent)

	InputStateEvent(double abs_x_, double abs_y_,
	                double delta_time_, uint64_t key_mask_, uint8_t btn_mask_, bool mouse_active_)
		: abs_x(abs_x_), abs_y(abs_y_),
		  delta_time(delta_time_), key_mask(key_mask_),
		  btn_mask(btn_mask_), mouse_active(mouse_active_)
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
