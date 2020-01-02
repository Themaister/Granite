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

#include "widget.hpp"
#include <functional>

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

	enum class Orientation
	{
		Horizontal,
		Vertical
	};

	void set_orientation(Orientation orient)
	{
		orientation = orient;
		geometry_changed();
	}

	void set_size(vec2 size_)
	{
		size = size_;
	}

	void set_color(vec4 color_)
	{
		color = color_;
	}

	vec4 get_color() const
	{
		return color;
	}

	void set_label_slider_gap(float gap_size)
	{
		gap = gap_size;
	}

	void set_range(float minimum, float maximum);
	void set_value(float value);
	float get_value() const
	{
		return value;
	}

	Widget *on_mouse_button_pressed(vec2 offset) override;
	void on_mouse_button_move(vec2 offset) override;
	void on_mouse_button_released(vec2 offset) override;

	void show_label(bool enable)
	{
		label_enable = enable;
		geometry_changed();
	}

	void show_value(bool enable)
	{
		value_enable = enable;
		geometry_changed();
	}

	void show_tooltip(bool enable)
	{
		tooltip_enable = enable;
		geometry_changed();
	}

	void on_value_changed(std::function<void (float)> func)
	{
		value_cb = std::move(func);
	}

private:
	void reconfigure_to_canvas(vec2 offset, vec2 size) override;
	std::string text;
	Orientation orientation = Orientation::Horizontal;
	vec4 color = vec4(1.0f);
	vec2 size = vec2(0.0f);
	vec2 drag_size = vec2(0.0f);
	vec2 drag_base = vec2(0.0f);
	float gap = 0.0f;
	float normalized_value = 1.0f;
	float value = 1.0f;
	float value_minimum = 0.0f;
	float value_maximum = 1.0f;

	vec2 label_offset;
	vec2 label_size;
	vec2 slider_offset;
	vec2 slider_size;
	vec2 value_offset;
	vec2 value_size;

	bool label_enable = true;
	bool value_enable = true;
	bool tooltip_enable = false;

	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;

	bool displaying_tooltip = false;
	vec2 tooltip_offset = vec2(0.0f);

	std::function<void (float)> value_cb;
};
}
}