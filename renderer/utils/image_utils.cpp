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

#include "image_utils.hpp"
#include "transforms.hpp"
#include "device.hpp"
#include "render_parameters.hpp"
#include "logging.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include "memory_mapped_texture.hpp"
#include <string.h>

using namespace Vulkan;

namespace Granite
{
ImageHandle convert_cube_to_ibl_specular(Device &device, ImageView &view)
{
	unsigned size = 128;
	float base_sample_lod = log2(float(std::max(view.get_image().get_create_info().width,
	                                            view.get_image().get_create_info().height))) - 7.0f;

	ImageCreateInfo info = ImageCreateInfo::render_target(size, size, VK_FORMAT_R16G16B16A16_SFLOAT);
	info.levels = 8;
	info.layers = 6;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	auto handle = device.create_image(info, nullptr);
	auto cmd = device.request_command_buffer();

	RenderParameters params = {};

	for (unsigned layer = 0; layer < 6; layer++)
	{
		for (unsigned level = 0; level < info.levels; level++)
		{
			ImageViewCreateInfo view_info = {};
			view_info.layers = 1;
			view_info.base_layer = layer;
			view_info.base_level = level;
			view_info.format = info.format;
			view_info.levels = 1;
			view_info.image = handle.get();
			auto rt_view = device.create_image_view(view_info);

			RenderPassInfo rp = {};
			rp.num_color_attachments = 1;
			rp.color_attachments[0] = rt_view.get();
			rp.store_attachments = 1;

			cmd->begin_render_pass(rp);

			mat4 look, proj;
			compute_cube_render_transform(vec3(0.0f), layer, proj, look, 0.1f, 100.0f);
			params.inv_local_view_projection = inverse(proj * look);
			memcpy(cmd->allocate_constant_data(0, 0, sizeof(params)), &params, sizeof(params));
			cmd->set_texture(2, 0, view, StockSampler::TrilinearWrap);

			struct Push
			{
				float lod;
				float roughness;
			};

			float sample_lod = base_sample_lod + level;
			Push push = { sample_lod, mix(0.001f, 1.0f, float(level) / (info.levels - 1)) };
			cmd->push_constants(&push, 0, sizeof(push));

			CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/skybox.vert",
			                                        "builtin://shaders/util/ibl_specular.frag");

			cmd->end_render_pass();
		}
	}

	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	device.submit(cmd);
	return handle;
}

ImageHandle convert_cube_to_ibl_diffuse(Device &device, ImageView &view)
{
	unsigned size = 32;

	float sample_lod = log2(float(size)) - 5.0f;

	ImageCreateInfo info = ImageCreateInfo::render_target(size, size, VK_FORMAT_R16G16B16A16_SFLOAT);
	info.levels = 1;
	info.layers = 6;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	auto handle = device.create_image(info, nullptr);
	auto cmd = device.request_command_buffer();

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

		cmd->begin_render_pass(rp);

		mat4 look, proj;
		compute_cube_render_transform(vec3(0.0f), i, proj, look, 0.1f, 100.0f);

		params.inv_local_view_projection = inverse(proj * look);
		memcpy(cmd->allocate_constant_data(0, 0, sizeof(params)), &params, sizeof(params));
		cmd->set_texture(2, 0, view, StockSampler::LinearWrap);

		cmd->push_constants(&sample_lod, 0, sizeof(sample_lod));
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/skybox.vert",
		                                        "builtin://shaders/util/ibl_diffuse.frag");

		cmd->end_render_pass();
	}

	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	device.submit(cmd);
	return handle;
}

