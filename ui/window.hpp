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

#include "vertical_packing.hpp"
#include <string>

namespace Granite
{
namespace UI
{
class Window : public VerticalPacking
{
public:
	Window();
	void set_title(const std::string &title);

	const std::string &get_title() const
	{
		return title;
	}

	void set_floating_position(vec2 pos)
	{
		position = pos;
		geometry_changed();
	}

	vec2 get_floating_position() const
	{
		return position;
	}

	Widget *on_mouse_button_pressed(vec2 position, vec2 size) override;
	void on_mouse_button_move(vec2 offset) override;

private:
	std::string title;
	vec2 position = vec2(0.0f);
	vec2 move_base = vec2(0.0f);

	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}
