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
}
}
