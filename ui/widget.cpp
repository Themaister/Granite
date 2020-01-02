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

#include "widget.hpp"
#include "flat_renderer.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
namespace UI
{
float Widget::render_children(FlatRenderer &renderer, float layer, vec2 offset)
{
	float minimum_layer = layer;
	for (auto &child : children)
	{
		if (child.widget->get_visible())
		{
			if (child.widget->bg_color.w > 0.0f)
			{
				if (child.widget->bg_image)
				{
					auto &image = *child.widget->bg_image->get_image();
					renderer.render_textured_quad(image.get_view(),
					                              vec3(child.offset + offset, layer - 0.5f), vec2(child.size),
					                              vec2(0.0f), vec2(image.get_width(0), image.get_height(0)),
					                              DrawPipeline::AlphaBlend, child.widget->bg_color, Vulkan::StockSampler::LinearClamp);
				}
				else
				{
					renderer.render_quad(vec3(child.offset + offset, layer - 0.5f), vec2(child.size),
					                     child.widget->bg_color);
				}
			}

			renderer.push_scissor(child.offset + offset, child.size);
			float min_layer = child.widget->render(renderer, layer - 1.0f, child.offset + offset, child.size);
			minimum_layer = std::min(minimum_layer, min_layer);
			renderer.pop_scissor();
		}
	}
	return minimum_layer;
}

Widget *Widget::on_mouse_button_pressed(vec2 offset)
{
	for (auto &child : children)
	{
		if (any(lessThan(offset, child.offset)) ||
		    any(greaterThanEqual(offset, child.offset + child.size)))
			continue;

		auto *ret = child.widget->on_mouse_button_pressed(offset - child.offset);
		if (ret)
			return ret;
	}

	return nullptr;
}

void Widget::add_child(Util::IntrusivePtr<Widget> widget)
{
	children.push_back({ vec2(0), vec2(0), widget });
	assert(widget->parent == nullptr);
	widget->parent = this;
	geometry_changed();
}

Util::IntrusivePtr<Widget> Widget::remove_child(const Widget &widget)
{
	auto itr = find_if(begin(children), end(children), [&](const Child &c) -> bool {
		return c.widget.get() == &widget;
	});

	if (itr == end(children))
		return {};

	auto res = itr->widget;
	children.erase(itr);
	res->parent = nullptr;
	return res;
}

bool Widget::get_needs_redraw() const
{
	if (needs_redraw)
		return true;

	for (auto &child : children)
	{
		if (child.widget->needs_redraw)
			return true;
	}

	return false;
}

void Widget::geometry_changed()
{
	needs_redraw = true;
	needs_reconfigure = true;
	if (parent)
		parent->geometry_changed();
}

void Widget::reconfigure_geometry()
{
	for (auto &child : children)
		child.widget->reconfigure_geometry();
	reconfigure();
	needs_reconfigure = false;
}

void Widget::reconfigure_geometry_to_canvas(vec2 offset, vec2 size)
{
	reconfigure_to_canvas(offset, size);
	for (auto &child : children)
		child.widget->reconfigure_geometry_to_canvas(child.offset + offset, child.size);
}
}
}