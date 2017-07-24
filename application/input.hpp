/* Copyright (c) 2017 Hans-Kristian Arntzen
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

namespace Granite
{
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

class InputTracker
{
public:
	void key_event(Key key, KeyState state);
	void mouse_button_event(MouseButton button, bool pressed);
	void mouse_move_event(double x, double y);
	void dispatch_current_state(double delta_time);
	void orientation_event(quat rot);

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

private:
	uint64_t key_state = 0;
	uint8_t mouse_button_state = 0;
	bool mouse_active = false;

	double last_mouse_x = 0.0;
	double last_mouse_y = 0.0;

	enum { TouchCount = 16 };
	TouchState touch;
};

class TouchGestureEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(TouchGestureEvent);

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
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(TouchDownEvent);

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
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(TouchUpEvent);

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

class KeyboardEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(KeyboardEvent);

	KeyboardEvent(Key key, KeyState state)
		: Granite::Event(type_id), key(key), state(state)
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
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(OrientationEvent);
	OrientationEvent(quat rot)
		: Granite::Event(type_id), rot(rot)
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
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(MouseButtonEvent);

	MouseButtonEvent(MouseButton button, bool pressed)
		: Granite::Event(type_id), button(button), pressed(pressed)
	{
	}

	MouseButton get_button() const
	{
		return button;
	}

	bool get_pressed() const
	{
		return pressed;
	}

private:
	MouseButton button;
	bool pressed;
};

class MouseMoveEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(MouseMoveEvent);

	MouseMoveEvent(double delta_x, double delta_y, double abs_x, double abs_y,
	               uint64_t key_mask, uint8_t btn_mask)
		: Granite::Event(type_id),
		  delta_x(delta_x), delta_y(delta_y), abs_x(abs_x), abs_y(abs_y), key_mask(key_mask), btn_mask(btn_mask)
	{
	}

	bool get_mouse_button_pressed(MouseButton button) const
	{
		return (btn_mask & (1 << Util::ecast(button))) != 0;
	}

	bool get_key_pressed(Key key) const
	{
		return (key_mask & (1 << Util::ecast(key))) != 0;
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

class InputStateEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(InputStateEvent);

	InputStateEvent(double abs_x, double abs_y, double delta_time, uint64_t key_mask, uint8_t btn_mask, bool mouse_active)
		: Granite::Event(type_id), abs_x(abs_x), abs_y(abs_y), delta_time(delta_time), key_mask(key_mask), btn_mask(btn_mask), mouse_active(mouse_active)
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
		return (key_mask & (1 << Util::ecast(key))) != 0;
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