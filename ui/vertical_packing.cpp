#include "vertical_packing.hpp"
#include "widget.hpp"

namespace Granite
{
namespace UI
{
float VerticalPacking::render(FlatRenderer &renderer, float layer, vec2 offset, vec2 size)
{
	vec2 off = vec2(geometry.margin, 0.0f);

	if (!children.empty())
	{
		float effective_height = size.y - geometry.margin * (children.size() + 1);
		float minimum_height = 0.0f;

		// Make sure we allocate the minimum.
		for (auto &child : children)
		{
			minimum_height += child.widget->get_minimum_geometry().y;
			child.size.y = child.widget->get_minimum_geometry().y;
		}

		float slack_height = effective_height - minimum_height;

		// Use padding space which is left. Unused padding space can be reallocated to other blocks.
		while (slack_height > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
			{
				if (child.size.y < child.widget->get_target_geometry().y)
					padding_targets++;
			}

			if (!padding_targets)
				break;

			float extra_height_per_object = max(floor(slack_height / padding_targets), 1.0f);

			// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
			for (auto &child : children)
			{
				float desired_padding = max(
					child.widget->get_target_geometry().y - child.size.y, 0.0f);
				float padding = min(desired_padding, extra_height_per_object);
				child.size.y += padding;
				slack_height -= padding;
			}
		}

		// Try to go "beyond" the target size for flexibly sized members.
		if (slack_height > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
				if (child.widget->get_size_is_flexible())
					padding_targets++;

			if (padding_targets)
			{
				float extra_height_per_object = max(floor(slack_height / padding_targets), 1.0f);

				// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
				for (auto &child : children)
				{
					if (child.widget->get_size_is_flexible())
						child.size.y += extra_height_per_object;
					slack_height -= extra_height_per_object;
				}
			}
		}

		for (auto &child : children)
		{
			off.y += geometry.margin;
			child.offset = off;
			off.y += child.size.y;

			float target = max(child.widget->get_target_geometry().x, child.widget->get_minimum_geometry().x);
			if (child.widget->get_size_is_flexible())
				target = max(target, size.x - 2.0f * geometry.margin);
			child.size.x = min(target, size.x - 2.0f * geometry.margin);
		}
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