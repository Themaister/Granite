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

		yuv_file = Global::filesystem()->open(path, FileMode::ReadOnly);
		if (!yuv_file)
			throw std::runtime_error("Failed to open file.\n");

		file_mapped = static_cast<const uint8_t *>(yuv_file->map());
		if (!file_mapped)
			throw std::runtime_error("Failed to map file.\n");

		file_offset = 0;
		file_size = yuv_file->get_size();

		EVENT_MANAGER_REGISTER_LATCH(YCbCrSamplingTest, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		if (!e.get_device().get_device_features().sampler_ycbcr_conversion_features.samplerYcbcrConversion)
		{
			LOGE("YCbCr sampling not supported!\n");
			std::terminate();
		}
#if 0
		YCbCrImageCreateInfo info;
		info.format = YCbCrFormat::YUV420P_3PLANE;
		info.width = 16;
		info.height = 16;
		ycbcr_image = e.get_device().create_ycbcr_image(info);
#else
		ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ycbcr_image = e.get_device().create_image(info);
#endif

		if (!ycbcr_image)
		{
			LOGE("Failed to create YCbCr image!\n");
			std::terminate();
		}

		auto cmd = e.get_device().request_command_buffer();


		e.get_device().submit(cmd);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
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

#if 0
		cmd->image_barrier(ycbcr_image->get_ycbcr_image(), VK_IMAGE_LAYOUT_UNDEFINED,
		                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		// Should be gray.
		VkClearValue clear_value = {};
		clear_value.color.float32[0] = 1.0f;
		cmd->clear_image(ycbcr_image->get_plane_image(0), clear_value);

		clear_value.color.float32[0] = 0.5f;
		cmd->clear_image(ycbcr_image->get_plane_image(1), clear_value);

		clear_value.color.float32[0] = 0.5f;
		cmd->clear_image(ycbcr_image->get_plane_image(2), clear_value);

		cmd->image_barrier(ycbcr_image->get_ycbcr_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
#else
		cmd->image_barrier(*ycbcr_image, VK_IMAGE_LAYOUT_UNDEFINED,
		                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

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
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
#endif

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
#if 0
		cmd->set_texture(0, 0, ycbcr_image->get_ycbcr_image().get_view());
#else
		cmd->set_texture(0, 0, ycbcr_image->get_view());
#endif
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/yuv420p-sample.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}

	std::string path;
	unsigned width;
	unsigned height;

	std::unique_ptr<Granite::File> yuv_file;
	size_t file_offset = 0;
	size_t file_size = 0;
	const uint8_t *file_mapped = nullptr;

#if 0
	YCbCrImageHandle ycbcr_image;
#else
	ImageHandle ycbcr_image;
#endif
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	application_dummy();

	if (argc != 4)
	{
		LOGE("Usage: ycbcr-sampling <path to raw yuv420p> <width> <height>\n");
		return nullptr;
	}

	unsigned width = std::stoi(argv[2]);
	unsigned height = std::stoi(argv[3]);
	std::string path = argv[1];

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

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
