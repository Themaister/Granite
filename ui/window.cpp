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

#include "window.hpp"
#include "flat_renderer.hpp"
#include "ui_manager.hpp"
#include "widget.hpp"

namespace Granite
{
namespace UI
{
Window::Window()
{
	bg_color = vec4(1.0f);
}

void Window::set_title(const std::string &title)
{
	this->title = title;
	geometry_changed();
}

Widget *Window::on_mouse_button_pressed(vec2 offset)
{
	move_base = position;

	auto &font = UIManager::get().get_font(FontSize::Large);
	vec2 text_geom = font.get_text_geometry(title.c_str());
	float line_y = text_geom.y + geometry.margin + 2.0f;
	if (offset.y < line_y)
		return this;

	for (auto &child : children)
	{
		if (any(lessThan(offset, child.offset)) ||
		    any(greaterThanEqual(offset, child.offset + child.size)))
			continue;

		return child.widget->on_mouse_button_pressed(offset - child.offset);
	}

	return nullptr;
}

void Window::on_mouse_button_move(vec2 offset)
{
	position = move_base + offset;
	geometry_changed();
}

float Window::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	if (bg_color.a > 0.0f)
		renderer.render_quad(vec3(offset, layer), size, bg_color);

	auto &font = UIManager::get().get_font(FontSize::Large);

	vec2 text_geom = font.get_text_geometry(title.c_str());
	vec2 text_offset = font.get_aligned_offset(Font::Alignment::TopCenter, text_geom, size);
	float line_y = text_geom.y + text_offset.y + geometry.margin;
	vec2 offsets[] = {
		{ offset.x + geometry.margin, line_y + offset.y },
		{ offset.x + size.x - geometry.margin, line_y + offset.y },
	};
	renderer.render_line_strip(offsets, layer - 0.5f, 2, vec4(0.0f, 0.0f, 0.0f, 1.0f));
	offsets[0].y += 2.0f;
	offsets[1].y += 2.0f;
	renderer.render_line_strip(offsets, layer - 0.5f, 2, vec4(0.0f, 0.0f, 0.0f, 1.0f));

	renderer.render_text(font, title.c_str(),
	                     vec3(offset, layer - 0.5f), size, vec4(0.0f, 0.0f, 0.0f, 1.0f), Font::Alignment::TopCenter);

	float ret = VerticalPacking::render(renderer, layer, vec2(offset.x, offset.y + line_y + 2.0f), size - vec2(0.0f, line_y + 2.0f));
	return std::min(ret, layer - 0.5f);
}

void Window::reconfigure()
{
	auto target = geometry.target;
	VerticalPacking::reconfigure();
	geometry.target = target;

	auto &font = UIManager::get().get_font(FontSize::Large);
	vec2 text_geom = font.get_text_geometry(title.c_str());
	float line_y = text_geom.y + geometry.margin + 2.0f;

	vec2 minimum = geometry.minimum;
	minimum.y += line_y;
	minimum.x = max(text_geom.x + 2.0f * geometry.margin, minimum.x);
	geometry.minimum = minimum;
}
}
}