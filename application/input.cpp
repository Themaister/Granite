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

#include "input.hpp"
#include "vulkan_events.hpp"
#include "event.hpp"
#include <algorithm>
#include <string.h>
#include <ui_manager.hpp>

using namespace Util;
using namespace std;

namespace Granite
{
void InputTracker::orientation_event(quat rot)
{
	OrientationEvent event(rot);
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
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

	TouchDownEvent event(index, id, x, y);
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
}

void InputTracker::dispatch_touch_gesture()
{
	TouchGestureEvent event(touch);
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
}

void InputTracker::on_touch_move(unsigned id, float x, float y)
{
	auto &pointers = touch.pointers;
	auto itr = find_if(begin(pointers), begin(pointers) + touch.active_pointers, [id](const TouchState::Pointer &pointer) {
		return pointer.id == id;
	});

	if (itr == end(pointers))
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
	auto itr = find_if(begin(pointers), begin(pointers) + touch.active_pointers, [id](const TouchState::Pointer &pointer) {
		return pointer.id == id;
	});

	if (itr == end(pointers))
	{
		LOGE("Could not find pointer!\n");
		return;
	}

	auto index = itr - begin(pointers);

	TouchUpEvent event(itr->id, x, y, itr->start_x, itr->start_y);
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
	memmove(&pointers[index], &pointers[index + 1], (TouchCount - (index + 1)) * sizeof(TouchState::Pointer));
	touch.active_pointers--;
}

void InputTracker::joypad_key_state(unsigned index, JoypadKey key, JoypadKeyState state)
{
	if (index >= Joypads)
		return;

	auto &joy = joypads[index];
	unsigned key_index = Util::ecast(key);
	if (state == JoypadKeyState::Pressed)
	{
		if ((joy.button_mask & key_index) == 0)
		{
			JoypadButtonEvent event(index, key, state);
			if (UI::UIManager::get().filter_input_event(event))
				EventManager::get_global().dispatch_inline(event);
		}
		joy.button_mask |= 1u << key_index;
	}
	else if (state == JoypadKeyState::Released)
	{
		if ((joy.button_mask & key_index) == 1)
		{
			JoypadButtonEvent event(index, key, state);
			if (UI::UIManager::get().filter_input_event(event))
				EventManager::get_global().dispatch_inline(event);
		}
		joy.button_mask &= ~(1u << key_index);
	}
}

void InputTracker::joyaxis_state(unsigned index, JoypadAxis axis, float value)
{
	if (index >= Joypads)
		return;

	if (std::abs(value) < axis_deadzone)
		value = 0.0f;

	auto &joy = joypads[index];
	unsigned axis_index = Util::ecast(axis);
	auto &a = joy.axis[axis_index];
	if (a != value)
	{
		JoypadAxisEvent event(index, axis, value);
		if (UI::UIManager::get().filter_input_event(event))
			EventManager::get_global().dispatch_inline(event);
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
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
}

void InputTracker::mouse_button_event(MouseButton button, double x, double y, bool pressed)
{
	if (pressed)
		mouse_button_state |= 1ull << ecast(button);
	else
		mouse_button_state &= ~(1ull << ecast(button));

	MouseButtonEvent event(button, x, y, pressed);
	if (UI::UIManager::get().filter_input_event(event))
		EventManager::get_global().dispatch_inline(event);
}

void InputTracker::mouse_move_event(double x, double y)
{
	if (mouse_active)
	{
		double delta_x = x - last_mouse_x;
		double delta_y = y - last_mouse_y;
		last_mouse_x = x;
		last_mouse_y = y;
		MouseMoveEvent event(delta_x, delta_y, x, y, key_state, mouse_button_state);
		if (UI::UIManager::get().filter_input_event(event))
			EventManager::get_global().dispatch_inline(event);
	}
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

void InputTracker::dispatch_current_state(double delta_time)
{
	EventManager::get_global().dispatch_inline(JoypadStateEvent{joypads, Joypads, delta_time});
	EventManager::get_global().dispatch_inline(InputStateEvent{last_mouse_x, last_mouse_y, delta_time, key_state, mouse_button_state, mouse_active});
}

}