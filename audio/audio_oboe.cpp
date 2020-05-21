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

#include "audio_oboe.hpp"
#include "dsp/dsp.hpp"
#include "logging.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <oboe/Oboe.h>

namespace Granite
{
namespace Audio
{
void set_oboe_low_latency_parameters(unsigned sample_rate, unsigned block_frames)
{
	// For OpenSL ES fallback path.
	oboe::DefaultStreamValues::SampleRate = sample_rate;
	oboe::DefaultStreamValues::FramesPerBurst = block_frames;
}

struct OboeBackend : Backend, oboe::AudioStreamCallback
{
	OboeBackend(BackendCallback &callback)
			: Backend(callback)
	{
		device_alive.test_and_set();
	}

	~OboeBackend();
	bool init(float target_sample_rate, unsigned channels);

	const char *get_backend_name() override
	{
		return "Oboe";
	}

	float get_sample_rate() override
	{
		return sample_rate;
	}

	unsigned get_num_channels() override
	{
		return num_channels;
	}

	bool start() override;
	bool stop() override;
	void heartbeat() override;

	oboe::AudioStream *stream = nullptr;
	std::vector<float> mix_buffers[Backend::MaxAudioChannels];
	float *mix_buffers_ptr[Backend::MaxAudioChannels] = {};

	oboe::AudioFormat format = oboe::AudioFormat::Unspecified;
	float sample_rate = 0.0f;
	double inv_sample_rate = 0.0;
	unsigned num_channels = 0;
	int64_t frame_count = 0;
	int32_t frames_per_callback = 0;
	int32_t old_underrun_count = 0;
	bool is_active = false;

	double last_latency = 0.0;

	oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboe_stream,
	                                      void *audio_data,
	                                      int32_t num_frames) override;

	void onErrorBeforeClose(oboe::AudioStream *oboe_stream, oboe::Result error) override;
	void onErrorAfterClose(oboe::AudioStream *oboe_stream, oboe::Result error) override;

