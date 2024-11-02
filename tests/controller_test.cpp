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

struct ControllerApplication : Granite::Application, Granite::EventHandler
{
	ControllerApplication()
	{
		EVENT_MANAGER_REGISTER(ControllerApplication, on_joypad, JoypadStateEvent);
	}

	vec2 axis_left = vec2(0.0f);
	vec2 axis_right = vec2(0.0f);

	bool on_joypad(const JoypadStateEvent &e)
	{
		auto &state = e.get_state(0);
		axis_left = vec2(state.get_axis(JoypadAxis::LeftX), state.get_axis(JoypadAxis::LeftY));
		axis_right = vec2(state.get_axis(JoypadAxis::RightX), state.get_axis(JoypadAxis::RightY));
		return true;
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		cmd->begin_render_pass(device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly));

		VkClearRect rect = {};
		rect.layerCount = 1;
		rect.rect = { { 100, 100 }, { 400, 400 } };
		VkClearValue gray = {};
		VkClearValue green = {};
		gray.color.float32[0] = 0.1f;
		gray.color.float32[1] = 0.1f;
		gray.color.float32[2] = 0.1f;
		green.color.float32[0] = 0.0f;
		green.color.float32[1] = 1.0f;
		green.color.float32[2] = 0.0f;
		cmd->clear_quad(0, rect, gray);

		rect.rect.offset.x = 600;
		cmd->clear_quad(0, rect, gray);

		rect.rect.extent = { 16, 16 };
		vec2 left_axis_pos = vec2(300.0f - 8.0f) + vec2(200.0f) * axis_left;
		vec2 right_axis_pos = vec2(800.0f - 8.0f, 300.0f - 8.0f) + vec2(200.0f) * axis_right;
		rect.rect.offset = { int(left_axis_pos.x), int(left_axis_pos.y) };
		cmd->clear_quad(0, rect, green);
		rect.rect.offset = { int(right_axis_pos.x), int(right_axis_pos.y) };
		cmd->clear_quad(0, rect, green);

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
		auto *app = new ControllerApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}
