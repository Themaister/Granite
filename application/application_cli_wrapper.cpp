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

#include "application_cli_wrapper.hpp"
#include "global_managers.hpp"
#include "message_queue.hpp"
#include "thread_id.hpp"
#include "device.hpp"
#include "ui_manager.hpp"
#include <algorithm>

namespace Granite
{
void ApplicationCLIWrapper::render_frame(double, double elapsed_time)
{
	if (!started)
	{
		LOGI("Begin main function ...\n");
		auto ctx = Global::create_thread_context();
		task = std::async(std::launch::async, [=, c = std::move(ctx)]() -> int {
			Global::set_thread_context(*c);
			Vulkan::register_thread_index(0);
			return func(argc, argv);
		});
		started = true;
	}

	auto &device = get_wsi().get_device();

	if (task.valid())
	{
		auto result = task.wait_for(std::chrono::milliseconds(10));
		if (result == std::future_status::ready)
		{
			int ret = task.get();
			task = {};
			LOGI("======================\n");
			LOGI("Executable returned %d.\n", ret);
			LOGI("======================\n");
		}
	}

	bool got_message = false;
	auto *queue = Global::message_queue();
	while (queue->available_read_messages())
	{
		auto message = queue->read_message();
		messages.emplace_back(static_cast<const char *>(message.get_payload_data()));
		queue->recycle_payload(std::move(message));
		got_message = true;
	}

	if (got_message || !task.valid())
	{
		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.1 * std::sin(elapsed_time * 0.5) + 0.1;
		rp.clear_color[0].float32[1] = 0.1 * std::sin(elapsed_time * 0.6) + 0.1;
		rp.clear_color[0].float32[2] = 0.1 * std::sin(elapsed_time * 0.7) + 0.1;
		cmd->begin_render_pass(rp);
		renderer.begin();

		float width = cmd->get_viewport().width;
		float height = cmd->get_viewport().height;
		float accum_y = 20.0f;

		auto &font = Global::ui_manager()->get_font(UI::FontSize::Normal);
		for (auto &msg : messages)
		{
			auto geom = font.get_text_geometry(msg.c_str());
			renderer.render_text(font, msg.c_str(), vec3(20.0f, accum_y, 0.0f), geom);
			accum_y += geom.y + 3.0f;
		}

		accum_y += 20.0f;

		if (messages.size() > 50)
			messages.erase(messages.begin(), messages.end() - 50);

		renderer.flush(*cmd,
		               vec3(0.0f, std::max(0.0f, accum_y - height), 0.0f),
		               vec3(width, height, 1.0f));

		cmd->end_render_pass();
		device.submit(cmd);
	}
}

ApplicationCLIWrapper::ApplicationCLIWrapper(int (*func_)(int, char **), int argc_, char **argv_)
    : func(func_)
    , argc(argc_)
    , argv(argv_)
{
	Global::message_queue()->uncork();
}
}