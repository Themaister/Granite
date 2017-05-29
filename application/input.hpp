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

class InputTracker
{
public:
	void key_event(Key key, KeyState state);
	void mouse_button_event(MouseButton button, bool pressed);
	void mouse_move_event(double x, double y);
	void dispatch_current_state(double delta_time);
	void orientation_event(quat rot);

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

private:
	uint64_t key_state = 0;
	uint8_t mouse_button_state = 0;
	bool mouse_active = false;

	double last_mouse_x = 0.0;
	double last_mouse_y = 0.0;
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

class FrameTickEvent : public Granite::Event
{
public:
	static constexpr Granite::EventType type_id = GRANITE_EVENT_TYPE_HASH(FrameTickEvent);

	FrameTickEvent(double frame_time, double elapsed_time)
		: Granite::Event(type_id), frame_time(frame_time), elapsed_time(elapsed_time)
	{
	}

	double get_frame_time() const
	{
		return frame_time;
	}

	double get_elapsed_time() const
	{
		return elapsed_time;
	}

private:
	double frame_time;
	double elapsed_time;
};

}