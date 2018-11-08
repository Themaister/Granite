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
#include "math.hpp"
#include "muglm/muglm_impl.hpp"

using namespace Granite;
using namespace Vulkan;

struct DisplayTimingApplication : Granite::Application
{
	void render_frame(double frame_time, double) override
	{
		get_wsi().get_timing().set_debug_enable(true);
		get_wsi().get_timing().set_swap_interval(1);
		//get_wsi().get_timing().set_latency_limiter(LatencyLimiter::AdaptiveLowLatency);

		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		cmd->begin_render_pass(rp);

		float phase = float(muglm::fract(total_time / 3.0)) * 0.8f + 0.1f;
		phase = 2.0f * phase - 1.0f;
		cmd->push_constants(&phase, 0, sizeof(phase));

		cmd->set_transparent_sprite_state();
		cmd->set_program("assets://shaders/test_quad.vert", "assets://shaders/test_quad.frag");
		CommandBufferUtil::set_quad_vertex_state(*cmd);
		CommandBufferUtil::draw_quad(*cmd);

		cmd->end_render_pass();

		device.submit(cmd);

		LOGI("Reported frame time: %.3f ms\n", frame_time * 1e3);
		total_time += frame_time;
	}

	double total_time = 0.0;
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
		auto *app = new DisplayTimingApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
