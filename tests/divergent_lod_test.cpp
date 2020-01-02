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
#include "cli_parser.hpp"

using namespace Granite;
using namespace Vulkan;

struct DivergentLOD : Granite::Application, Granite::EventHandler
{
	DivergentLOD()
	{
		EVENT_MANAGER_REGISTER_LATCH(DivergentLOD, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(2, 2, VK_FORMAT_R32G32B32A32_SFLOAT);
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		render_target = e.get_device().create_image(info, nullptr);

		ImageCreateInfo image_info = ImageCreateInfo::immutable_2d_image(8, 8, VK_FORMAT_R8G8B8A8_UNORM);
		image_info.levels = 4;

		uint32_t red_color[8 * 8];
		for (auto &r : red_color)
			r = 0xff;
		uint32_t green_color[8 * 8];
		for (auto &r : green_color)
			r = 0xff00;
		uint32_t blue_color[8 * 8];
		for (auto &r : blue_color)
			r = 0xff0000;
		uint32_t yellow_color[8 * 8];
		for (auto &r : yellow_color)
			r = 0xffff;

		ImageInitialData initial_data[4] = {};
		initial_data[0].data = red_color;
		initial_data[1].data = green_color;
		initial_data[2].data = blue_color;
		initial_data[3].data = yellow_color;
		texture = e.get_device().create_image(image_info, initial_data);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		render_target.reset();
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		RenderPassInfo rp_info;
		rp_info.clear_color[0].float32[0] = 1.0f;
		rp_info.clear_color[0].float32[1] = 1.0f;
		rp_info.clear_color[0].float32[2] = 1.0f;
		rp_info.clear_color[0].float32[3] = 1.0f;
		rp_info.num_color_attachments = 1;
		rp_info.color_attachments[0] = &render_target->get_view();
		rp_info.store_attachments = 1;
		rp_info.clear_attachments = 1;

		auto cmd = device.request_command_buffer();

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		cmd->begin_render_pass(rp_info);
		vec4 *weights = cmd->allocate_typed_constant_data<vec4>(0, 0, 4);
		weights[0] = vec4(1.0f);
		weights[1] = vec4(0.0f);
		weights[2] = vec4(0.0f);
		weights[3] = vec4(1.0f);
		cmd->set_texture(0, 1, texture->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/divergent_lod.frag");
		cmd->end_render_pass();

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		BufferCreateInfo readback_buffer_info = {};
		readback_buffer_info.domain = BufferDomain::CachedHost;
		readback_buffer_info.size = 4 * sizeof(vec4);
		readback_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto readback_buffer = get_wsi().get_device().create_buffer(readback_buffer_info);
		cmd->copy_image_to_buffer(*readback_buffer, *render_target, 0, {}, { 2, 2, 1 }, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		cmd->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, render_target->get_view(), StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();

		Fence fence;
		device.submit(cmd, &fence);
		fence->wait();

		auto *color = static_cast<const vec4 *>(device.map_host_buffer(*readback_buffer, MEMORY_ACCESS_READ_BIT));
		LOGI("[0, 0] = color: %f, %f, %f, %f\n", color[0].x, color[0].y, color[0].z, color[0].w);
		LOGI("[0, 1] = color: %f, %f, %f, %f\n", color[1].x, color[1].y, color[1].z, color[1].w);
		LOGI("[1, 0] = color: %f, %f, %f, %f\n", color[2].x, color[2].y, color[2].z, color[2].w);
		LOGI("[1, 1] = color: %f, %f, %f, %f\n", color[3].x, color[3].y, color[3].z, color[3].w);
		device.unmap_host_buffer(*readback_buffer, MEMORY_ACCESS_READ_BIT);
	}

	ImageHandle render_target;
	ImageHandle texture;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	application_dummy();

#ifdef ASSET_DIRECTORY
	const char *asset_dir = getenv("ASSET_DIRECTORY");
	if (!asset_dir)
		asset_dir = ASSET_DIRECTORY;

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	Util::CLICallbacks cbs;
	Util::CLIParser parser(std::move(cbs), argc - 1, argv + 1);

	try
	{
		auto *app = new DivergentLOD();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
