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

#include "ui_manager.hpp"
#include "window.hpp"

using namespace Util;

namespace Granite
{
namespace UI
{
UIManager &UIManager::get()
{
	static UIManager manager;
	return manager;
}

UIManager::UIManager()
{
	fonts[ecast(FontSize::Small)].reset(new Font("builtin://fonts/font.ttf", 12));
	fonts[ecast(FontSize::Normal)].reset(new Font("builtin://fonts/font.ttf", 16));
	fonts[ecast(FontSize::Large)].reset(new Font("builtin://fonts/font.ttf", 24));
}

void UIManager::add_child(WidgetHandle handle)
{
	widgets.push_back(handle);
}

void UIManager::render(Vulkan::CommandBuffer &cmd)
{
	renderer.begin();

	float minimum_layer = 0.0f;
	for (auto &widget : widgets)
	{
		auto *window = static_cast<Window *>(widget.get());
		widget->reconfigure_geometry();
		widget->reconfigure_geometry_to_canvas(window->get_floating_position(), window->get_minimum_geometry());

		vec2 window_size = max(widget->get_target_geometry(), widget->get_minimum_geometry());
		renderer.push_scissor(window->get_floating_position(), window_size);
		float min_layer = widget->render(renderer, 0.0f, window->get_floating_position(), window_size);
		renderer.pop_scissor();

		minimum_layer = min(min_layer, minimum_layer);
	}

	renderer.flush(cmd, vec3(0.0f, 0.0f, minimum_layer), vec3(cmd.get_viewport().width, cmd.get_viewport().height, 32000.0f));
}

Font& UIManager::get_font(FontSize size)
{
	return *fonts[Util::ecast(size)];
}

bool UIManager::filter_input_event(const TouchUpEvent &)
{
	return true;
}

bool UIManager::filter_input_event(const TouchDownEvent &)
{
	return true;
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

bool UIManager::filter_input_event(const MouseButtonEvent &e)
{
	if (drag_receiver && e.get_pressed())
		return false;

	if (!e.get_pressed())
	{
		if (drag_receiver)
		{
			vec2 pos(e.get_abs_x(), e.get_abs_y());
			drag_receiver->on_mouse_button_released(pos);
			drag_receiver = nullptr;
			return false;
		}
		else
			return true;
	}

	for (auto &widget : widgets)
	{
		auto *window = static_cast<Window *>(widget.get());
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

bool UIManager::filter_input_event(const TouchGestureEvent &)
{
	return true;
}
}
}
