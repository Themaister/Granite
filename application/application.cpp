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
#include "sprite.hpp"
#include "horizontal_packing.hpp"
#include "image_widget.hpp"
#include "label.hpp"
#include "quirks.hpp"
#include "post/hdr.hpp"
#include "rapidjson_wrapper.hpp"
#include "light_export.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include "utils/image_utils.hpp"
#include "thread_group.hpp"
#include "thread_id.hpp"
#include <chrono>
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

using namespace std;
using namespace Vulkan;

namespace Granite
{
Application::Application()
{
}

bool Application::init_wsi(std::unique_ptr<WSIPlatform> new_platform)
{
	platform = move(new_platform);
	application_wsi.set_platform(platform.get());
	if (!platform->has_external_swapchain() && !application_wsi.init(Global::thread_group()->get_num_threads() + 1))
		return false;

	return true;
}

bool Application::poll()
{
	auto &wsi = get_wsi();
	if (!get_platform().alive(wsi))
		return false;

	if (requested_shutdown)
		return false;

	auto *fs = Global::filesystem();
	auto *em = Global::event_manager();
	if (fs)
		fs->poll_notifications();
	if (em)
		em->dispatch();

#ifdef HAVE_GRANITE_AUDIO
	auto *backend = Global::audio_backend();
	if (backend)
		backend->heartbeat();
	auto *am = Global::audio_mixer();
	if (am)
	{
		// Pump through events from audio thread.
		auto &queue = am->get_message_queue();
		Util::MessageQueuePayload payload;
		while ((payload = queue.read_message()))
		{
			auto &event = payload.as<Event>();
			if (em)
				em->dispatch_inline(event);
			queue.recycle_payload(std::move(payload));
		}

		// Recycle dead streams.
		am->dispose_dead_streams();
	}
#endif

	return true;
}

void Application::run_frame()
{
	application_wsi.begin_frame();
	render_frame(application_wsi.get_smooth_frame_time(), application_wsi.get_smooth_elapsed_time());
	application_wsi.end_frame();
}

void ApplicationCLIWrapper::render_frame(double, double)
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

	if (!task.valid())
		return;

	auto &device = get_wsi().get_device();
	auto result = task.wait_for(std::chrono::milliseconds(100));
	if (result != std::future_status::ready)
	{
		int ret = task.get();
		LOGI("======================\n");
		LOGI("Executable returned %d.\n", ret);
		LOGI("======================\n");
		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 1.0f;
		rp.clear_color[0].float32[1] = 1.0f;
		rp.clear_color[0].float32[2] = 1.0f;

		cmd->begin_render_pass(rp);
		cmd->end_render_pass();
		device.submit(cmd);

		request_shutdown();
	}
}

ApplicationCLIWrapper::ApplicationCLIWrapper(int (*func_)(int, char **), int argc_, char **argv_)
	: func(func_), argc(argc_), argv(argv_)
{
}

}
