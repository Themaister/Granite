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

#include "input.hpp"
#include "ui_manager.hpp"
#include "event.hpp"
#include <algorithm>
#include <string.h>

using namespace Util;
using namespace std;

namespace Granite
{
static inline Hash hash(unsigned code)
{
	Hasher h;
	h.u32(code);
	return h.get();
}

void JoypadRemapper::register_button(unsigned code, JoypadKey key, JoypadAxis axis)
{
	auto *btn = button_map.emplace_replace(hash(code));
	btn->axis = axis;
	btn->key = key;
}

void JoypadRemapper::register_axis(unsigned code,
                                   JoypadAxis axis, float axis_mod,
                                   JoypadKey neg_edge, JoypadKey pos_edge)
{
	auto *ax = axis_map.emplace_replace(hash(code));
	ax->axis = axis;
	ax->axis_mod = axis_mod;
	ax->neg_edge = neg_edge;
	ax->pos_edge = pos_edge;
}

const JoypadRemapper::AxisMap *JoypadRemapper::map_axis(unsigned code) const
{
	return axis_map.find(hash(code));
}

const JoypadRemapper::ButtonMap *JoypadRemapper::map_button(unsigned code) const
{
	return button_map.find(hash(code));
}

void JoypadRemapper::button_event(InputTracker &tracker, unsigned index, unsigned code, bool pressed)
{
	auto *button = map_button(code);
	if (!button)
		return;

	if (button->key != JoypadKey::Unknown)
		tracker.joypad_key_state(index, button->key, pressed ? JoypadKeyState::Pressed : JoypadKeyState::Released);

	if (button->axis != JoypadAxis::Unknown)
		tracker.joyaxis_state(index, button->axis, pressed ? 1.0f : 0.0f);
}

void JoypadRemapper::axis_event(InputTracker &tracker, unsigned index, unsigned code,
                                float value)
{
	auto *axis = map_axis(code);
	if (!axis)
		return;

	value = muglm::clamp(value * axis->axis_mod, -1.0f, 1.0f);

	if (axis->axis != JoypadAxis::Unknown)
	{
		if (axis->axis == JoypadAxis::LeftTrigger || axis->axis == JoypadAxis::RightTrigger)
			value = 0.5f * value + 0.5f;
		tracker.joyaxis_state(index, axis->axis, value);
	}

	if (axis->pos_edge != JoypadKey::Unknown)
	{
		tracker.joypad_key_state(index, axis->pos_edge,
		                         value > 0.5f ? JoypadKeyState::Pressed :
		                         JoypadKeyState::Released);
	}

	if (axis->neg_edge != JoypadKey::Unknown)
	{
		tracker.joypad_key_state(index, axis->neg_edge,
		                         value < -0.5f ? JoypadKeyState::Pressed
		                                       : JoypadKeyState::Released);
	}
}

void JoypadRemapper::reset()
{
	button_map.clear();
	axis_map.clear();
}

void InputTracker::orientation_event(quat rot)
{
	OrientationEvent event(rot);

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);
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

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);
}

void InputTracker::dispatch_touch_gesture()
{
	TouchGestureEvent event(touch);

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);
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

	TouchUpEvent event(itr->id, x, y, itr->start_x, itr->start_y, touch.width, touch.height);

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);

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

			auto *ui = Global::ui_manager();
			auto *event_manager = Global::event_manager();
			if (ui && ui->filter_input_event(event))
				if (event_manager)
					event_manager->dispatch_inline(event);
		}
		joy.button_mask |= 1u << key_index;
	}
	else if (state == JoypadKeyState::Released)
	{
		if ((joy.button_mask & key_index) == 1)
		{
			JoypadButtonEvent event(index, key, state);

			auto *ui = Global::ui_manager();
			auto *event_manager = Global::event_manager();
			if (ui && ui->filter_input_event(event))
				if (event_manager)
					event_manager->dispatch_inline(event);
		}
		joy.button_mask &= ~(1u << key_index);
	}
}

void InputTracker::joypad_key_state_raw(unsigned index, unsigned code, bool pressed)
{
	if (index >= Joypads)
		return;
	remappers[index].button_event(*this, index, code, pressed);
}

void InputTracker::joyaxis_state_raw(unsigned index, unsigned code, float value)
{
	if (index >= Joypads)
		return;
	remappers[index].axis_event(*this, index, code, value);
}

void InputTracker::joyaxis_state(unsigned index, JoypadAxis axis, float value)
{
	if (index >= Joypads)
		return;

	assert(active_joypads & (1u << index));

	if (std::abs(value) < axis_deadzone)
		value = 0.0f;

	auto &joy = joypads[index];
	unsigned axis_index = Util::ecast(axis);
	auto &a = joy.axis[axis_index];
	if (a != value)
	{
		JoypadAxisEvent event(index, axis, value);

		auto *ui = Global::ui_manager();
		auto *event_manager = Global::event_manager();
		if (ui && ui->filter_input_event(event))
			if (event_manager)
				event_manager->dispatch_inline(event);
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

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);
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

	MouseButtonEvent event(button, x, y, pressed);

	auto *ui = Global::ui_manager();
	auto *event_manager = Global::event_manager();
	if (ui && ui->filter_input_event(event))
		if (event_manager)
			event_manager->dispatch_inline(event);
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

		auto *ui = Global::ui_manager();
		auto *event_manager = Global::event_manager();
		if (ui && ui->filter_input_event(event))
			if (event_manager)
				event_manager->dispatch_inline(event);
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

		auto *ui = Global::ui_manager();
		auto *event_manager = Global::event_manager();
		if (ui && ui->filter_input_event(event))
			if (event_manager)
				event_manager->dispatch_inline(event);
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
	auto *event_manager = Global::event_manager();
	if (event_manager)
	{
		event_manager->dispatch_inline(JoypadStateEvent{active_joypads, joypads, Joypads, delta_time});
		event_manager->dispatch_inline(
				InputStateEvent{last_mouse_x, last_mouse_y, delta_time, key_state, mouse_button_state, mouse_active});
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

void InputTracker::enable_joypad(unsigned index)
{
	if (index >= Joypads)
		return;

	if (active_joypads & (1u << index))
		return;

	active_joypads |= 1u << index;
	joypads[index] = {};
	JoypadConnectionEvent event(index, true);

	auto *event_manager = Global::event_manager();
	if (event_manager)
		event_manager->dispatch_inline(event);
}

void InputTracker::disable_joypad(unsigned index)
{
	if (index >= Joypads)
		return;

	if ((active_joypads & (1u << index)) == 0)
		return;

	active_joypads &= ~(1u << index);
	joypads[index] = {};
	JoypadConnectionEvent event(index, false);

	auto *event_manager = Global::event_manager();
	if (event_manager)
		event_manager->dispatch_inline(event);
}

}