ImageHandle convert_equirect_to_cube(Device &device, ImageView &view, float scale)
{
	unsigned size = unsigned(scale * std::max(view.get_image().get_create_info().width / 3,
	                                          view.get_image().get_create_info().height / 2));

	ImageCreateInfo info = ImageCreateInfo::render_target(size, size, view.get_format());
	info.levels = 0;
	info.layers = 6;
	info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	auto handle = device.create_image(info, nullptr);
	auto cmd = device.request_command_buffer();

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

		cmd->begin_render_pass(rp);

		mat4 look, proj;
		compute_cube_render_transform(vec3(0.0f), i, proj, look, 0.1f, 100.0f);

		params.inv_local_view_projection = inverse(proj * look);
		memcpy(cmd->allocate_constant_data(0, 0, sizeof(params)), &params, sizeof(params));
		cmd->set_texture(2, 0, view, StockSampler::LinearWrap);

		vec4 color = vec4(1.0f);
		cmd->push_constants(&color, 0, sizeof(color));
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/skybox.vert", "builtin://shaders/skybox_latlon.frag", {{ "HAVE_EMISSIVE", 1 }});

		cmd->end_render_pass();
	}

	cmd->barrier_prepare_generate_mipmap(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, true);
	cmd->generate_mipmap(*handle);
	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

	device.submit(cmd);
	return handle;
}

ImageReadback save_image_to_cpu_buffer(Vulkan::Device &device, const Vulkan::Image &image, CommandBuffer::Type type)
{
	ImageReadback readback;
	readback.create_info = image.get_create_info();

	switch (image.get_create_info().type)
	{
	case VK_IMAGE_TYPE_1D:
		readback.layout.set_1d(image.get_format(), image.get_create_info().width, image.get_create_info().layers, image.get_create_info().levels);
		break;

	case VK_IMAGE_TYPE_2D:
		readback.layout.set_2d(image.get_format(), image.get_create_info().width, image.get_create_info().height, image.get_create_info().layers, image.get_create_info().levels);
		break;

	case VK_IMAGE_TYPE_3D:
		readback.layout.set_3d(image.get_format(), image.get_create_info().width, image.get_create_info().height, image.get_create_info().depth, image.get_create_info().levels);
		break;

	default:
		return {};
	}

	Util::SmallVector<VkBufferImageCopy, 32> blits;
	readback.layout.build_buffer_image_copies(blits);

	BufferCreateInfo buffer_info;
	buffer_info.size = readback.layout.get_required_size();
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.domain = BufferDomain::CachedHost;
	readback.buffer = device.create_buffer(buffer_info, nullptr);

	auto cmd = device.request_command_buffer(type);
	cmd->copy_image_to_buffer(*readback.buffer, image, blits.size(), blits.data());
	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	device.submit(cmd, &readback.fence);
	return readback;
}

bool save_image_buffer_to_gtx(Vulkan::Device &device, ImageReadback &readback, const char *path)
{
	auto &info = readback.create_info;
	if (info.format == VK_FORMAT_UNDEFINED)
	{
		LOGE("Unsupported format!\n");
		return false;
	}

	SceneFormats::MemoryMappedTexture tex;

	if (info.levels == 1)
		tex.set_generate_mipmaps_on_load();

	switch (info.type)
	{
	case VK_IMAGE_TYPE_1D:
		tex.set_1d(info.format, info.width, info.layers, info.levels);
		break;

	case VK_IMAGE_TYPE_2D:
		if (info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
			tex.set_cube(info.format, info.width, info.layers / 6, info.levels);
		else
			tex.set_2d(info.format, info.width, info.height, info.layers, info.levels);
		break;

	case VK_IMAGE_TYPE_3D:
		tex.set_3d(info.format, info.width, info.height, info.depth, info.levels);
		break;

	default:
		return false;
	}

	if (!tex.map_write(path))
	{
		LOGE("Failed to save texture to %s\n", path);
		return false;
	}

	readback.fence->wait();
	void *ptr = device.map_host_buffer(*readback.buffer, MEMORY_ACCESS_READ_BIT);
	memcpy(tex.get_layout().data(), ptr, tex.get_layout().get_required_size());
	device.unmap_host_buffer(*readback.buffer, MEMORY_ACCESS_READ_BIT);

	return true;
}
}