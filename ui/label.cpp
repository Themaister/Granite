/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "label.hpp"
#include "widget.hpp"
#include "device.hpp"

namespace Granite
{
namespace UI
{
Label::Label(std::string text_, FontSize font_size_)
	: text(std::move(text_)), font_size(font_size_)
{
}

void Label::set_text(std::string text_)
{
	text = std::move(text_);
	geometry_changed();
}

void Label::set_font_size(FontSize font_size_)
{
	font_size = font_size_;
	geometry_changed();
}

void Label::reconfigure_to_canvas(vec2, vec2)
{
}

float Label::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	auto &ui = *GRANITE_UI_MANAGER();
	auto &font = ui.get_font(font_size);
	renderer.render_text(font, text.c_str(), vec3(offset + geometry.margin, layer - 0.5f), size - 2.0f * geometry.margin,
	                     color, alignment);

	assert(children.empty());
	return layer - 0.5f;
}

void Label::reconfigure()
{
	auto &ui = *GRANITE_UI_MANAGER();
	auto &font = ui.get_font(font_size);
	vec2 minimum = font.get_text_geometry(text.c_str());

	geometry.minimum = max(geometry.minimum, minimum + 2.0f * geometry.margin);
}
}
}