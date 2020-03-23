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

struct MDIApplication : Granite::Application
{
	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		BufferHandle count_buffer;
		{
			BufferCreateInfo info = {};
			info.size = sizeof(uint32_t);
			info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			info.domain = BufferDomain::Device;

			uint32_t count = 16;
			count_buffer = device.create_buffer(info, &count);
		}

		BufferHandle indirect_buffer;
		{
			BufferCreateInfo info = {};
			info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			info.domain = BufferDomain::Device;

			VkDrawIndexedIndirectCommand commands[16] = {};
			info.size = sizeof(commands);

			for (auto &command : commands)
			{
				command.indexCount = 6;
				command.instanceCount = 1;
				command.vertexOffset = 4 * unsigned(&command - commands);
			}

			indirect_buffer = device.create_buffer(info, commands);
		}

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_opaque_state();
		cmd->set_program("assets://shaders/multi_draw_indirect.vert", "assets://shaders/multi_draw_indirect.frag");
		cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		auto *indices = static_cast<uint16_t *>(cmd->allocate_index_data(6 * sizeof(uint16_t), VK_INDEX_TYPE_UINT16));
		indices[0] = 0;
		indices[1] = 1;
		indices[2] = 2;
		indices[3] = 3;
		indices[4] = 2;
		indices[5] = 1;

		cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
		cmd->set_vertex_attrib(1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0);

		auto *positions = static_cast<vec4 *>(cmd->allocate_vertex_data(0, 16 * 4 * sizeof(vec4), sizeof(vec4)));
		auto *colors = static_cast<vec4 *>(cmd->allocate_vertex_data(1, 16 * 4 * sizeof(vec4), sizeof(vec4)));

		for (unsigned y = 0; y < 4; y++)
		{
			for (unsigned x = 0; x < 4; x++)
			{
				auto *p = positions + 4 * (4 * y + x);
				auto *c = colors + 4 * (4 * y + x);

				vec2 base_pos = vec2((x - 1.5f) * 0.5f, (y - 1.5f) * 0.5f);
				vec4 base_color = (4 * y + x < 10) ? vec4(0.0f, 1.0f, 0.0f, 0.0f) : vec4(1.0f, 0.0f, 0.0f, 1.0f);

				p[0] = vec4(base_pos + vec2(-0.2f, -0.2f), 0.0f, 1.0f);
				p[1] = vec4(base_pos + vec2(-0.2f, +0.2f), 0.0f, 1.0f);
				p[2] = vec4(base_pos + vec2(+0.2f, -0.2f), 0.0f, 1.0f);
				p[3] = vec4(base_pos + vec2(+0.2f, +0.2f), 0.0f, 1.0f);

				c[0] = base_color;
				c[1] = base_color;
				c[2] = base_color;
				c[3] = base_color;
			}
		}

		cmd->draw_indexed_multi_indirect(*indirect_buffer, 0, 10, sizeof(VkDrawIndexedIndirectCommand), *count_buffer, 0);

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
		auto *app = new MDIApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}