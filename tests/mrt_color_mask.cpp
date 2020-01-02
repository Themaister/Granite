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

struct MRTColorMaskApplication : Application, EventHandler
{
	MRTColorMaskApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(MRTColorMaskApplication,
		                             on_device_created,
		                             on_device_destroyed,
		                             DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		auto rt = ImageCreateInfo::render_target(256, 64, VK_FORMAT_R8G8B8A8_UNORM);
		rt.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		for (auto &mrt : mrts)
			mrt = e.get_device().create_image(rt);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		for (auto &mrt : mrts)
			mrt.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		RenderPassInfo rp;
		rp.num_color_attachments = 4;
		for (unsigned i = 0; i < 4; i++)
			rp.color_attachments[i] = &mrts[i]->get_view();
		rp.clear_attachments = 0xf;
		rp.store_attachments = 0xf;
		for (unsigned i = 0; i < 4; i++)
			for (unsigned j = 0; j < 4; j++)
				rp.clear_color[i].float32[j] = 1.0f;

		for (auto &mrt : mrts)
		{
			cmd->image_barrier(*mrt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		}

		cmd->begin_render_pass(rp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/mrt_quad.frag");
		cmd->end_render_pass();

		for (auto &mrt : mrts)
		{
			cmd->image_barrier(*mrt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		}

		rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, mrts[0]->get_view(), StockSampler::NearestClamp);
		cmd->set_texture(0, 1, mrts[1]->get_view(), StockSampler::NearestClamp);
		cmd->set_texture(0, 2, mrts[2]->get_view(), StockSampler::NearestClamp);
		cmd->set_texture(0, 3, mrts[3]->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/mrt_debug.frag");
		cmd->end_render_pass();

		device.submit(cmd);
	}

	ImageHandle mrts[4];
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
		auto *app = new MRTColorMaskApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
