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

struct MultiviewApplication : Application, EventHandler
{
	MultiviewApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(MultiviewApplication,
		                             on_device_created,
		                             on_device_destroyed,
		                             DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		auto rt = ImageCreateInfo::render_target(256, 64, VK_FORMAT_R8G8B8A8_UNORM);
		rt.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt.layers = 4;
		rt.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		multiview_rt = e.get_device().create_image(rt);

		VkDrawIndirectCommand initial = {};
		initial.vertexCount = 4;
		initial.instanceCount = 4;
		BufferCreateInfo info = {};
		info.size = sizeof(VkDrawIndirectCommand);
		info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		info.domain = BufferDomain::Device;
		indirect = e.get_device().create_buffer(info, &initial);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		multiview_rt.reset();
	}

	void render_frame(double, double) override
	{
		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		RenderPassInfo rp;
		rp.num_color_attachments = 1;
		rp.color_attachments[0] = &multiview_rt->get_view();
		rp.clear_attachments = 1;
		rp.store_attachments = 1;
		rp.base_layer = 1;
		rp.num_layers = 3;
		for (unsigned i = 0; i < 4; i++)
			rp.clear_color[0].float32[i] = 1.0f;

		cmd->image_barrier(*multiview_rt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

		cmd->clear_image(*multiview_rt, {});

		cmd->image_barrier(*multiview_rt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd->set_program("assets://shaders/multiview_quad.vert", "assets://shaders/multiview_quad.frag");
		auto *coord = static_cast<int8_t *>(cmd->allocate_vertex_data(0, 8, 2));
		coord[0] = -1;
		coord[1] = -1;
		coord[2] = -1;
		coord[3] = +1;
		coord[4] = +1;
		coord[5] = -1;
		coord[6] = +1;
		coord[7] = +1;
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R8G8_SNORM, 0);
		auto *base_positions = cmd->allocate_typed_constant_data<vec4>(0, 0, 4);
		base_positions[0] = vec4(-0.8f, -0.8f, 0.0f, 1.0f);
		base_positions[1] = vec4(-0.8f, +0.8f, 0.0f, 1.0f);
		base_positions[2] = vec4(+0.8f, -0.8f, 0.0f, 1.0f);
		base_positions[3] = vec4(+0.8f, +0.8f, 0.0f, 1.0f);
		//cmd->draw(4, 4);
		cmd->draw_indirect(*indirect, 0, 1, sizeof(VkDrawIndirectCommand));
		cmd->end_render_pass();

		cmd->image_barrier(*multiview_rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, multiview_rt->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/multiview_debug.frag");
		cmd->end_render_pass();

		device.submit(cmd);
	}

	ImageHandle multiview_rt;
	BufferHandle indirect;
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
		auto *app = new MultiviewApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
