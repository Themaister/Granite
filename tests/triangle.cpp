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
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct TriangleApplication : Granite::Application, Granite::EventHandler
{
	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		constexpr unsigned WIDTH = 64;
		constexpr unsigned HEIGHT = 64;
		constexpr unsigned STRETCH_A = 1;
		constexpr unsigned STRETCH_B = 2;
		constexpr float STRETCH_A_F = float(STRETCH_A);
		constexpr float STRETCH_B_F = float(STRETCH_B);

		auto rt_info = ImageCreateInfo::render_target(WIDTH, HEIGHT, VK_FORMAT_R8G8B8A8_UNORM);
		rt_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rt_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		auto rt = device.create_image(rt_info);

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.color_attachments[0] = &rt->get_view();

		cmd->image_barrier(*rt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		cmd->begin_render_pass(rp);
		cmd->set_program("assets://shaders/triangle.vert", "assets://shaders/triangle.frag");
		cmd->set_opaque_state();
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		cmd->set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
		cmd->set_cull_mode(VK_CULL_MODE_BACK_BIT);

		float snap = 1.0f / float(1u << device.get_gpu_properties().limits.subPixelPrecisionBits);

		vec2 vertices[3];

		// Window coordinates.
		vertices[0] = vec2(1.5f - 2.0f * snap, 1.5f + 3.0f * snap);
		vertices[1] = vec2(1.5f + STRETCH_A_F * snap, 1.5f - STRETCH_A_F * snap);
		vertices[2] = vec2(1.5f - STRETCH_B_F * snap, 1.5f + STRETCH_B_F * snap);

		const char *env = getenv("BIAS");
		if (env)
		{
			LOGI("Applying bias.\n");
			vertices[2] += vec2(0.49f * snap);
		}
		else
			LOGI("Not applying bias.\n");

		// Convert to clip coordinates.
		for (auto &v : vertices)
			v = 2.0f * (v / vec2(WIDTH, HEIGHT)) - 1.0f;

		static const vec4 colors[] = {
			vec4(1.0f, 0.0f, 0.0f, 1.0f),
			vec4(0.0f, 1.0f, 0.0f, 1.0f),
			vec4(0.0f, 0.0f, 1.0f, 1.0f),
		};

		auto *verts = static_cast<vec2 *>(cmd->allocate_vertex_data(0, sizeof(vertices), sizeof(vec2)));
		auto *col = static_cast<vec4 *>(cmd->allocate_vertex_data(1, sizeof(colors), sizeof(vec4)));
		memcpy(verts, vertices, sizeof(vertices));
		memcpy(col, colors, sizeof(colors));
		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
		cmd->draw(3);
		cmd->end_render_pass();

		cmd->image_barrier(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_texture(0, 0, rt->get_view(), StockSampler::NearestClamp);
		CommandBufferUtil::draw_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();
		device.submit(cmd);
	}
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

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
