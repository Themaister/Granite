/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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
#include "muglm/muglm_impl.hpp"

using namespace Granite;
using namespace Vulkan;

struct CubeArrayTest : Granite::Application, Granite::EventHandler
{
	CubeArrayTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(CubeArrayTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	ImageHandle cube;
	ImageHandle cube_sample;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(16, 16, VK_FORMAT_R8G8B8A8_UNORM);
		info.layers = 6 * 256;
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		cube = e.get_device().create_image(info);

		info = ImageCreateInfo::render_target(6, 256, VK_FORMAT_R8G8B8A8_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		cube_sample = e.get_device().create_image(info);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		cube.reset();
		cube_sample.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*cube, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		for (unsigned slice = 0; slice < 256; slice++)
		{
			for (unsigned face = 0; face < 6; face++)
			{
				RenderPassInfo cube_rp;
				cube_rp.layer = face + slice * 6;
				cube_rp.clear_attachments = 1;
				cube_rp.store_attachments = 1;
				cube_rp.num_color_attachments = 1;
				cube_rp.color_attachments[0] = &cube->get_view();
				cube_rp.clear_color[0].float32[0] = float(slice) / 255.0f;
				cube_rp.clear_color[0].float32[1] = float(slice) / 255.0f;
				cube_rp.clear_color[0].float32[2] = float(slice) / 255.0f;
				cube_rp.clear_color[0].float32[3] = float(slice) / 255.0f;

				switch (face)
				{
				case 0:
					cube_rp.clear_color[0].float32[0] *= 1.0f;
					cube_rp.clear_color[0].float32[1] *= 0.0f;
					cube_rp.clear_color[0].float32[2] *= 0.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				case 1:
					cube_rp.clear_color[0].float32[0] *= 0.0f;
					cube_rp.clear_color[0].float32[1] *= 1.0f;
					cube_rp.clear_color[0].float32[2] *= 0.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				case 2:
					cube_rp.clear_color[0].float32[0] *= 0.0f;
					cube_rp.clear_color[0].float32[1] *= 0.0f;
					cube_rp.clear_color[0].float32[2] *= 1.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				case 3:
					cube_rp.clear_color[0].float32[0] *= 1.0f;
					cube_rp.clear_color[0].float32[1] *= 1.0f;
					cube_rp.clear_color[0].float32[2] *= 0.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				case 4:
					cube_rp.clear_color[0].float32[0] *= 1.0f;
					cube_rp.clear_color[0].float32[1] *= 0.0f;
					cube_rp.clear_color[0].float32[2] *= 1.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				case 5:
					cube_rp.clear_color[0].float32[0] *= 1.0f;
					cube_rp.clear_color[0].float32[1] *= 1.0f;
					cube_rp.clear_color[0].float32[2] *= 1.0f;
					cube_rp.clear_color[0].float32[3] *= 0.0f;
					break;
				}
				cmd->begin_render_pass(cube_rp);
				cmd->end_render_pass();
			}
		}

		cmd->image_barrier(*cube, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		cmd->image_barrier(*cube_sample, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		RenderPassInfo read_rp;
		read_rp.num_color_attachments = 1;
		read_rp.color_attachments[0] = &cube_sample->get_view();
		read_rp.store_attachments = 1;
		cmd->begin_render_pass(read_rp);
		cmd->set_texture(0, 0, cube->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert",
		                                        "assets://shaders/sample_cube_array.frag");
		cmd->end_render_pass();

		cmd->image_barrier(*cube_sample, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, cube_sample->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert",
		                                        "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int, char **)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new CubeArrayTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}