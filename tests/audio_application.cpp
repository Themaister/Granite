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
#include "os_filesystem.hpp"
#include "muglm/matrix_helper.hpp"
#include "audio_events.hpp"
#include "vorbis_stream.hpp"
#include "dsp/tone_filter_stream.hpp"
#include "dsp/tone_filter.hpp"
#include "muglm/muglm_impl.hpp"
#include <string.h>
#include <dsp/dsp.hpp>

using namespace Granite;
using namespace Granite::Audio;
using namespace Vulkan;

struct AudioApplication : Application, EventHandler
{
	AudioApplication()
	{
		EVENT_MANAGER_REGISTER(AudioApplication, on_key_pressed, KeyboardEvent);
		EVENT_MANAGER_REGISTER(AudioApplication, on_touch_down, TouchDownEvent);
		EVENT_MANAGER_REGISTER(AudioApplication, on_stream_event, StreamStoppedEvent);
		EVENT_MANAGER_REGISTER(AudioApplication, on_audio_samples, AudioMonitorSamplesEvent);
		EVENT_MANAGER_REGISTER(AudioApplication, on_tone_samples, DSP::ToneFilterWave);
		EVENT_MANAGER_REGISTER(AudioApplication, on_audio_perf, AudioStreamPerformanceEvent);
		EVENT_MANAGER_REGISTER_LATCH(AudioApplication, on_mixer_start, on_mixer_stop, MixerStartEvent);
	}

	enum { RingSize = 512 };
	float ring[RingSize] = {};
	float power_ratio[DSP::ToneFilter::ToneCount / 12][12] = {};
	unsigned offset = 0;
	float tone_ring[DSP::ToneFilter::ToneCount / 12][12][RingSize] = {};
	unsigned tone_offset[DSP::ToneFilter::ToneCount / 12][12] = {};

	double total_time = 0.0;
	uint64_t total_samples = 0;

	bool on_audio_perf(const AudioStreamPerformanceEvent &e)
	{
		total_time += e.get_time();
		total_samples += e.get_sample_count();
		return true;
	}

	bool on_stream_event(const StreamStoppedEvent &e)
	{
		LOGI("Stream %u stopped.\n", e.get_index());
		return true;
	}

	bool on_tone_samples(const DSP::ToneFilterWave &e)
	{
		unsigned count = e.get_sample_count();
		const float *data = e.get_payload();

		power_ratio[e.get_tone_index() / 12][e.get_tone_index() % 12] = e.get_power_ratio();

		auto &r = tone_ring[e.get_tone_index() / 12][e.get_tone_index() % 12];
		auto &off = tone_offset[e.get_tone_index() / 12][e.get_tone_index() % 12];
		for (unsigned i = 0; i < count; i++)
			r[(off + i) & (RingSize - 1)] = data[i];
		off += count;

		return true;
	}

	bool on_audio_samples(const AudioMonitorSamplesEvent &e)
	{
		if (e.get_channel_index() != 0)
			return true;

		unsigned count = e.get_sample_count();
		const float *data = e.get_payload();
		for (unsigned i = 0; i < count; i++)
			ring[(offset + i) & (RingSize - 1)] = data[i];
		offset += count;

		return true;
	}

	Mixer *mixer = nullptr;
	StreamID id = 0;

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
			id = mixer->add_mixer_stream(DSP::create_tone_filter_stream(create_vorbis_stream("assets://audio/a.ogg")));
		else
			id = mixer->add_mixer_stream(DSP::create_tone_filter_stream(create_vorbis_stream("assets://audio/b.ogg")));

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
			//id = mixer->add_mixer_stream(create_vorbis_stream("assets://audio/a.ogg"));
			id = mixer->add_mixer_stream(DSP::create_tone_filter_stream(create_vorbis_stream("/tmp/test.ogg")));
			break;

		case Key::B:
			//id = mixer->add_mixer_stream(create_vorbis_stream("assets://audio/b.ogg"));
			id = mixer->add_mixer_stream(DSP::create_tone_filter_stream(create_vorbis_stream("/tmp/test2.ogg")));
			break;

		case Key::C:
			mixer->pause_stream(id);
			break;

		case Key::D:
			mixer->play_stream(id);
			break;

		default:
			break;
		}
		return true;
	}

	void render_frame(double, double) override
	{
		if (total_time > 0.0)
			LOGI("Samples / s = %f M/s\n", 1e-6 * double(total_samples) / total_time);
		auto &wsi = get_wsi();
		auto &device = wsi.get_device();

		auto cmd = device.request_command_buffer();

		auto rp = device.get_swapchain_render_pass(SwapchainRenderPass::ColorOnly);
		rp.clear_color[0].float32[0] = 0.0f;
		rp.clear_color[0].float32[1] = 0.0f;
		rp.clear_color[0].float32[2] = 0.0f;
		rp.clear_color[0].float32[3] = 1.0f;
		cmd->begin_render_pass(rp);
		cmd->set_opaque_state();
		cmd->set_program("assets://shaders/music_viz.vert", "assets://shaders/music_viz.frag");

		float width = cmd->get_viewport().width;
		float height = cmd->get_viewport().height;

		for (unsigned octave = 0; octave < DSP::ToneFilter::ToneCount / 12; octave++)
		{
			for (unsigned tone = 0; tone < 12; tone++)
			{
				unsigned off = tone_offset[octave][tone];
				auto *cmd_buffer = static_cast<float *>(cmd->allocate_vertex_data(0, (RingSize / 4) * sizeof(float),
				                                                                  sizeof(float)));
				for (unsigned i = 0; i < RingSize; cmd_buffer++, i += 4)
				{
					float val = 0.0f;
					for (unsigned j = 0; j < 4; j++)
						val += 0.25f * tone_ring[octave][tone][(i + j + off) & (RingSize - 1)];
					*cmd_buffer = val;
				}
				cmd->set_vertex_attrib(0, 0, VK_FORMAT_R32_SFLOAT, 0);
				cmd->set_primitive_topology(VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);

				struct Push
				{
					vec3 color;
					float inv_res;
				} push;
				push.inv_res = 1.0f / (RingSize / 4 - 1);
				push.color = mix(vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), float(tone) / 11.0f);
				push.color.y = (5.5f - abs(tone - 5.5f)) / 5.5f;
				cmd->push_constants(&push, 0, sizeof(push));

				const VkViewport vp = {
					width * float(tone) / 12.0f,
					height * 12.0f * float(octave) / DSP::ToneFilter::ToneCount,
					width / 12.0f,
					12.0f * height / DSP::ToneFilter::ToneCount,
					0.0f,
					1.0f,
				};

				VkClearRect clear_rect = {};
				VkClearValue clear_value = {};
				clear_rect.layerCount = 1;
				clear_rect.rect.offset.x = vp.x;
				clear_rect.rect.offset.y = vp.y;
				clear_rect.rect.extent.width  = vp.width;
				clear_rect.rect.extent.height = vp.height;

				float ratio = power_ratio[octave][tone];
				if (ratio < 0.0002f)
					clear_value.color.float32[2] = ratio / 0.0002f;
				else if (ratio < 0.09f)
					clear_value.color.float32[1] = (ratio - 0.0001f) * 20.0f;
				else
					clear_value.color.float32[0] = 100.0f * (ratio - 0.09f);
				cmd->clear_quad(0, clear_rect, clear_value);
				cmd->set_viewport(vp);
				cmd->draw(RingSize / 4);
			}
		}

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