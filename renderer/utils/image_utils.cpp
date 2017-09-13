/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "image_utils.hpp"
#include "transforms.hpp"
#include "device.hpp"
#include "render_parameters.hpp"
#include "util.hpp"

using namespace Vulkan;

namespace Granite
{
ImageHandle convert_equirect_to_cube(Device &device, ImageView &view)
{
	unsigned size = std::max(view.get_image().get_create_info().width / 3,
	                         view.get_image().get_create_info().height / 2);

	ImageCreateInfo info = ImageCreateInfo::render_target(size, size, view.get_format());
	info.levels = 0;
	info.layers = 6;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	auto handle = device.create_image(info, nullptr);
	auto cmd = device.request_command_buffer();

	static const vec3 dirs[6] = {
		vec3(1.0f, 0.0f, 0.0f),
		vec3(-1.0f, 0.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, -1.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f),
		vec3(0.0f, 0.0f, -1.0f),
	};

	static const vec3 ups[6] = {
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 0.0f, -1.0f),
		vec3(0.0f, 0.0f, +1.0f),
		vec3(0.0f, 1.0f, 0.0f),
		vec3(0.0f, 1.0f, 0.0f),
	};

	RenderParameters params = {};

	for (unsigned i = 0; i < 6; i++)
	{
		ImageViewCreateInfo view_info = {};
		view_info.layers = 1;
		view_info.base_layer = i;
		view_info.format = info.format;
		view_info.levels = 1;
		view_info.image = handle.get();
		auto rt_view = device.create_image_view(view_info);

		RenderPassInfo rp = {};
		rp.num_color_attachments = 1;
		rp.color_attachments[0] = rt_view.get();
		rp.store_attachments = 1;
		rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;

		cmd->begin_render_pass(rp);

		mat4 look = mat4_cast(look_at(dirs[i], ups[i]));
		mat4 proj = scale(vec3(-1.0f, 1.0f, 1.0f)) * projection(0.5f * pi<float>(), 1.0f, 0.1f, 100.0f);
		params.inv_local_view_projection = inverse(proj * look);
		memcpy(cmd->allocate_constant_data(0, 0, sizeof(params)), &params, sizeof(params));
		cmd->set_texture(2, 0, view, StockSampler::LinearWrap);

		vec4 color = vec4(1.0f);
		cmd->push_constants(&color, 0, sizeof(color));
		cmd->set_quad_state();
		CommandBufferUtil::set_quad_vertex_state(*cmd);
		CommandBufferUtil::draw_quad(*cmd, "builtin://shaders/skybox.vert", "builtin://shaders/skybox_latlon.frag", {{ "HAVE_EMISSIVE", 1 }});

		cmd->end_render_pass();
	}

	cmd->barrier_prepare_generate_mipmap(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, true);
	handle->set_layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	cmd->generate_mipmap(*handle);
	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	handle->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	device.submit(cmd);
	return handle;
}
}