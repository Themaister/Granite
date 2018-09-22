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
#include "muglm/matrix_helper.hpp"
#include "audio_events.hpp"
#include "vorbis_stream.hpp"
#include <string.h>

using namespace Granite;
using namespace Granite::Audio;
using namespace Vulkan;

struct AudioApplication : Application, EventHandler
{
	AudioApplication()
	{
		EVENT_MANAGER_REGISTER(AudioApplication, on_key_pressed, KeyboardEvent);
		EVENT_MANAGER_REGISTER(AudioApplication, on_touch_down, TouchDownEvent);
		EVENT_MANAGER_REGISTER_LATCH(AudioApplication, on_mixer_start, on_mixer_stop, MixerStartEvent);
	}

	Mixer *mixer = nullptr;
	void on_mixer_start(const MixerStartEvent &e)
	{
		mixer = &e.get_mixer();
	}

	void on_mixer_stop(const MixerStartEvent &)
	{
		mixer = nullptr;
	}

	bool on_touch_down(const TouchDownEvent &e)
	{
		if (!mixer)
			return false;

		if (e.get_x() < 0.5f)
			mixer->add_mixer_stream(create_vorbis_stream("assets://audio/a.ogg"));
		else
			mixer->add_mixer_stream(create_vorbis_stream("assets://audio/b.ogg"));

		return true;
	}

	bool on_key_pressed(const KeyboardEvent &e)
	{
		if (!mixer)
			return true;
		if (e.get_key_state() != KeyState::Pressed)
			return true;

		switch (e.get_key())
		{
		case Key::A:
			mixer->add_mixer_stream(create_vorbis_stream("assets://audio/a.ogg"));
			break;

		case Key::B:
			mixer->add_mixer_stream(create_vorbis_stream("assets://audio/b.ogg"));
			break;

		default:
			break;
		}
		return true;
	}

	void render_frame(double, double)
	{
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.1f;
		rp.clear_color[0].float32[1] = 0.2f;
		rp.clear_color[0].float32[2] = 0.3f;
		rp.clear_color[0].float32[3] = 0.4f;
		cmd->begin_render_pass(rp);
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

	Global::filesystem()->register_protocol("assets", std::unique_ptr<FilesystemBackend>(new OSFilesystem(asset_dir)));
#endif

	try
	{
		auto *app = new AudioApplication();
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
}