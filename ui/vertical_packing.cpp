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

#include "vertical_packing.hpp"
#include "widget.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
namespace UI
{
void VerticalPacking::reconfigure_to_canvas(vec2, vec2 size)
{
	vec2 off = vec2(geometry.margin, 0.0f);

	unsigned fixed_children = 0;
	for (auto &child : children)
	{
		if (!child.widget->get_visible())
			continue;

		if (!child.widget->is_floating())
			fixed_children++;
	}

	if (!children.empty())
	{
		float effective_height = size.y - geometry.margin * (fixed_children + 1);
		float minimum_height = 0.0f;

		// Make sure we allocate the minimum.
		for (auto &child : children)
		{
			if (!child.widget->get_visible())
				continue;

			if (child.widget->is_floating())
			{
				child.size = max(child.widget->get_minimum_geometry(), child.widget->get_target_geometry());
				child.offset = child.widget->get_floating_position() + geometry.margin;
			}
			else
			{
				minimum_height += child.widget->get_minimum_geometry().y;
				child.size.y = child.widget->get_minimum_geometry().y;
			}
		}

		float slack_height = effective_height - minimum_height;

		// Use padding space which is left. Unused padding space can be reallocated to other blocks.
		while (slack_height > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
			{
				if (!child.widget->get_visible())
					continue;

				if (!child.widget->is_floating() && (child.size.y < child.widget->get_target_geometry().y))
					padding_targets++;
			}

			if (!padding_targets)
				break;

			float extra_height_per_object = max(muglm::floor(slack_height / padding_targets), 1.0f);

			// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
			for (auto &child : children)
			{
				if (!child.widget->get_visible())
					continue;

				if (!child.widget->is_floating())
				{
					float desired_padding = max(
						child.widget->get_target_geometry().y - child.size.y, 0.0f);
					float padding = min(desired_padding, extra_height_per_object);
					child.size.y += padding;
					slack_height -= padding;
				}
			}
		}

		// Try to go "beyond" the target size for flexibly sized members.
		if (slack_height > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
			{
				if (!child.widget->get_visible())
					continue;

				if (!child.widget->is_floating() && child.widget->get_size_is_flexible())
					padding_targets++;
			}

			if (padding_targets)
			{
				float extra_height_per_object = max(muglm::floor(slack_height / padding_targets), 1.0f);

				// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
				for (auto &child : children)
				{
					if (!child.widget->is_floating() && child.widget->get_size_is_flexible())
						child.size.y += extra_height_per_object;
					slack_height -= extra_height_per_object;
				}
			}
		}

		for (auto &child : children)
		{
			if (!child.widget->get_visible())
				continue;

			if (!child.widget->is_floating())
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
	}
}

float VerticalPacking::render(FlatRenderer &renderer, float layer, vec2 offset, vec2)
{
	return render_children(renderer, layer, offset);
}

void VerticalPacking::reconfigure()
{
	vec2 minimum = vec2(0.0f);
	vec2 target = vec2(0.0f);

	unsigned non_floating_count = 0;

	for (auto &child : children)
	{
		if (!child.widget->get_visible())
			continue;

		if (child.widget->is_floating())
		{
			minimum = max(minimum,
			              child.widget->get_floating_position() +
			              max(child.widget->get_minimum_geometry(), child.widget->get_target_geometry()));
		}
		else
		{
			non_floating_count++;
			minimum.x = max(child.widget->get_minimum_geometry().x, minimum.x);
			minimum.y += child.widget->get_minimum_geometry().y;

			target.x = max(child.widget->get_target_geometry().x, target.x);
			target.y += child.widget->get_target_geometry().y;
		}
	}

	if (!children.empty())
	{
		target.y += geometry.margin * (non_floating_count + 1);
		minimum.y += geometry.margin * (non_floating_count + 1);
	}

	target.x += 2.0f * geometry.margin;
	minimum.x += 2.0f * geometry.margin;

	geometry.target = target;
	geometry.minimum = minimum;
}
}
}
