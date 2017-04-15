#pragma once

#include "enum_cast.hpp"
#include <stdint.h>

namespace Vulkan
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
	void dispatch_current_state();

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

}