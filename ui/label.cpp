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

#include "label.hpp"
#include "widget.hpp"

using namespace std;

namespace Granite
{
namespace UI
{
Label::Label(string text_, FontSize font_size_)
	: text(move(text_)), font_size(font_size_)
{
}

void Label::set_text(std::string text_)
{
	text = move(text_);
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
	auto &ui = *Global::ui_manager();
	auto &font = ui.get_font(font_size);
	renderer.render_text(font, text.c_str(), vec3(offset + geometry.margin, layer), size - 2.0f * geometry.margin,
	                     color, alignment);

	assert(children.empty());
	return layer;
}

void Label::reconfigure()
{
	auto &ui = *Global::ui_manager();
	auto &font = ui.get_font(font_size);
	vec2 minimum = font.get_text_geometry(text.c_str());

	geometry.minimum = max(geometry.minimum, minimum + 2.0f * geometry.margin);
}
}
}