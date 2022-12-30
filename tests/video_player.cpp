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

#include "ffmpeg.hpp"
#include "application.hpp"
#include "application_wsi_events.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

struct VideoPlayerApplication : Granite::Application, Granite::EventHandler
{
	explicit VideoPlayerApplication(const char *path)
	{
#ifdef HAVE_GRANITE_AUDIO
		if (!decoder.init(GRANITE_AUDIO_MIXER(), path))
#else
		if (!decoder.init(nullptr, path))
#endif
		{
			throw std::runtime_error("Failed to open file");
		}

		EVENT_MANAGER_REGISTER_LATCH(VideoPlayerApplication, on_module_created, on_module_destroyed, Vulkan::DeviceShaderModuleReadyEvent);
		EVENT_MANAGER_REGISTER(VideoPlayerApplication, on_key_pressed, Granite::KeyboardEvent);
	}

	bool on_key_pressed(const Granite::KeyboardEvent &e)
	{
		if (e.get_key_state() == Granite::KeyState::Pressed && e.get_key() == Granite::Key::R)
		{
			frame = {};
			if (!decoder.rewind())
				LOGE("Failed to rewind.\n");
		}

		return true;
	}

	void on_module_created(const Vulkan::DeviceShaderModuleReadyEvent &e)
	{
		if (!decoder.begin_device_context(&e.get_device()))
			LOGE("Failed to begin device context.\n");
		if (!decoder.play())
			LOGE("Failed to begin playback.\n");
	}

	void on_module_destroyed(const Vulkan::DeviceShaderModuleReadyEvent &)
	{
		decoder.stop();
		decoder.end_device_context();
		sem.reset();
	}

	void render_frame(double, double elapsed_time)
	{
		auto &device = get_wsi().get_device();

		double target_pts = decoder.get_estimated_audio_playback_timestamp();
		if (target_pts < 0.0)
			target_pts = elapsed_time;

		if (frame.view && target_pts > frame.pts)
		{
			decoder.release_video_frame(frame.index, std::move(sem));
			sem = {};
			frame = {};
		}

		if (!frame.view)
		{
			if (!decoder.acquire_video_frame(frame))
			{
				request_shutdown();
				return;
			}

			device.add_wait_semaphore(
					Vulkan::CommandBuffer::Type::Generic, std::move(frame.sem),
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
		}

		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		cmd->set_texture(0, 0, *frame.view, Vulkan::StockSampler::LinearClamp);
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(
				*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		cmd->end_render_pass();

		if (sem)
		{
			device.add_wait_semaphore(Vulkan::CommandBuffer::Type::Generic, std::move(sem),
			                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
			sem = {};
		}

		device.submit(cmd, nullptr, 1, &sem);
	}

	Granite::VideoDecoder decoder;
	Granite::VideoFrame frame;
	Vulkan::Semaphore sem;
};

namespace Granite
{
Application *application_create(int argc, char **argv)
{
	GRANITE_APPLICATION_SETUP_FILESYSTEM();

	if (argc != 2)
		return nullptr;

	try
	{
		auto *app = new VideoPlayerApplication(argv[1]);
		return app;
	}
	catch (const std::exception &e)
	{
		LOGE("application_create() threw exception: %s\n", e.what());
		return nullptr;
	}
}
} // namespace Granite

