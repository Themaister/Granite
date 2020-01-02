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
#include "font.hpp"
#include "ui_manager.hpp"

namespace Granite
{
namespace UI
{
class Label : public Widget
{
public:
	Label(std::string text = "", FontSize font_size = FontSize::Normal);

	void set_text(std::string text);
	void set_font_size(FontSize font_size);

	const std::string &get_text() const
	{
		return text;
	}

	void set_font_alignment(Font::Alignment alignment_)
	{
		alignment = alignment_;
		geometry_changed();
	}

	Font::Alignment get_font_alignment() const
	{
		return alignment;
	}

	void set_color(vec4 color_)
	{
		color = color_;
	}

	vec4 get_color() const
	{
		return color;
	}

private:
	std::string text;
	FontSize font_size;
	vec4 color = vec4(1.0f);
	Font::Alignment alignment = Font::Alignment::TopLeft;
	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	void reconfigure() override;
	void reconfigure_to_canvas(vec2 offset, vec2 size) override;
};
}
}