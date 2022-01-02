/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "thread_group.hpp"
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

	Context::SystemHandles system_handles;
	system_handles.filesystem = GRANITE_FILESYSTEM();
	system_handles.thread_group = GRANITE_THREAD_GROUP();
	system_handles.timeline_trace_file = system_handles.thread_group->get_timeline_trace_file();

	if (!platform->has_external_swapchain() &&
	    !application_wsi.init(system_handles.thread_group->get_num_threads() + 1, system_handles))
	{
		return false;
	}

	return true;
}

bool Application::poll()
{
	auto &wsi = get_wsi();
	if (!get_platform().alive(wsi))
		return false;

	if (requested_shutdown)
		return false;

	auto *fs = GRANITE_FILESYSTEM();
	auto *em = GRANITE_EVENT_MANAGER();
	if (fs)
		fs->poll_notifications();
	if (em)
		em->dispatch();

#ifdef HAVE_GRANITE_AUDIO
	auto *backend = GRANITE_AUDIO_BACKEND();
	if (backend)
		backend->heartbeat();
	auto *am = GRANITE_AUDIO_MIXER();
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
}
