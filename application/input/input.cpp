/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "input.hpp"
#include "event.hpp"
#include "muglm/muglm_impl.hpp"
#include "logging.hpp"
#include <algorithm>
#include <string.h>

using namespace Util;

namespace Granite
{
const char *joypad_key_to_tag(JoypadKey key)
{
#define D(k) case JoypadKey::k: return #k
	switch (key)
	{
	D(Left);
	D(Right);
	D(Up);
	D(Down);
	D(LeftShoulder);
	D(RightShoulder);
	D(West);
	D(East);
	D(North);
	D(South);
	D(LeftThumb);
	D(RightThumb);
	D(Mode);
	D(Start);
	D(Select);
	default:
		return "Unknown";
	}
#undef D
}

const char *joypad_axis_to_tag(JoypadAxis axis)
{
#define D(k) case JoypadAxis::k: return #k
	switch (axis)
	{
	D(LeftX);
	D(LeftY);
	D(RightX);
	D(RightY);
	D(LeftTrigger);
	D(RightTrigger);
	default:
		return "Unknown";
	}
#undef D
}

void InputTracker::orientation_event(quat rot)
{
	OrientationEvent event(rot);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::on_touch_down(unsigned id, float x, float y)
{
	if (touch.active_pointers >= TouchCount)
	{
		LOGE("Touch pointer overflow!\n");
		return;
	}

	unsigned index = touch.active_pointers++;
	auto &pointer = touch.pointers[index];
	pointer.id = id;
	pointer.start_x = x;
	pointer.start_y = y;
	pointer.last_x = x;
	pointer.last_y = y;
	pointer.x = x;
	pointer.y = y;

	TouchDownEvent event(index, id, x, y, touch.width, touch.height);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::dispatch_touch_gesture()
{
	TouchGestureEvent event(touch);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::on_touch_move(unsigned id, float x, float y)
{
	auto &pointers = touch.pointers;
	auto itr = std::find_if(std::begin(pointers), std::begin(pointers) + touch.active_pointers, [id](const TouchState::Pointer &pointer) {
		return pointer.id == id;
	});

	if (itr == std::end(pointers))
	{
		LOGE("Could not find pointer!\n");
		return;
	}

	itr->x = x;
	itr->y = y;
}

void InputTracker::on_touch_up(unsigned id, float x, float y)
{
	auto &pointers = touch.pointers;
	auto itr = std::find_if(std::begin(pointers), std::begin(pointers) + touch.active_pointers, [id](const TouchState::Pointer &pointer) {
		return pointer.id == id;
	});

	if (itr == std::end(pointers))
	{
		LOGE("Could not find pointer!\n");
		return;
	}

	auto index = itr - std::begin(pointers);

	TouchUpEvent event(itr->id, x, y, itr->start_x, itr->start_y, touch.width, touch.height);

	if (handler)
		handler->dispatch(event);

	memmove(&pointers[index], &pointers[index + 1], (TouchCount - (index + 1)) * sizeof(TouchState::Pointer));
	touch.active_pointers--;
}

void InputTracker::joypad_key_state(unsigned index, JoypadKey key, JoypadKeyState state)
{
	if (index >= Joypads)
		return;

	assert(active_joypads & (1u << index));

	auto &joy = joypads[index];
	unsigned key_index = Util::ecast(key);
	if (state == JoypadKeyState::Pressed)
	{
		if ((joy.button_mask & key_index) == 0)
		{
			JoypadButtonEvent event(index, key, state);
			if (handler)
				handler->dispatch(event);
		}
		joy.button_mask |= 1u << key_index;
	}
	else if (state == JoypadKeyState::Released)
	{
		if ((joy.button_mask & key_index) == 1)
		{
			JoypadButtonEvent event(index, key, state);
			if (handler)
				handler->dispatch(event);
		}
		joy.button_mask &= ~(1u << key_index);
	}
}

void JoypadState::snap_deadzone(float deadzone)
{
	memcpy(snapped_axis, raw_axis, sizeof(raw_axis));

	static const JoypadAxis fused_axes[2][2] = {
		{ JoypadAxis::LeftX, JoypadAxis::LeftY },
		{ JoypadAxis::RightX, JoypadAxis::RightY },
	};

	for (auto &fused : fused_axes)
	{
		if (std::abs(raw_axis[int(fused[0])]) < deadzone && std::abs(raw_axis[int(fused[1])]) < deadzone)
			for (auto &axis : fused)
				snapped_axis[int(axis)] = 0.0f;
	}
}

void InputTracker::joyaxis_state(unsigned index, JoypadAxis axis, float value)
{
	if (index >= Joypads)
		return;

	assert(active_joypads & (1u << index));

	auto &joy = joypads[index];
	unsigned axis_index = Util::ecast(axis);
	auto &a = joy.raw_axis[axis_index];
	if (a != value)
	{
		JoypadAxisEvent event(index, axis, value);
		if (handler)
			handler->dispatch(event);
	}

	a = value;
}

void InputTracker::key_event(Key key, KeyState state)
{
	if (state == KeyState::Released)
		key_state &= ~(1ull << ecast(key));
	else if (state == KeyState::Pressed)
		key_state |= 1ull << ecast(key);

	KeyboardEvent event(key, state);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::mouse_button_event(Granite::MouseButton button, bool pressed)
{
	mouse_button_event(button, last_mouse_x, last_mouse_y, pressed);
}

void InputTracker::mouse_button_event(MouseButton button, double x, double y, bool pressed)
{
	if (pressed)
		mouse_button_state |= 1ull << ecast(button);
	else
		mouse_button_state &= ~(1ull << ecast(button));

	if (mouse_active)
	{
		last_mouse_x = x;
		last_mouse_y = y;
	}

	MouseButtonEvent event(button, x, y, pressed);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::mouse_move_event_relative(double x, double y)
{
	x *= mouse_speed_x;
	y *= mouse_speed_y;
	if (mouse_active)
	{
		last_mouse_x += x;
		last_mouse_y += y;
		last_mouse_x = clamp(last_mouse_x, mouse_relative_range_x,
		                     mouse_relative_range_x + mouse_relative_range_width);
		last_mouse_y = clamp(last_mouse_y, mouse_relative_range_y,
		                     mouse_relative_range_y + mouse_relative_range_height);
		MouseMoveEvent event(x, y, last_mouse_x, last_mouse_y, key_state, mouse_button_state);
		if (handler)
			handler->dispatch(event);
	}
}

void InputTracker::mouse_move_event_absolute(double x, double y)
{
	if (mouse_active)
	{
		double delta_x = x - last_mouse_x;
		double delta_y = y - last_mouse_y;
		last_mouse_x = x;
		last_mouse_y = y;
		MouseMoveEvent event(delta_x, delta_y, x, y, key_state, mouse_button_state);
		if (handler)
			handler->dispatch(event);
	}
}

void InputTracker::mouse_move_event_absolute_normalized(double x, double y)
{
	mouse_move_event_absolute(x * double(touch.width), y * double(touch.height));
}

void InputTracker::mouse_button_event_normalized(MouseButton button, double x, double y, bool pressed)
{
	mouse_button_event(button, x * double(touch.width), y * double(touch.height), pressed);
}

void InputTracker::mouse_enter(double x, double y)
{
	mouse_active = true;
	last_mouse_x = x;
	last_mouse_y = y;
}

void InputTracker::mouse_leave()
{
	mouse_active = false;
}

void InputTracker::dispatch_current_state(double delta_time, InputTrackerHandler *override_handler)
{
	if (!override_handler)
		override_handler = handler;

	if (override_handler)
	{
		for (auto &pad : joypads)
			pad.snap_deadzone(axis_deadzone);

		override_handler->dispatch(JoypadStateEvent{active_joypads, joypads, Joypads, delta_time});
		override_handler->dispatch(InputStateEvent{last_mouse_x, last_mouse_y,
		                                           delta_time, key_state, mouse_button_state, mouse_active});
	}
}

int InputTracker::find_vacant_joypad_index() const
{
	for (int i = 0; i < Joypads; i++)
	{
		if ((active_joypads & (1 << i)) == 0)
			return i;
	}

	return -1;
}

void InputTracker::enable_joypad(unsigned index, uint32_t vid, uint32_t pid)
{
	if (index >= Joypads)
		return;

	if (active_joypads & (1u << index))
		return;

	active_joypads |= 1u << index;
	joypads[index] = {};
	joypads[index].vid = vid;
	joypads[index].pid = pid;
	JoypadConnectionEvent event(index, true, vid, pid);
	if (handler)
		handler->dispatch(event);
}

void InputTracker::disable_joypad(unsigned index, uint32_t vid, uint32_t pid)
{
	if (index >= Joypads)
		return;

	if ((active_joypads & (1u << index)) == 0)
		return;

	active_joypads &= ~(1u << index);
	joypads[index] = {};
	JoypadConnectionEvent event(index, false, vid, pid);
	if (handler)
		handler->dispatch(event);
}

std::mutex &InputTracker::get_lock()
{
	return dispatch_lock;
}
}
