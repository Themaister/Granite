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
	set_floating(true);
}

void Window::set_title(const std::string &title_)
{
	title = title_;
	geometry_changed();
}

void Window::set_title_color(const vec4 &color)
{
	title_color = color;
}

Widget *Window::on_mouse_button_pressed(vec2 offset)
{
	move_base = floating_position;

	if (title_bar && floating)
	{
		if (offset.y < y_offset)
			return this;
	}

	float off_y = y_offset;

	for (auto &child : children)
	{
		if (!child.widget->get_visible())
			continue;

		if (any(lessThan(offset, child.offset + vec2(0.0f, off_y))) ||
		    any(greaterThanEqual(offset, child.offset + vec2(0.0f, off_y) + child.size)))
			continue;

		auto *ret = child.widget->on_mouse_button_pressed(offset - (child.offset + vec2(0.0f, off_y)));
		if (ret)
			return ret;
	}

	return nullptr;
}

void Window::on_mouse_button_move(vec2 offset)
{
	floating_position = move_base + offset;
	geometry_changed();
}

void Window::reconfigure_to_canvas(vec2 offset, vec2 size)
{
	y_offset = 0.0f;
	line_y = 0.0f;

	if (title_bar)
	{
		auto &ui = *Global::ui_manager();
		auto &font = ui.get_font(FontSize::Large);
		vec2 text_geom = font.get_text_geometry(title.c_str());
		vec2 text_offset = font.get_aligned_offset(Font::Alignment::TopCenter, text_geom, size);
		line_y = text_geom.y + text_offset.y + geometry.margin;
		y_offset = line_y + 2.0f;
	}

	WindowContainer::reconfigure_to_canvas(vec2(offset.x, offset.y + y_offset),
	                                       size - vec2(0.0f, y_offset));
}

float Window::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	if (bg_color.w > 0.0f)
	{
		if (bg_image)
		{
			auto &image = *bg_image->get_image();
			renderer.render_textured_quad(image.get_view(),
			                              vec3(offset, layer), size,
			                              vec2(0.0f), vec2(image.get_width(0), image.get_height(0)),
			                              DrawPipeline::AlphaBlend, bg_color, Vulkan::StockSampler::LinearClamp);
		}
		else
		{
			renderer.render_quad(vec3(offset, layer), size, bg_color);
		}
	}

	if (title_bar)
	{
		auto &ui = *Global::ui_manager();
		auto &font = ui.get_font(FontSize::Large);

		vec2 offsets[] = {
			{offset.x + geometry.margin,          line_y + offset.y},
			{offset.x + size.x - geometry.margin, line_y + offset.y},
		};

		renderer.render_line_strip(offsets, layer - 0.5f, 2, title_color);
		offsets[0].y += 2.0f;
		offsets[1].y += 2.0f;
		renderer.render_line_strip(offsets, layer - 0.5f, 2, title_color);

		renderer.render_text(font, title.c_str(),
		                     vec3(offset, layer - 0.5f), size, title_color,
		                     Font::Alignment::TopCenter);
	}

	float ret = WindowContainer::render(renderer, layer, vec2(offset.x, offset.y + y_offset), size - vec2(0.0f, y_offset));
	return std::min(ret, layer - 0.5f);
}

void Window::reconfigure()
{
	auto target = geometry.target;
	WindowContainer::reconfigure();
	geometry.target = target;

	if (title_bar)
	{
		auto &ui = *Global::ui_manager();
		auto &font = ui.get_font(FontSize::Large);
		vec2 text_geom = font.get_text_geometry(title.c_str());
		float y = text_geom.y + geometry.margin + 2.0f;

		vec2 minimum = geometry.minimum;
		minimum.y += y;
		minimum.x = max(text_geom.x + 2.0f * geometry.margin, minimum.x);
		geometry.minimum = minimum;
	}
}
}
}