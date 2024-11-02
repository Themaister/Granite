/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
Image::Image(const std::string &path, vec2 target)
{
	texture = GRANITE_ASSET_MANAGER()->register_asset(
			*GRANITE_FILESYSTEM(), path,
			AssetClass::ImageColor);

	geometry.minimum = target;
	geometry.target = target;
}

void Image::reconfigure()
{
}

void Image::reconfigure_to_canvas(vec2, vec2 size)
{
	sprite_offset = vec2(0.0f);
	sprite_size = size;
}

float Image::render(FlatRenderer &renderer, float layer, vec2 offset, vec2)
{
	auto *view = renderer.get_device().get_resource_manager().get_image_view_blocking(texture);
	vec2 image_size(view->get_view_width(), view->get_view_height());

	renderer.render_textured_quad(
			*view, vec3(offset + sprite_offset, layer), sprite_size,
			vec2(0.0f), image_size, DrawPipeline::AlphaBlend, vec4(1.0f), sampler);
	return layer;
}
}
}
