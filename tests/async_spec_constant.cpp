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
#include "math.hpp"
#include "thread_group.hpp"

using namespace Granite;
using namespace Vulkan;
using namespace Util;

struct AsyncSpecConstantApplication : Granite::Application, Granite::EventHandler
{
	std::unordered_set<Hash> pending_pipelines;

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();
		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));

		CommandBufferUtil::setup_fullscreen_quad(*cmd, "builtin://shaders/quad.vert", "assets://shaders/fill_color_spec_constant.frag");

		uint32_t value = frame_count >> 8;
		cmd->set_specialization_constant_mask(3 << 1);
		cmd->set_specialization_constant(1, 1);
		cmd->set_specialization_constant(2, value);
		cmd->push_constants(&value, 0, sizeof(value));

		// If we have the specialized shader already, go ahead, otherwise, queue up a compile to specialize the pipeline.
		if (!cmd->flush_pipeline_state_without_blocking())
		{
			DeferredPipelineCompile compile;
			cmd->extract_pipeline_state(compile);

			if (pending_pipelines.count(compile.hash) == 0)
			{
				LOGI("Enqueueing pipeline compile for spec constant %u.\n", value);
				GRANITE_THREAD_GROUP()->create_task([&device, c = std::make_unique<DeferredPipelineCompile>(compile)]() {
					CommandBuffer::build_graphics_pipeline(&device, *c, CommandBuffer::CompileMode::AsyncThread);
				});
				pending_pipelines.insert(compile.hash);
			}

			cmd->set_specialization_constant_mask(0);
			LOGI("Pipeline is currently not compiled, so falling back to ubershader for spec constant %u.\n", value);
		}
		CommandBufferUtil::draw_fullscreen_quad(*cmd);

		cmd->end_render_pass();
		device.submit(cmd);
		frame_count++;
	}

	ImageHandle render_target;
	unsigned frame_count = 0;
};

namespace Granite
{
Application *application_create(int, char **)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	try
	{
		auto *app = new AsyncSpecConstantApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}