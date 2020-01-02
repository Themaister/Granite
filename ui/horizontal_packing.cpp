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

#include "horizontal_packing.hpp"
#include "widget.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
namespace UI
{
void HorizontalPacking::reconfigure_to_canvas(vec2, vec2 size)
{
	vec2 off = vec2(0.0f, geometry.margin);

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
		float effective_width = size.x - geometry.margin * (fixed_children + 1);
		float minimum_width = 0.0f;

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
				minimum_width += child.widget->get_minimum_geometry().x;
				child.size.x = child.widget->get_minimum_geometry().x;
			}
		}

		float slack_width = effective_width - minimum_width;

		// Use padding space which is left. Unused padding space can be reallocated to other blocks.
		while (slack_width > 0.0f)
		{
			unsigned padding_targets = 0;

			for (auto &child : children)
			{
				if (!child.widget->get_visible())
					continue;

				if (!child.widget->is_floating() && (child.size.x < child.widget->get_target_geometry().x))
					padding_targets++;
			}

			if (!padding_targets)
				break;

			float extra_width_per_object = max(muglm::floor(slack_width / float(padding_targets)), 1.0f);

			// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
			for (auto &child : children)
			{
				if (!child.widget->get_visible())
					continue;

				if (!child.widget->is_floating())
				{
					float desired_padding = max(
						child.widget->get_target_geometry().x - child.size.x, 0.0f);
					float padding = min(desired_padding, extra_width_per_object);
					child.size.x += padding;
					slack_width -= padding;
				}
			}
		}

		// Try to go "beyond" the target size for flexibly sized members.
		if (slack_width > 0.0f)
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
				float extra_width_per_object = max(muglm::floor(slack_width / padding_targets), 1.0f);

				// If we have some slack room, use at most extra_height_per_object to pad from minimum to target.
				for (auto &child : children)
				{
					if (!child.widget->get_visible())
						continue;

					if (!child.widget->is_floating() && child.widget->get_size_is_flexible())
						child.size.x += extra_width_per_object;
					slack_width -= extra_width_per_object;
				}
			}
		}

		for (auto &child : children)
		{
			if (!child.widget->get_visible())
				continue;

			if (!child.widget->is_floating())
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
	}
}

float HorizontalPacking::render(FlatRenderer &renderer, float layer, vec2 offset, vec2)
{
	return render_children(renderer, layer, offset);
}

void HorizontalPacking::reconfigure()
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
			minimum.y = max(child.widget->get_minimum_geometry().y, minimum.y);
			minimum.x += child.widget->get_minimum_geometry().x;

			target.y = max(child.widget->get_target_geometry().y, target.y);
			target.x += child.widget->get_target_geometry().x;
		}
	}

	if (!children.empty())
	{
		target.x += geometry.margin * (non_floating_count + 1);
		minimum.x += geometry.margin * (non_floating_count + 1);
	}

	target.y += 2.0f * geometry.margin;
	minimum.y += 2.0f * geometry.margin;

	geometry.target = target;
	geometry.minimum = minimum;
}
}
}
