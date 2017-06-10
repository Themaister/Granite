#include "widget.hpp"
#include "flat_renderer.hpp"
#include <algorithm>

using namespace std;

namespace Granite
{
namespace UI
{
float Widget::render_children(FlatRenderer &renderer, float layer, ivec2 offset)
{
	float minimum_layer = layer;
	for (auto &child : children)
	{
		if (child.widget->get_visible())
		{
			if (child.widget->bg_color.a > 0.0f)
				renderer.render_quad(vec3(child.offset + offset, layer - 0.5f), vec2(child.size), child.widget->bg_color);

			renderer.push_scissor(child.offset + offset, child.size);
			float min_layer = child.widget->render(renderer, layer - 1.0f, child.offset + offset, child.size);
			minimum_layer = std::min(minimum_layer, min_layer);
			renderer.pop_scissor();
		}
	}
	return minimum_layer;
}

void Widget::add_child(const Util::IntrusivePtr<Widget> &widget)
{
	children.push_back({ ivec2(0), ivec2(0), widget });
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
}
}