	void setup_stream_builder(oboe::AudioStreamBuilder &builder);
	bool reinit();
	std::atomic_flag device_alive;
};

OboeBackend::~OboeBackend()
{
	stop();
	if (stream)
		stream->close();
	stream = nullptr;
}

bool OboeBackend::reinit()
{
	// Apparently the error callbacks can be called multiple times
	// from reading some Oboe samples.
	bool ret = init(0.0f, num_channels);
	if (!ret)
		return false;

	if (is_active)
	{
		is_active = false;
		if (!start())
			return false;

		LOGI("Oboe: Recovered from disconnect!\n");
	}

	return true;
}

void OboeBackend::setup_stream_builder(oboe::AudioStreamBuilder &builder)
{
	builder.setDirection(oboe::Direction::Output);
	builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
	builder.setCallback(this);
	// XXX: AAudio is broken on my device. See
	// https://github.com/google/oboe/issues/380
	// https://github.com/google/oboe/issues/381
	// Force OpenSLES for now. It works quite well.
	builder.setAudioApi(oboe::AudioApi::OpenSLES);
	builder.setChannelCount(num_channels);
	builder.setContentType(oboe::ContentType::Music);
	builder.setSharingMode(oboe::SharingMode::Shared);
	builder.setUsage(oboe::Usage::Game);

	// If we have already committed to a sample rate, keep using it.
	if (sample_rate != 0.0f)
		builder.setSampleRate(int32_t(sample_rate));
}

bool OboeBackend::init(float, unsigned channels)
{
	num_channels = channels;
	oboe::AudioStreamBuilder builder;
	setup_stream_builder(builder);

	if (builder.openStream(&stream) != oboe::Result::OK)
	{
		LOGE("Failed to create Oboe stream!\n");
		return false;
	}

	sample_rate = stream->getSampleRate();
	inv_sample_rate = 1.0 / sample_rate;
	num_channels = unsigned(stream->getChannelCount());
	format = stream->getFormat();

	if (!frames_per_callback)
	{
		frames_per_callback = stream->getFramesPerCallback();
		if (frames_per_callback == oboe::kUnspecified)
			frames_per_callback = stream->getFramesPerBurst();

		// Allocate mix-buffers.
		// If we have to generate more than this in a callback, iterate multiple times ...
		for (unsigned c = 0; c < num_channels; c++)
		{
			mix_buffers[c].resize(frames_per_callback);
			mix_buffers_ptr[c] = mix_buffers[c].data();
		}

		callback.set_backend_parameters(sample_rate, num_channels, size_t(frames_per_callback));
	}

	// Set initial latency estimate.
	last_latency = double(stream->getBufferSizeInFrames()) * inv_sample_rate;
	callback.set_latency_usec(uint32_t(last_latency * 1e6));

	return true;
}

void OboeBackend::onErrorBeforeClose(oboe::AudioStream *, oboe::Result error)
{
	LOGW("Oboe: Error before close: %s.\n", oboe::convertToText(error));
}

void OboeBackend::onErrorAfterClose(oboe::AudioStream *, oboe::Result error)
{
	LOGW("Oboe: Error after close: %s.\n", oboe::convertToText(error));
	if (error == oboe::Result::ErrorDisconnected)
		device_alive.clear(std::memory_order_release);
}

oboe::DataCallbackResult OboeBackend::onAudioReady(
		oboe::AudioStream *oboe_stream,
		void *audio_data,
		int32_t num_frames)
{
	auto xrun_count = oboe_stream->getXRunCount();
	if (xrun_count.error() == oboe::Result::OK)
	{
		int32_t underrun_count = xrun_count.value();
		if (underrun_count > old_underrun_count)
		{
			LOGW("Oboe: observed %d new underruns.", underrun_count - old_underrun_count);
			old_underrun_count = underrun_count;
		}
	}

	// Update measured latency.
	// Can fail spuriously, don't update latency estimate in that case.
	int64_t frame_position, time_ns;
	if (oboe_stream->getTimestamp(CLOCK_MONOTONIC, &frame_position, &time_ns) == oboe::Result::OK)
	{
		timespec ts;
		if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		{
			int64_t current_ns = ts.tv_sec * 1000000000 + ts.tv_nsec;

			// Extrapolate play counter based on timestamp.
			double playing_time = double(frame_position) * inv_sample_rate;
			playing_time += 1e-9 * double(current_ns - time_ns);
			double pushed_time = double(frame_count) * inv_sample_rate;
			double latency = pushed_time - playing_time;
			if (latency < 0.0)
				latency = 0.0;

			// Interpolate latency over time for a smoother result.
			last_latency = 0.95 * last_latency + 0.05 * latency;
			callback.set_latency_usec(uint32_t(last_latency * 1e6));

			//LOGI("Measured latency: %.3f ms\n", last_latency * 1000.0);
		}
	}

	frame_count += num_frames;

	union
	{
		int16_t *i16;
		float *f32;
		void *data;
	} u;
	u.data = audio_data;

	// Ideally we'll only run this loop once, but you never know ...
	while (num_frames)
	{
		auto to_render = std::min(num_frames, frames_per_callback);

		callback.mix_samples(mix_buffers_ptr, size_t(to_render));

		// Deal with whatever format AAudio wants.
		// Convert from deinterleaved F32 to whatever.
		if (format == oboe::AudioFormat::Float && num_channels == 2)
		{
			DSP::interleave_stereo_f32(u.f32, mix_buffers_ptr[0], mix_buffers_ptr[1], size_t(to_render));
			u.f32 += to_render * 2;
		}
		else if (format == oboe::AudioFormat::Float)
		{
			for (int f = 0; f < to_render; f++)
				for (unsigned c = 0; c < num_channels; c++)
					*u.f32++ = mix_buffers[c][f];
		}
		else if (format == oboe::AudioFormat::I16 && num_channels == 2)
		{
			DSP::interleave_stereo_f32_i16(u.i16, mix_buffers_ptr[0], mix_buffers_ptr[1], size_t(to_render));
			u.i16 += to_render * 2;
		}
		else
		{
			for (int f = 0; f < to_render; f++)
				for (unsigned c = 0; c < num_channels; c++)
					*u.i16++ = DSP::f32_to_i16(mix_buffers[c][f]);
		}

		num_frames -= to_render;
	}

	return oboe::DataCallbackResult::Continue;
}

// Called periodically from the main loop, just in case we need to recover from a device lost.
void OboeBackend::heartbeat()
{
	if (!device_alive.test_and_set(std::memory_order_acquire))
	{
		stream = nullptr;
		if (!reinit())
			LOGE("Oboe: Failed to reinit stream.\n");
	}
}

bool OboeBackend::start()
{
	if (is_active)
		return false;

	if (!stream)
		return false;

	callback.on_backend_start();
	frame_count = 0;
	old_underrun_count = 0;

	// Starts async, and will pull from callback.
	oboe::Result res;
	if ((res = stream->requestStart()) != oboe::Result::OK)
	{
		LOGE("Oboe: Failed to start stream (%s).\n", oboe::convertToText(res));
		return false;
	}

	is_active = true;
	return true;
}

bool OboeBackend::stop()
{
	if (!is_active)
		return false;

	if (!stream)
		return false;

	oboe::Result res;
	if ((res = stream->stop(1000000)) != oboe::Result::OK)
	{
		LOGE("Oboe: Failed to stop stream (%s).\n", oboe::convertToText(res));
		return false;
	}

	callback.on_backend_stop();
	is_active = false;
	return true;
}

Backend *create_oboe_backend(BackendCallback &callback, float sample_rate, unsigned channels)
{
	auto *oboe = new OboeBackend(callback);
	if (!oboe->init(sample_rate, channels))
	{
		delete oboe;
		return nullptr;
	}
	return oboe;
}
}
}
