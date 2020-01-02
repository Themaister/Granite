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

#include "ui_manager.hpp"
#include "window.hpp"

using namespace Util;

namespace Granite
{
namespace UI
{
UIManager::UIManager()
{
}

void UIManager::add_child(WidgetHandle handle)
{
	widgets.push_back(handle);
}

void UIManager::reset_children()
{
	widgets.clear();
}

void UIManager::remove_child(Widget *widget)
{
	auto itr = remove_if(begin(widgets), end(widgets), [widget](const WidgetHandle &handle) {
		return handle.get() == widget;
	});
	widgets.erase(itr, end(widgets));
}

void UIManager::render(Vulkan::CommandBuffer &cmd)
{
	renderer.begin();

	const float max_layers = 20000.0f; // Roughly for D16 with some headroom for quantization errors.

	float minimum_layer = max_layers - 1.0f;
	for (auto &widget : widgets)
	{
		auto *window = static_cast<Window *>(widget.get());
		if (!window->get_visible())
			continue;

		widget->reconfigure_geometry();

		vec2 window_size;
		vec2 window_pos;

		if (window->is_fullscreen())
		{
			window_size.x = cmd.get_viewport().width;
			window_size.y = cmd.get_viewport().height;
			widget->reconfigure_geometry_to_canvas(vec2(0.0f),
			                                       vec2(cmd.get_viewport().width, cmd.get_viewport().height));
			window_pos = vec2(0.0f);
		}
		else
		{
			widget->reconfigure_geometry_to_canvas(window->get_floating_position(), window->get_minimum_geometry());
			window_size = max(widget->get_target_geometry(), widget->get_minimum_geometry());
			window_pos = window->get_floating_position();
		}

		renderer.push_scissor(window->get_floating_position(), window_size);
		float min_layer = widget->render(renderer, minimum_layer, window_pos, window_size);
		renderer.pop_scissor();

		minimum_layer = min(min_layer, minimum_layer);
	}

	renderer.flush(cmd, vec3(0.0f, 0.0f, minimum_layer),
	               vec3(cmd.get_viewport().width, cmd.get_viewport().height, max_layers));
}

Font& UIManager::get_font(FontSize size)
{
	auto &font = fonts[Util::ecast(size)];
	if (!font)
	{
		unsigned pix_size = 0;
		switch (size)
		{
		case FontSize::Small:
			pix_size = 12;
			break;

		case FontSize::Normal:
			pix_size = 16;
			break;

		case FontSize::Large:
			pix_size = 24;
			break;

		default:
			break;
		}

		font.reset(new Font("builtin://fonts/font.ttf", pix_size));
	}
	return *font;
}

bool UIManager::filter_input_event(const TouchUpEvent &e)
{
	if (e.get_id() != touch_emulation_id)
		return true;

	touch_emulation_id = ~0u;
	double x = e.get_x() * e.get_screen_width();
	double y = e.get_y() * e.get_screen_height();
	MouseButtonEvent mouse_btn(MouseButton::Left, x, y, false);
	return filter_input_event(mouse_btn);
}

bool UIManager::filter_input_event(const TouchDownEvent &e)
{
	if (e.get_index() != 0)
		return true;

	touch_emulation_id = e.get_id();

	double x = e.get_x() * e.get_screen_width();
	double y = e.get_y() * e.get_screen_height();

	MouseButtonEvent mouse_btn(MouseButton::Left, x, y, true);
	return filter_input_event(mouse_btn);
}

bool UIManager::filter_input_event(const TouchGestureEvent &e)
{
	auto &state = e.get_state();

	auto &pointers = state.pointers;
	auto itr = std::find_if(std::begin(pointers), std::begin(pointers) + state.active_pointers, [this](const TouchState::Pointer &pointer) {
		return pointer.id == touch_emulation_id;
	});

	if (itr == std::end(pointers))
		return true;

	MouseMoveEvent move(0.0, 0.0, itr->x * state.width, itr->y * state.height, 0, 0);
	return filter_input_event(move);
}

bool UIManager::filter_input_event(const KeyboardEvent &)
{
	return true;
}

bool UIManager::filter_input_event(const MouseMoveEvent &e)
{
	if (drag_receiver)
	{
		vec2 pos(e.get_abs_x(), e.get_abs_y());
		drag_receiver->on_mouse_button_move(pos - drag_receiver_base);
		return false;
	}
	else
		return true;
}

bool UIManager::filter_input_event(const JoypadButtonEvent &)
{
	return true;
}

bool UIManager::filter_input_event(const JoypadAxisEvent &)
{
	return true;
}

bool UIManager::filter_input_event(const MouseButtonEvent &e)
{
	if (drag_receiver && e.get_pressed())
		return false;

	if (!e.get_pressed())
	{
		if (drag_receiver)
		{
			vec2 pos(e.get_abs_x(), e.get_abs_y());
			drag_receiver->on_mouse_button_released(pos - drag_receiver_base);
			drag_receiver = nullptr;
			return false;
		}
		else
			return true;
	}

	for (auto &widget : widgets)
	{
		auto *window = static_cast<Window *>(widget.get());
		if (!window->get_visible())
			continue;
		widget->reconfigure_geometry();
		widget->reconfigure_geometry_to_canvas(window->get_floating_position(), window->get_minimum_geometry());

		vec2 pos(e.get_abs_x(), e.get_abs_y());
		vec2 window_pos = pos - window->get_floating_position();

		if (any(greaterThanEqual(window_pos, window->get_minimum_geometry())) ||
		    any(lessThan(window_pos, vec2(0.0f))))
		{
			continue;
		}

		if (e.get_button() == MouseButton::Left)
		{
			auto *receiver = window->on_mouse_button_pressed(pos - window->get_floating_position());
			drag_receiver = receiver;
			drag_receiver_base = pos;
		}

		return false;
	}

	return true;
}

bool UIManager::filter_input_event(const OrientationEvent &)
{
	return true;
}

}
}
