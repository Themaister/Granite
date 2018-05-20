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
#include "os.hpp"
#include <string.h>

using namespace Granite;
using namespace Vulkan;

struct TriangleApplication : Granite::Application
{
	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
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

	Filesystem::get().register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
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
