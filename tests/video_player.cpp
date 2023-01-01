/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "ffmpeg_decode.hpp"
#include "application.hpp"
#include "application_wsi_events.hpp"
#ifdef HAVE_GRANITE_AUDIO
#include "audio_mixer.hpp"
#endif

struct VideoPlayerApplication : Granite::Application, Granite::EventHandler
{
	explicit VideoPlayerApplication(const char *path)
	{
		Granite::VideoDecoder::DecodeOptions opts;
		opts.mipgen = true;
#ifdef HAVE_GRANITE_AUDIO
		if (!decoder.init(GRANITE_AUDIO_MIXER(), path, opts))
#else
		if (!decoder.init(nullptr, path, opts))
#endif
		{
			throw std::runtime_error("Failed to open file");
		}

		EVENT_MANAGER_REGISTER_LATCH(VideoPlayerApplication, on_module_created, on_module_destroyed, Vulkan::DeviceShaderModuleReadyEvent);
		EVENT_MANAGER_REGISTER(VideoPlayerApplication, on_key_pressed, Granite::KeyboardEvent);
	}

	bool on_key_pressed(const Granite::KeyboardEvent &e)
	{
		if (e.get_key_state() == Granite::KeyState::Pressed)
		{
			double seek_offset = 0.0;
			bool drop_frame = false;

			if (e.get_key() == Granite::Key::R)
			{
				if (!decoder.seek(0.0))
					LOGE("Failed to rewind.\n");
				else
					drop_frame = true;
			}
			else if (e.get_key() == Granite::Key::Space)
				decoder.set_paused(!decoder.get_paused());
			else if (e.get_key() == Granite::Key::Left)
				seek_offset = -10.0;
			else if (e.get_key() == Granite::Key::Right)
				seek_offset = +10.0;
			else if (e.get_key() == Granite::Key::Up)
				seek_offset = +60.0;
			else if (e.get_key() == Granite::Key::Down)
				seek_offset = -60.0;

			if (seek_offset != 0.0)
			{
				auto ts = decoder.get_estimated_audio_playback_timestamp_raw();
				if (ts >= 0.0)
				{
					if (decoder.seek(ts + seek_offset))
						drop_frame = true;
					else
						LOGE("Failed to seek.\n");
				}
			}

			if (drop_frame)
			{
				frame = {};
				next_frame = {};
			}
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
		frame = {};
		next_frame = {};
		decoder.stop();
		decoder.end_device_context();
	}

	void shift_frame()
	{
		if (frame.view)
		{
			// If we never actually read the image and discarded it,
			// we just forward the acquire semaphore directly to release.
			// This resolves any write-after-write hazard for the image.
			VK_ASSERT(frame.sem);
			decoder.release_video_frame(frame.index, std::move(frame.sem));
		}

		frame = std::move(next_frame);
		next_frame = {};
		need_acquire = true;
	}

	void render_frame(double, double elapsed_time) override
	{
		auto &device = get_wsi().get_device();

		// Based on the audio PTS, we want to display a video frame that is slightly larger.
		double target_pts = decoder.get_estimated_audio_playback_timestamp(elapsed_time);
		if (target_pts < 0.0)
			target_pts = elapsed_time;

		// Update the latest frame. We want the closest PTS to target_pts.
		if (!next_frame.view)
			if (decoder.try_acquire_video_frame(next_frame) < 0 && target_pts > frame.pts)
				request_shutdown();

		while (next_frame.view)
		{
			// If we have two candidates, shift out frame if next_frame PTS is closer.
			double d_current = std::abs(frame.pts - target_pts);
			double d_next = std::abs(next_frame.pts - target_pts);
			if (d_next < d_current || !frame.view)
			{
				shift_frame();

				// Try to catch up quickly by skipping frames if we have to.
				// Defer any EOF handling to next frame.
				decoder.try_acquire_video_frame(next_frame);
			}
			else
				break;
		}

		if (need_acquire)
		{
			// When we have committed to display this video frame,
			// inject the wait semaphore.
			device.add_wait_semaphore(
					Vulkan::CommandBuffer::Type::Generic, std::move(frame.sem),
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, true);
			frame.sem = {};
			need_acquire = false;
		}

		// Blit on screen.
		auto cmd = device.request_command_buffer();
		auto rp = device.get_swapchain_render_pass(Vulkan::SwapchainRenderPass::ColorOnly);
		cmd->begin_render_pass(rp);
		if (frame.view)
		{
			cmd->set_texture(0, 0, *frame.view, Vulkan::StockSampler::LinearClamp);
			Vulkan::CommandBufferUtil::draw_fullscreen_quad(
					*cmd, "builtin://shaders/quad.vert", "builtin://shaders/blit.frag");
		}
		cmd->end_render_pass();

		frame.sem.reset();
		device.submit(cmd, nullptr, 1, &frame.sem);
	}

	Granite::VideoDecoder decoder;
	Granite::VideoFrame frame, next_frame;
	bool need_acquire = false;
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

