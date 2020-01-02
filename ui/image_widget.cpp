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

#include "image_widget.hpp"
#include "device.hpp"
#include "flat_renderer.hpp"
#include "widget.hpp"

using namespace Vulkan;

namespace Granite
{
namespace UI
{
Image::Image(const std::string &path_)
	: path(path_)
{
	EVENT_MANAGER_REGISTER_LATCH(Image, on_device_created, on_device_destroyed, DeviceCreatedEvent);
}

void Image::reconfigure()
{
}

void Image::on_device_created(const DeviceCreatedEvent &created)
{
	auto &device = created.get_device();
	texture = device.get_texture_manager().request_texture(path);

	auto &create_info = texture->get_image()->get_create_info();
	geometry.minimum = vec2(create_info.width, create_info.height);
	geometry.target = vec2(create_info.width, create_info.height);
}

void Image::reconfigure_to_canvas(vec2, vec2 size)
{
	sprite_offset = vec2(0.0f);
	sprite_size = size;

	auto &create_info = texture->get_image()->get_create_info();
	image_size = vec2(create_info.width, create_info.height);

	if (keep_aspect)
	{
		float target_aspect = image_size.x / image_size.y;
		float canvas_aspect = size.x / size.y;

		if (muglm::abs(canvas_aspect / target_aspect - 1.0f) > 0.001f)
		{
			if (canvas_aspect > target_aspect)
			{
				float width = muglm::round(size.y * target_aspect);
				float bias = 0.5f * (size.x - width);
				sprite_offset.x = muglm::round(sprite_offset.x + bias);
				sprite_size.x = width;
			}
			else
			{
				float height = muglm::round(size.x / target_aspect);
				float bias = 0.5f * (size.y - height);
				sprite_offset.y = muglm::round(sprite_offset.y + bias);
				sprite_size.y = height;
			}
		}
	}
}

float Image::render(FlatRenderer &renderer, float layer, vec2 offset, vec2)
{
	renderer.render_textured_quad(texture->get_image()->get_view(), vec3(offset + sprite_offset, layer), sprite_size,
	                              vec2(0.0f), image_size, DrawPipeline::AlphaBlend, vec4(1.0f), sampler);
	return layer;
}

void Image::on_device_destroyed(const DeviceCreatedEvent &)
{
	texture = nullptr;
}
}
}
