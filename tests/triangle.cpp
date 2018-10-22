/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
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
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct TriangleApplication : Granite::Application, Granite::EventHandler
{
	TriangleApplication()
	{
		EVENT_MANAGER_REGISTER_LATCH(TriangleApplication, on_device_created, on_device_destroyed, DeviceCreatedEvent);
	}

	void on_device_created(const DeviceCreatedEvent &e)
	{
		ImageCreateInfo info = ImageCreateInfo::render_target(1280, 720, VK_FORMAT_B8G8R8A8_SRGB);
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		render_target = e.get_device().create_image(info, nullptr);
	}

	void on_device_destroyed(const DeviceCreatedEvent &)
	{
		render_target.reset();
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		RenderPassInfo incremental_rp;
		incremental_rp.num_color_attachments = 1;
		incremental_rp.color_attachments[0] = &render_target->get_view();
		incremental_rp.depth_stencil = &device.get_transient_attachment(1280, 720, device.get_default_depth_format());
		incremental_rp.clear_attachments = 1;
		incremental_rp.store_attachments = 0;
		incremental_rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;

		auto before = CommandBuffer::request_secondary_command_buffer(device, incremental_rp, 0, 0);
		auto after = CommandBuffer::request_secondary_command_buffer(device, incremental_rp, 0, 0);

		VkClearValue clear_value = {};
		VkClearRect clear_rect = {};
		clear_rect.baseArrayLayer = 0;
		clear_rect.layerCount = 1;

		clear_rect.rect.offset = { 100, 100 };
		clear_rect.rect.extent = { 200, 200 };
		clear_value.color.float32[0] = 1.0f;
		clear_value.color.float32[1] = 0.0f;
		clear_value.color.float32[2] = 1.0f;
		clear_value.color.float32[3] = 1.0f;
		before->clear_quad(0, clear_rect, clear_value, VK_IMAGE_ASPECT_COLOR_BIT);
		clear_value.depthStencil.depth = 0.5f;
		before->clear_quad(1, clear_rect, clear_value, VK_IMAGE_ASPECT_DEPTH_BIT);

		vec4 color_depth;
		color_depth = vec4(1.0f, 0.0f, 0.0f, 0.8f);
		before->push_constants(&color_depth, 0, sizeof(color_depth));
		CommandBufferUtil::draw_fullscreen_quad_depth(*before, "builtin://shaders/quad.vert", "assets://shaders/fill_depth.frag",
		                                              true, true, VK_COMPARE_OP_LESS);

		clear_rect.rect.offset = { 500, 500 };
		clear_rect.rect.extent = { 200, 200 };
		clear_value.color.float32[0] = 0.0f;
		clear_value.color.float32[1] = 1.0f;
		clear_value.color.float32[2] = 0.0f;
		clear_value.color.float32[3] = 1.0f;
		after->clear_quad(0, clear_rect, clear_value, VK_IMAGE_ASPECT_COLOR_BIT);
		clear_value.depthStencil.depth = 0.0f;
		after->clear_quad(1, clear_rect, clear_value, VK_IMAGE_ASPECT_DEPTH_BIT);

		color_depth = vec4(0.0f, 0.0f, 1.0f, 0.75f);
		after->push_constants(&color_depth, 0, sizeof(color_depth));
		CommandBufferUtil::draw_fullscreen_quad_depth(*after, "builtin://shaders/quad.vert", "assets://shaders/fill_depth.frag",
		                                              true, true, VK_COMPARE_OP_LESS);

		auto cmd = device.request_command_buffer();

		incremental_rp.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT | RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT | RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT;
		incremental_rp.clear_attachments = 1;
		incremental_rp.load_attachments = 0;
		incremental_rp.store_attachments = 1;
		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		cmd->begin_render_pass(incremental_rp, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		cmd->submit_secondary(before);
		cmd->end_render_pass();

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		incremental_rp.op_flags = RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT | RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT;
		incremental_rp.clear_attachments = 0;
		incremental_rp.load_attachments = 1;
		incremental_rp.store_attachments = 1;
		cmd->begin_render_pass(incremental_rp, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		cmd->submit_secondary(after);
		cmd->end_render_pass();

		cmd->image_barrier(*render_target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::Depth);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);

		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_A2B10G10R10_SNORM_PACK32, 0);
		cmd->set_specialization_constant_mask(0xf);
		cmd->set_specialization_constant(0, 0.2f);
		cmd->set_specialization_constant(1, 0.3f);
		cmd->set_specialization_constant(2, 0.8f);
		cmd->set_specialization_constant(3, 1.0f);

		auto *program = device.get_shader_manager().register_graphics("assets://shaders/triangle.vert", "assets://shaders/triangle.frag");
		auto variant = program->register_variant({});
		cmd->set_program(*program->get_program(variant));
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);

		static const vec2 vertices[] = {
			vec2(-1.0f, -1.0f),
			vec2(-1.0f, +1.0f),
			vec2(+1.0f, -1.0f),
			vec2(+1.0f, +1.0f),
		};
		auto *verts = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vertices), sizeof(vec2)));
		memcpy(verts, vertices, sizeof(vertices));

		auto *attribs = static_cast<uint32_t *>(cmd->allocate_vertex_data(1, 4 * sizeof(uint32_t), sizeof(uint32_t)));
		attribs[0] = 511u <<  0;
		attribs[1] = 511u << 10;
		attribs[2] = 511u << 20;
		attribs[3] = 1u << 30;

		attribs[0] |= 512u << 10;
		attribs[0] |= 512u << 20;
		attribs[0] |= 2u << 30;
		attribs[1] |= 512u <<  0;
		attribs[1] |= 512u << 20;
		attribs[1] |= 2u << 30;
		attribs[2] |= 512u <<  0;
		attribs[2] |= 512u << 10;
		attribs[2] |= 2u << 30;
		attribs[3] |= 512u <<  0;
		attribs[3] |= 512u << 10;
		attribs[3] |= 512u << 20;

		cmd->draw(4);

		cmd->set_texture(0, 0, render_target->get_view(), StockSampler::LinearClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");

		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle render_target;
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
		auto *app = new TriangleApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
