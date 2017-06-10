#include "horizontal_packing.hpp"
#include "widget.hpp"

namespace Granite
{
namespace UI
{
float HorizontalPacking::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	vec2 off = vec2(0.0f, geometry.margin);

	if (!children.empty())
	{
		float effective_width = size.x - geometry.margin * (children.size() + 1);
		float minimum_width = 0.0f;

		// Make sure we allocate the minimum.
		for (auto &child : children)
		{
			minimum_width += child.widget->get_minimum_geometry().x;
			child.size.x = child.widget->get_minimum_geometry().x;
		}

		float slack_width = effective_width - minimum_width;

		// Use padding space which is left. Unused padding space can be reallocated to other blocks.
		while (slack_width > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
			{
				if (child.size.x < child.widget->get_target_geometry().x)
					padding_targets++;
			}

			if (!padding_targets)
				break;

			float extra_width_per_object = max(floor(slack_width / padding_targets), 1.0f);

			// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
			for (auto &child : children)
			{
				float desired_padding = max(
					child.widget->get_target_geometry().x - child.size.x, 0.0f);
				float padding = min(desired_padding, extra_width_per_object);
				child.size.x += padding;
				slack_width -= padding;
			}
		}

		// Try to go "beyond" the target size for flexibly sized members.
		if (slack_width > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
				if (child.widget->get_size_is_flexible())
					padding_targets++;

			if (padding_targets)
			{
				float extra_width_per_object = max(floor(slack_width / padding_targets), 1.0f);

				// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
				for (auto &child : children)
				{
					if (child.widget->get_size_is_flexible())
						child.size.x += extra_width_per_object;
					slack_width -= extra_width_per_object;
				}
			}
		}

		for (auto &child : children)
		{
			off.x += geometry.margin;
			child.offset = off;
			off.x += child.size.x;

			float target = max(child.widget->get_target_geometry().y, child.widget->get_minimum_geometry().y);
			if (child.widget->get_size_is_flexible())
				target = max(target, size.y - 2.0f * geometry.margin);
			child.size.y = min(target, size.y - 2.0f * geometry.margin);
		}
	}

	return render_children(renderer, layer, offset);
}

void HorizontalPacking::reconfigure()
{
	vec2 minimum = vec2(0.0f);
	vec2 target = vec2(0.0f);

	for (auto &child : children)
	{
		minimum.y = max(child.widget->get_minimum_geometry().y, minimum.y);
		minimum.x += child.widget->get_minimum_geometry().x;

		target.y = max(child.widget->get_target_geometry().y, target.y);
		target.x += child.widget->get_target_geometry().x;
	}

	if (!children.empty())
	{
		target.x += geometry.margin * (children.size() + 1);
		minimum.x += geometry.margin * (children.size() + 1);
	}

	target.y += 2.0f * geometry.margin;
	minimum.y += 2.0f * geometry.margin;

	geometry.target = target;
	geometry.minimum = minimum;
}
}
}