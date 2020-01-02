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

struct ConservativeRasterApplication : Granite::Application, Granite::EventHandler
{
	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto info = ImageCreateInfo::render_target(4, 4, VK_FORMAT_R8G8B8A8_UNORM);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		auto image = device.create_image(info);

		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);

		RenderPassInfo rp;
		rp.num_color_attachments = 1;
		rp.color_attachments[0] = &image->get_view();
		rp.store_attachments = 1;
		rp.clear_attachments = 1;
		cmd->begin_render_pass(rp);

		cmd->set_opaque_state();
		cmd->set_conservative_rasterization(true);
		cmd->set_program("assets://shaders/triangle.vert", "assets://shaders/triangle.frag");

		auto *pos = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vec2) * 3, sizeof(vec2)));
		auto *color = static_cast<vec4 *>(cmd->allocate_vertex_data(1, sizeof(vec4) * 3, sizeof(vec4)));

		pos[0] = vec2(-1.0f, -1.0f);
		pos[1] = vec2(-1.0f, -0.95f);
		pos[2] = vec2(-0.95, -1.0f);
		color[0] = vec4(1.0f, 0.0f, 0.0f, 1.0f);
		color[1] = vec4(0.0f, 1.0f, 0.0f, 1.0f);
		color[2] = vec4(0.0f, 0.0f, 1.0f, 1.0f);

		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
		cmd->set_specialization_constant_mask(0xf);
		cmd->set_specialization_constant(0, 1.0f);
		cmd->set_specialization_constant(1, 1.0f);
		cmd->set_specialization_constant(2, 1.0f);
		cmd->set_specialization_constant(3, 1.0f);
		cmd->draw(3);

		cmd->end_render_pass();

		cmd->image_barrier(*image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, image->get_view(), StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle images[4];
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
		auto *app = new ConservativeRasterApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
