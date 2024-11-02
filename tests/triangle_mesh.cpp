/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

struct TriangleMeshApplication : Granite::Application, Granite::EventHandler
{
	BufferHandle create_readonly_ssbo(const void *data, size_t size)
	{
		BufferCreateInfo info = {};
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		info.domain = BufferDomain::LinkedDeviceHost;
		info.size = size;
		return get_wsi().get_device().create_buffer(info, data);
	}

	void render_frame(double, double elapsed_time)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();
		auto &features = device.get_device_features();

		if ((features.vk11_props.subgroupSupportedStages & VK_SHADER_STAGE_MESH_BIT_EXT) == 0)
		{
			LOGE("Subgroups not supported in mesh.\n");
			return;
		}

		if ((features.vk11_props.subgroupSupportedStages & VK_SHADER_STAGE_TASK_BIT_EXT) == 0)
		{
			LOGE("Subgroups not supported in task.\n");
			return;
		}

		if (!device.supports_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_TASK_BIT_EXT))
		{
			LOGE("Wave32 not supported in task shader.\n");
			return;
		}

		if (!device.supports_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT))
		{
			LOGE("Wave32 not supported in mesh shader.\n");
			return;
		}

		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));
		cmd->set_program("assets://shaders/triangle.task",
		                 "assets://shaders/triangle.mesh",
		                 "assets://shaders/triangle_mesh.frag");
		cmd->set_opaque_state();

		cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_TASK_BIT_EXT);
		cmd->enable_subgroup_size_control(true, VK_SHADER_STAGE_MESH_BIT_EXT);
		cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_TASK_BIT_EXT);
		cmd->set_subgroup_size_log2(true, 5, 5, VK_SHADER_STAGE_MESH_BIT_EXT);

		vec2 vertices[] = {
			vec2(-0.2f, -0.2f),
			vec2(-0.2f, +0.2f),
			vec2(+0.2f, -0.2f),
		};

		auto c = float(muglm::cos(elapsed_time * 2.0));
		auto s = float(muglm::sin(elapsed_time * 2.0));
		mat2 m{vec2(c, -s), vec2(s, c)};
		for (auto &v : vertices)
			v = m * v;

		static const vec4 colors[] = {
			vec4(1.0f, 0.0f, 0.0f, 1.0f),
			vec4(0.0f, 1.0f, 0.0f, 1.0f),
			vec4(0.0f, 0.0f, 1.0f, 1.0f),
			vec4(1.0f, 1.0f, 1.0f, 1.0f),
		};

		static const vec2 offsets[] = {
			vec2(-0.5f, -0.5f),
			vec2(+0.5f, -0.5f),
			vec2(-0.5f, +0.5f),
			vec2(+0.5f, +0.5f),
		};

		auto pos = create_readonly_ssbo(vertices, sizeof(vertices));
		cmd->set_storage_buffer(0, 0, *pos);
		auto offbuf = create_readonly_ssbo(offsets, sizeof(offsets));
		cmd->set_storage_buffer(0, 1, *offbuf);
		auto colbuf = create_readonly_ssbo(colors, sizeof(colors));
		cmd->set_storage_buffer(0, 2, *colbuf);

		cmd->draw_mesh_tasks(1, 1, 1);
		cmd->end_render_pass();
		device.submit(cmd);
	}

	ImageHandle render_target;
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new TriangleMeshApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
