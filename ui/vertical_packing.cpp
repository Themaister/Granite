#include "vertical_packing.hpp"
#include "widget.hpp"

namespace Granite
{
namespace UI
{
float VerticalPacking::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	vec2 off = vec2(geometry.margin, 0.0f);

	unsigned child_count = children.size();
	for (auto &child : children)
	{
		off.y += geometry.margin;
		child.offset = off;
		child.size.x = min(child.widget->get_target_geometry().x, size.x - 2.0f * geometry.margin);

		float layout_height = size.y - off.y;
		layout_height -= geometry.margin * child_count;
		layout_height /= child_count;

		float target = child.widget->get_target_geometry().y;
		if (child.widget->get_size_is_flexible())
			target = max(layout_height, target);

		child.size.y = min(layout_height, target);
		child.size.y = max(child.widget->get_minimum_geometry().y, child.size.y);

		off.y += child.size.y;
		child_count--;
	}

	return render_children(renderer, layer, offset);
}

void VerticalPacking::reconfigure()
{
	vec2 minimum = vec2(0.0f);
	vec2 target = vec2(0.0f);

	for (auto &child : children)
	{
		minimum.x = max(child.widget->get_minimum_geometry().x, minimum.x);
		minimum.y += child.widget->get_minimum_geometry().y;

		target.x = max(child.widget->get_target_geometry().x, target.x);
		target.y += child.widget->get_target_geometry().y;
	}

	if (!children.empty())
	{
		target.y += geometry.margin * (children.size() + 1);
		minimum.y += geometry.margin * (children.size() + 1);
	}

	target.x += 2.0f * geometry.margin;
	minimum.x += 2.0f * geometry.margin;

	geometry.target = target;
	geometry.minimum = minimum;
}
}
}