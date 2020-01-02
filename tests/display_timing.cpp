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
#include "muglm/muglm_impl.hpp"

using namespace Granite;
using namespace Vulkan;

struct DisplayTimingApplication : Granite::Application, Granite::EventHandler
{
	DisplayTimingApplication()
	{
		EVENT_MANAGER_REGISTER(DisplayTimingApplication, on_key_down, KeyboardEvent);
		EVENT_MANAGER_REGISTER(DisplayTimingApplication, on_touch_down, TouchDownEvent);
		EVENT_MANAGER_REGISTER(DisplayTimingApplication, on_stutter, DisplayTimingStutterEvent);
	}

	bool on_stutter(const DisplayTimingStutterEvent &stutter)
	{
		red = 0.8f;
		LOGE("Observed %u dropped frames!\n", stutter.get_dropped_frames());
		return true;
	}

	bool on_touch_down(const TouchDownEvent &)
	{
		color_flip = !color_flip;
		return true;
	}

	bool on_key_down(const KeyboardEvent &e)
	{
		if (e.get_key_state() == KeyState::Pressed && e.get_key() == Key::Space)
		{
			color_flip = !color_flip;
		}
		return true;
	}

	void render_frame(double frame_time, double) override
	{
		get_wsi().get_timing().set_debug_enable(true);
		get_wsi().get_timing().set_swap_interval(1);
		//get_wsi().get_timing().set_latency_limiter(LatencyLimiter::AdaptiveLowLatency);

		auto &device = get_wsi().get_device();
		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = red;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		red *= 0.95f;
		cmd->begin_render_pass(rp);

		struct Push
		{
			vec4 color;
			float phase;
		} push;

		push.color = color_flip ? vec4(1.0f, 0.0f, 1.0f, 1.0f) : vec4(0.0f, 1.0f, 0.0f, 1.0f);

		push.phase = float(muglm::fract(total_time / 3.0)) * 0.8f + 0.1f;
		push.phase = 2.0f * push.phase - 1.0f;
		cmd->push_constants(&push, 0, sizeof(push));

		cmd->set_transparent_sprite_state();
		cmd->set_program("assets://shaders/test_quad.vert", "assets://shaders/test_quad.frag");
		CommandBufferUtil::set_quad_vertex_state(*cmd);
		CommandBufferUtil::draw_quad(*cmd);

		cmd->end_render_pass();

		device.submit(cmd);

		LOGI("Reported frame time: %.3f ms\n", frame_time * 1e3);
		total_time += frame_time;
	}

	float red = 0.0f;
	double total_time = 0.0;
	bool color_flip = false;
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
