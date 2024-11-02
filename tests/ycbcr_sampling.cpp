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

#include "application.hpp"
#include "command_buffer.hpp"
#include "device.hpp"
#include "os_filesystem.hpp"
#include "math.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct YCbCrSamplingTest : Granite::Application, Granite::EventHandler
{
	YCbCrSamplingTest(std::string path_, unsigned width_, unsigned height_)
		: path(std::move(path_)), width(width_), height(height_)
	{
		get_wsi().set_backbuffer_srgb(false);

		yuv_file = GRANITE_FILESYSTEM()->open_readonly_mapping(path);
		if (!yuv_file)
			throw std::runtime_error("Failed to open file.\n");

		file_mapped = yuv_file->data<uint8_t>();
		if (!file_mapped)
			throw std::runtime_error("Failed to map file.\n");

		file_offset = 0;
		file_size = yuv_file->get_size();

		EVENT_MANAGER_REGISTER_LATCH(YCbCrSamplingTest, on_module_created, on_module_destroyed, DeviceShaderModuleReadyEvent);
	}

	void on_module_created(const DeviceShaderModuleReadyEvent &e)
	{
		if (!e.get_device().get_device_features().vk11_features.samplerYcbcrConversion)
		{
			LOGE("YCbCr sampling not supported!\n");
			std::terminate();
		}

		VkSamplerYcbcrConversionCreateInfo conv = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
		conv.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
		conv.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
		conv.chromaFilter = VK_FILTER_LINEAR;
		conv.xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
		conv.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
		conv.format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
		conv.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		conv.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		conv.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		conv.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		conv.forceExplicitReconstruction = VK_FALSE;
		ycbcr = e.get_device().request_immutable_ycbcr_conversion(conv);

		SamplerCreateInfo samp = {};
		samp.mag_filter = VK_FILTER_LINEAR;
		samp.min_filter = VK_FILTER_LINEAR;
		samp.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samp.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samp.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samp.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler = e.get_device().request_immutable_sampler(samp, ycbcr);

		ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.ycbcr_conversion = ycbcr;
		ycbcr_image = e.get_device().create_image(info);

		if (!ycbcr_image)
		{
			LOGE("Failed to create YCbCr image!\n");
			std::terminate();
		}
	}

	void on_module_destroyed(const DeviceShaderModuleReadyEvent &)
	{
		ycbcr_image.reset();
	}

	void render_frame(double, double)
	{
		constexpr unsigned DOWNSAMPLE_WIDTH = 2;
		constexpr unsigned DOWNSAMPLE_HEIGHT = 2;

		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		size_t required_size = width * height + 2 * ((width / DOWNSAMPLE_WIDTH) * (height / DOWNSAMPLE_HEIGHT));
		if (file_offset + required_size > file_size)
			file_offset = 0;

		cmd->image_barrier(*ycbcr_image, VK_IMAGE_LAYOUT_UNDEFINED,
		                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		uint8_t *y = static_cast<uint8_t *>(
				cmd->update_image(*ycbcr_image, {}, { width, height, 1 },
				                  0, 0,
				                  { VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1 }));

		uint8_t *cb = static_cast<uint8_t *>(
				cmd->update_image(*ycbcr_image, {}, { width / DOWNSAMPLE_WIDTH, height / DOWNSAMPLE_HEIGHT, 1 },
				                  0, 0,
				                  { VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1 }));

		uint8_t *cr = static_cast<uint8_t *>(
				cmd->update_image(*ycbcr_image, {}, { width / DOWNSAMPLE_WIDTH, height / DOWNSAMPLE_HEIGHT, 1 },
				                  0, 0,
				                  { VK_IMAGE_ASPECT_PLANE_2_BIT, 0, 0, 1 }));

		memcpy(y, file_mapped + file_offset, width * height);
		file_offset += width * height;
		memcpy(cb, file_mapped + file_offset, (width / DOWNSAMPLE_WIDTH) * (height / DOWNSAMPLE_HEIGHT));
		file_offset += (width / DOWNSAMPLE_WIDTH) * (height / DOWNSAMPLE_HEIGHT);
		memcpy(cr, file_mapped + file_offset, (width / DOWNSAMPLE_WIDTH) * (height / DOWNSAMPLE_HEIGHT));
		file_offset += (width / DOWNSAMPLE_WIDTH) * (height / DOWNSAMPLE_HEIGHT);

		cmd->image_barrier(*ycbcr_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.2f;
		cmd->begin_render_pass(rp);

		cmd->set_quad_state();
		CommandBufferUtil::set_fullscreen_quad_vertex_state(*cmd);
		auto *program = device.get_shader_manager().register_graphics("builtin://shaders/quad.vert", "builtin://shaders/blit.frag");

		ImmutableSamplerBank immutable_bank = {};
		immutable_bank.samplers[0][0] = sampler;
		auto *variant = program->register_variant({}, &immutable_bank);

		cmd->set_program(variant->get_program());
		cmd->set_texture(0, 0, ycbcr_image->get_view());
		CommandBufferUtil::draw_fullscreen_quad(*cmd);
		cmd->end_render_pass();
		device.submit(cmd);
	}

	std::string path;
	unsigned width;
	unsigned height;

	Granite::FileMappingHandle yuv_file;
	size_t file_offset = 0;
	size_t file_size = 0;
	const uint8_t *file_mapped = nullptr;

	ImageHandle ycbcr_image;
	const ImmutableYcbcrConversion *ycbcr = nullptr;
	const ImmutableSampler *sampler = nullptr;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	if (argc != 4)
	{
		LOGE("Usage: ycbcr-sampling <path to raw yuv420p> <width> <height>\n");
		return nullptr;
	}

	unsigned width = std::stoi(argv[2]);
	unsigned height = std::stoi(argv[3]);
	std::string path = argv[1];

	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new YCbCrSamplingTest(path, width, height);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
