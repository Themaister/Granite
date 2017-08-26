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

#include "widget.hpp"

namespace Granite
{
namespace UI
{
class Slider : public Widget
{
public:
	void set_text(std::string text);
	const std::string &get_text() const
	{
		return text;
	}

	void set_size(vec2 size)
	{
		this->size = size;
	}

	void set_color(vec4 color)
	{
		this->color = color;
	}

	vec4 get_color() const
	{
		return color;
	}

	void set_label_slider_gap(float size)
	{
		gap = size;
	}

	virtual Widget *on_mouse_button_pressed(vec2 offset, vec2 size) override;
	virtual void on_mouse_button_move(vec2 offset) override;

private:
	std::string text;
	vec4 color = vec4(1.0f);
	vec2 size = vec2(0.0f);
	vec2 drag_size = vec2(0.0f);
	vec2 drag_base = vec2(0.0f);
	float gap = 0.0f;
	float value = 1.0f;
	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
};
}
}