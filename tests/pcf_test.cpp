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

struct PCFTest : Granite::Application, Granite::EventHandler
{
	PCFTest()
	{
		EVENT_MANAGER_REGISTER_LATCH(PCFTest, on_device_create, on_device_destroy, DeviceCreatedEvent);
	}

	ImageHandle depth_buffer;

	void on_device_create(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(16, 16, VK_FORMAT_D16_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		depth_buffer = e.get_device().create_image(info);
	}

	void on_device_destroy(const DeviceCreatedEvent &)
	{
		depth_buffer.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		RenderPassInfo depth_rp;
		depth_rp.depth_stencil = &depth_buffer->get_view();
		depth_rp.op_flags = RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT;
		cmd->image_barrier(*depth_buffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
		cmd->begin_render_pass(depth_rp);
		CommandBufferUtil::draw_fullscreen_quad_depth(*cmd, "builtin://shaders/quad.vert",
		                                              "assets://shaders/fill_depth_checkerboard.frag",
		                                              true, true, VK_COMPARE_OP_ALWAYS);
		cmd->end_render_pass();
		cmd->image_barrier(*depth_buffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, depth_buffer->get_view(), StockSampler::NearestClamp);
		cmd->set_texture(1, 1, depth_buffer->get_view(), StockSampler::LinearShadow);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert",
		                                        "assets://shaders/sample_pcf.frag");
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
		auto *app = new PCFTest();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}