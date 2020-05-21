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
    ImageHandle cube_color;
	ImageHandle cube_sample;
	ImageViewHandle cube_view;

#define LAYERS 36

	void on_device_create(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(16, 16, VK_FORMAT_D32_SFLOAT);
		info.layers = 6 * LAYERS;
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		cube = e.get_device().create_image(info);

		ImageViewCreateInfo view = {};
		view.image = cube.get();
		view.view_type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		view.layers = 6 * LAYERS;
		view.levels = 1;
		cube_view = e.get_device().create_image_view(view);

#if 1
		info.format = VK_FORMAT_R8G8B8A8_UNORM;
		info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		cube_color = e.get_device().create_image(info);
#endif

		info = ImageCreateInfo::render_target(6, LAYERS, VK_FORMAT_R8G8B8A8_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		cube_sample = e.get_device().create_image(info);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		cube.reset();
		cube_sample.reset();
		cube_color.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*cube, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
#if 1
		cmd->image_barrier(*cube_color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
#endif

		for (unsigned slice = 0; slice < LAYERS; slice++)
		{
			for (unsigned face = 0; face < 6; face++)
			{
				RenderPassInfo cube_rp;
				cube_rp.base_layer = face + slice * 6;
				cube_rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
				cube_rp.depth_stencil = &cube->get_view();
				cube_rp.clear_depth_stencil.depth = 1.0f - 1.0f * float(cube_rp.base_layer) / float(LAYERS * 6);
				cube_rp.clear_color[0].float32[0] = cube_rp.clear_depth_stencil.depth;
				cube_rp.clear_color[0].float32[1] = 0.4f * cube_rp.clear_depth_stencil.depth;
				cube_rp.clear_color[0].float32[2] = 0.2f * cube_rp.clear_depth_stencil.depth;
				cube_rp.clear_color[0].float32[3] = 0.1f * cube_rp.clear_depth_stencil.depth;
#if 1
				cube_rp.num_color_attachments = 1;
				cube_rp.color_attachments[0] = &cube_color->get_view();
				cube_rp.clear_attachments = 1;
				cube_rp.store_attachments = 1;
#endif
				cmd->begin_render_pass(cube_rp);

#if 0
				VkClearRect clear_rect = {};
				clear_rect.layerCount = 1;
				clear_rect.rect.extent.width = 16;
				clear_rect.rect.extent.height = 16;
				VkClearValue clear_value = {};
				clear_value.depthStencil.depth = cube_rp.clear_depth_stencil.depth;
				//cmd->clear_quad(1, clear_rect, clear_value, VK_IMAGE_ASPECT_DEPTH_BIT);
#endif
				cmd->end_render_pass();
			}
		}

		cmd->image_barrier(*cube, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
#if 1
		cmd->image_barrier(*cube_color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
#endif
		cmd->image_barrier(*cube_sample, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		RenderPassInfo read_rp;
		read_rp.num_color_attachments = 1;
		read_rp.color_attachments[0] = &cube_sample->get_view();
		read_rp.store_attachments = 1;
		cmd->begin_render_pass(read_rp);
		cmd->set_texture(0, 0, *cube_view, StockSampler::NearestClamp);
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
