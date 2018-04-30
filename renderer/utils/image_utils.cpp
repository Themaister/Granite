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
#include "gli/save.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/func.hpp"

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
			rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;

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

			cmd->set_quad_state();
			CommandBufferUtil::set_quad_vertex_state(*cmd);
			CommandBufferUtil::draw_quad(*cmd, "builtin://shaders/skybox.vert",
			                             "builtin://shaders/util/ibl_specular.frag");

			cmd->end_render_pass();
		}
	}

	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	handle->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	Fence fence;
	device.submit(cmd, &fence);
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
		rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;

		cmd->begin_render_pass(rp);

		mat4 look, proj;
		compute_cube_render_transform(vec3(0.0f), i, proj, look, 0.1f, 100.0f);

		params.inv_local_view_projection = inverse(proj * look);
		memcpy(cmd->allocate_constant_data(0, 0, sizeof(params)), &params, sizeof(params));
		cmd->set_texture(2, 0, view, StockSampler::LinearWrap);

		cmd->push_constants(&sample_lod, 0, sizeof(sample_lod));
		cmd->set_quad_state();
		CommandBufferUtil::set_quad_vertex_state(*cmd);
		CommandBufferUtil::draw_quad(*cmd, "builtin://shaders/skybox.vert", "builtin://shaders/util/ibl_diffuse.frag");

		cmd->end_render_pass();
	}

	cmd->image_barrier(*handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	handle->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	Fence fence;
	device.submit(cmd, &fence);
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
		rp.op_flags = RENDER_PASS_OP_COLOR_OPTIMAL_BIT;

		cmd->begin_render_pass(rp);

		mat4 look, proj;
		compute_cube_render_transform(vec3(0.0f), i, proj, look, 0.1f, 100.0f);

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

ImageReadback save_image_to_cpu_buffer(Vulkan::Device &device, const Vulkan::Image &image)
{
	ImageReadback readback;
	readback.create_info = image.get_create_info();
	assert(image.get_layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || image.get_layout() == VK_IMAGE_LAYOUT_GENERAL);

	readback.levels.resize(readback.create_info.levels);

	size_t offset = 0;

	for (unsigned i = 0; i < readback.create_info.levels; i++)
	{
		readback.levels[i].width = max(readback.create_info.width >> i, 1u);
		readback.levels[i].height = max(readback.create_info.height >> i, 1u);
		readback.levels[i].depth = max(readback.create_info.depth >> i, 1u);
		readback.levels[i].layer_offsets.resize(readback.create_info.layers);
		size_t layer_size = format_get_layer_size(readback.create_info.format, readback.levels[i].width, readback.levels[i].height, readback.levels[i].depth);

		for (unsigned l = 0; l < readback.create_info.layers; l++)
		{
			readback.levels[i].layer_offsets[l] = offset;
			offset += layer_size;
			offset = (offset + 63) & ~63;
		}
	}

	BufferCreateInfo info = {};
	info.size = offset;
	info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	info.domain = BufferDomain::CachedHost;
	readback.buffer = device.create_buffer(info, nullptr);

	auto cmd = device.request_command_buffer();

	for (unsigned i = 0; i < readback.create_info.levels; i++)
	{
		for (unsigned l = 0; l < readback.create_info.layers; l++)
		{
			cmd->copy_image_to_buffer(*readback.buffer, image, readback.levels[i].layer_offsets[l],
			                          {}, { readback.levels[i].width, readback.levels[i].height, readback.levels[i].depth },
			                          0, 0, { format_to_aspect_mask(readback.create_info.format), i, l, 1 });
		}
	}

	cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	device.submit(cmd, &readback.fence);
	return readback;
}

// Expand as needed.
static gli::format vk_format_to_gli(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		return gli::FORMAT_RG11B10_UFLOAT_PACK32;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return gli::FORMAT_RGBA16_SFLOAT_PACK16;
	case VK_FORMAT_R16G16B16_SFLOAT:
		return gli::FORMAT_RGB16_SFLOAT_PACK16;
	case VK_FORMAT_R16G16_SFLOAT:
		return gli::FORMAT_RG16_SFLOAT_PACK16;
	case VK_FORMAT_R16_SFLOAT:
		return gli::FORMAT_R16_SFLOAT_PACK16;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return gli::FORMAT_RGBA32_SFLOAT_PACK32;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return gli::FORMAT_RGB32_SFLOAT_PACK32;
	case VK_FORMAT_R32G32_SFLOAT:
		return gli::FORMAT_RG32_SFLOAT_PACK32;
	case VK_FORMAT_R32_SFLOAT:
		return gli::FORMAT_R32_SFLOAT_PACK32;
	default:
		return gli::FORMAT_UNDEFINED;
	}
}

bool save_image_buffer_to_ktx(Vulkan::Device &device, ImageReadback &readback, const char *path)
{
	auto format = vk_format_to_gli(readback.create_info.format);
	if (format == gli::FORMAT_UNDEFINED)
	{
		LOGE("Unsupported format!\n");
		return false;
	}

	readback.fence->wait();

	void *ptr = device.map_host_buffer(*readback.buffer, MEMORY_ACCESS_READ);
	device.unmap_host_buffer(*readback.buffer);

	unsigned layers = readback.create_info.layers;
	unsigned faces = 1;
	bool cube = false;

	if (readback.create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
	{
		assert((layers % 6) == 0);
		layers /= 6;
		faces = 6;
		cube = true;
	}

	gli::target target;
	switch (readback.create_info.type)
	{
	case VK_IMAGE_TYPE_1D:
		target = layers > 1 ? gli::TARGET_1D_ARRAY : gli::TARGET_1D;
		break;

	case VK_IMAGE_TYPE_2D:
		if (cube)
			target = layers > 1 ? gli::TARGET_CUBE_ARRAY : gli::TARGET_CUBE;
		else
			target = layers > 1 ? gli::TARGET_2D_ARRAY : gli::TARGET_2D;
		break;

	case VK_IMAGE_TYPE_3D:
		target = gli::TARGET_3D;
		break;

	default:
		return false;
	}

	gli::texture tex(target, format,
	                 { readback.create_info.width, readback.create_info.height, readback.create_info.depth },
	                 layers, faces, readback.create_info.levels);

	for (unsigned layer = 0; layer < layers; layer++)
	{
		for (unsigned face = 0; face < faces; face++)
		{
			for (unsigned level = 0; level < readback.create_info.levels; level++)
			{
				size_t layer_size = format_get_layer_size(readback.create_info.format,
				                                          readback.levels[level].width,
				                                          readback.levels[level].height,
				                                          readback.levels[level].depth);

				void *dst = tex.data(layer, face, level);
				const uint8_t *src = static_cast<const uint8_t *>(ptr) + readback.levels[level].layer_offsets[face + layer * faces];
				memcpy(dst, src, layer_size);
			}
		}
	}

	if (!gli::save(tex, path))
	{
		LOGE("Failed to save texture to %s\n", path);
		return false;
	}

	return true;
}
}