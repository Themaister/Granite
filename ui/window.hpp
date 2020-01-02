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

#include "vertical_packing.hpp"
#include <string>

namespace Granite
{
namespace UI
{
using WindowContainer = VerticalPacking;
class Window : public WindowContainer
{
public:
	Window();
	void set_title(const std::string &title);
	void set_title_color(const vec4 &color);

	const std::string &get_title() const
	{
		return title;
	}

	Widget *on_mouse_button_pressed(vec2 position) override;
	void on_mouse_button_move(vec2 offset) override;

	void reconfigure_to_canvas(vec2 offset, vec2 size) override;

	void show_title_bar(bool enable)
	{
		title_bar = enable;
		geometry_changed();
	}

	void set_fullscreen(bool enable)
	{
		fullscreen = enable;
		geometry_changed();
	}

	bool is_fullscreen() const
	{
		return fullscreen;
	}

private:
	std::string title;
	vec2 move_base = vec2(0.0f);
	float line_y = 0.0f;
	float y_offset = 0.0f;
	bool title_bar = true;
	bool fullscreen = false;
	vec4 title_color = vec4(0.0f, 0.0f, 0.0f, 1.0f);

	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}
