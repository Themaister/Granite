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
#include <functional>

namespace Granite
{
namespace UI
{
class ClickButton : public Widget
{
public:
	void set_text(std::string text);
	const std::string &get_text() const
	{
		return text;
	}

	void set_label_alignment(Font::Alignment alignment_)
	{
		alignment = alignment_;
	}

	void set_font_color(vec4 color_)
	{
		color = color_;
	}

	void on_click(std::function<void ()> cb)
	{
		click_cb = std::move(cb);
	}

	void set_font_size(FontSize size);

private:
	void reconfigure() override;
	void reconfigure_to_canvas(vec2 offset, vec2 size) override;
	Widget *on_mouse_button_pressed(vec2 offset) override;
	void on_mouse_button_released(vec2 offset) override;

	float render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size) override;
	Font::Alignment alignment = Font::Alignment::Center;
	vec4 color = vec4(0.0f, 0.0f, 0.0f, 1.0f);
	std::string text;

	bool click_held = false;
	std::function<void ()> click_cb;
	FontSize font_size = FontSize::Small;
};
}
}