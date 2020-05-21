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

#include "audio_aaudio.hpp"
#include "dsp/dsp.hpp"
#include "logging.hpp"
#include <dlfcn.h>
#include <stdint.h>
#include <aaudio/AAudio.h>
#include <time.h>
#include <cmath>
#include <vector>
#include <algorithm>

namespace Granite
{
extern uint32_t android_api_version;
namespace Audio
{
static aaudio_data_callback_result_t aaudio_callback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames);
static void aaudio_error_callback(AAudioStream *stream, void *userData, aaudio_result_t error);

struct AAudioBackend : Backend
{
	AAudioBackend(BackendCallback &callback)
			: Backend(callback)
	{
		device_alive.test_and_set();
	}

	~AAudioBackend();
	bool init(float target_sample_rate, unsigned channels);

	const char *get_backend_name() override
	{
		return "AAudio";
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

	void thread_callback(void *data, int32_t num_frames) noexcept;
	void thread_error(aaudio_result_t error) noexcept;

	bool reinit();

	AAudioStream *stream = nullptr;

	std::vector<float> mix_buffers[Backend::MaxAudioChannels];
	float *mix_buffers_ptr[Backend::MaxAudioChannels] = {};

	float sample_rate = 0.0f;
	double inv_sample_rate = 0.0;
	unsigned num_channels = 0;
	int64_t frame_count = 0;
	int32_t frames_per_callback = 0;
	int32_t old_underrun_count = 0;
	aaudio_format_t format;
	bool is_active = false;

	double last_latency = 0.0;
	std::atomic_flag device_alive;

	bool create_stream(float sample_rate, unsigned channels);
	bool update_buffer_size();
};

bool AAudioBackend::update_buffer_size()
{
	// We didn't ask for S16 or F32, let driver decide, we can deal with it.
	format = AAudioStream_getFormat(stream);

	// What's the hardware burst size? We should align on that.
	int32_t burst_frames = AAudioStream_getFramesPerBurst(stream);

	// We can tweak the buffer size dynamically up to this size.
	int32_t max_frames = AAudioStream_getBufferCapacityInFrames(stream);

	// Target 50 ms latency (can make it dynamic, but hey).
	int32_t target_blocks = int(std::ceil(50.0f * sample_rate / (1000.0f * burst_frames)));

	// At least double-buffer.
	target_blocks = std::max(target_blocks, 2);

	// Resize the buffer, need to ask for actual value later.
	aaudio_result_t res;
	if ((res = AAudioStream_setBufferSizeInFrames(stream, std::min(max_frames, target_blocks * burst_frames))) < 0)
	{
		LOGE("AAudio: Failed to set buffer size: %s\n", AAudio_convertResultToText(res));
		return false;
	}

	// Set up our mixer on first run-through.
	if (!frames_per_callback)
	{
		// frames_per_callback is an internal detail so we have some idea how much memory to allocate for mix buffers.
		// It shouldn't change on reinit.
		frames_per_callback = AAudioStream_getFramesPerDataCallback(stream);

		// It might be unspecified, so you get arbitrary amounts every callback,
		// limit ourselves internally to the more likely burst size.
		if (frames_per_callback == AAUDIO_UNSPECIFIED)
			frames_per_callback = AAudioStream_getFramesPerBurst(stream);

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
	last_latency = double(AAudioStream_getBufferSizeInFrames(stream)) * inv_sample_rate;
	callback.set_latency_usec(uint32_t(last_latency * 1e6));
	return true;
}

bool AAudioBackend::create_stream(float request_sample_rate, unsigned channels)
{
	aaudio_result_t res;
	AAudioStreamBuilder *builder = nullptr;
	if ((res = AAudio_createStreamBuilder(&builder)) != AAUDIO_OK)
	{
		LOGE("AAudio: Failed to create stream builder: %s\n", AAudio_convertResultToText(res));
		return false;
	}

	AAudioStreamBuilder_setChannelCount(builder, num_channels);

	// Of course we want this ;)
	//AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);

	// Only set explicit sampling rate if requested.
	if (request_sample_rate != 0.0f)
		AAudioStreamBuilder_setSampleRate(builder, int32_t(request_sample_rate));
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);

	// Data callback is better for latency.
	AAudioStreamBuilder_setDataCallback(builder, aaudio_callback, this);
	AAudioStreamBuilder_setErrorCallback(builder, aaudio_error_callback, this);

	if (android_api_version >= 28)
	{
		auto *set_usage = reinterpret_cast<void (*)(AAudioStreamBuilder *, aaudio_usage_t)>(
				dlsym(RTLD_NEXT, "AAudioStreamBuilder_setUsage"));
		if (set_usage)
			set_usage(builder, AAUDIO_USAGE_GAME);

		auto *set_content_type = reinterpret_cast<void (*)(AAudioStreamBuilder *, aaudio_content_type_t)>(
				dlsym(RTLD_NEXT, "AAudioStreamBuilder_setContentType"));
		if (set_content_type)
			set_content_type(builder, AAUDIO_CONTENT_TYPE_MUSIC);
	}

	if ((res = AAudioStreamBuilder_openStream(builder, &stream)) != AAUDIO_OK)
	{
		LOGE("AAudio: Failed to create stream: %s\n", AAudio_convertResultToText(res));
		stream = nullptr;
	}

	// Query actual sample rate.
	// FIXME: First time around we set up any sampling rate,
	// but the mixer is currently unable to change sampling rate on the fly,
	// so make sure we actually get what we ask for.
	sample_rate = AAudioStream_getSampleRate(stream);
	if (request_sample_rate != 0.0f && sample_rate != int32_t(request_sample_rate))
	{
		LOGE("AAudio: requested explicitly %f Hz sample rate, but got %d :(\n",
		     request_sample_rate, AAudioStream_getSampleRate(stream));
		AAudioStream_close(stream);
		stream = nullptr;
	}

	inv_sample_rate = 1.0 / sample_rate;
	num_channels = channels;
	AAudioStreamBuilder_delete(builder);
	return stream != nullptr;
}

bool AAudioBackend::reinit()
{
	if (!create_stream(sample_rate, num_channels))
		return false;
	if (!update_buffer_size())
		return false;

	if (is_active)
	{
		is_active = false;
		if (!start())
			return false;

		LOGI("AAudio: Recovered from error! sample rate %f, frames per callback: %d, buffer frames: %d.\n",
		     sample_rate, frames_per_callback, AAudioStream_getBufferSizeInFrames(stream));
	}

	return true;
}

bool AAudioBackend::init(float, unsigned channels)
{
	if (!create_stream(0.0f, channels))
		return false;
	if (!update_buffer_size())
		return false;

	LOGI("AAudio: sample rate %f, frames per callback: %d, buffer frames: %d.\n",
	     sample_rate, frames_per_callback, AAudioStream_getBufferSizeInFrames(stream));

	return true;
}

void AAudioBackend::thread_error(aaudio_result_t) noexcept
{
	// Need to deal with this on another thread later.
	device_alive.clear(std::memory_order_release);
}

// This must be hard-realtime safe!
void AAudioBackend::thread_callback(void *data, int32_t numFrames) noexcept
{
	int32_t underrun_count = AAudioStream_getXRunCount(stream);
	if (underrun_count > old_underrun_count)
	{
		LOGW("AAudio: observed %d new underruns.", underrun_count - old_underrun_count);
		old_underrun_count = underrun_count;
	}

	// Update measured latency.
	// Can fail spuriously, don't update latency estimate in that case.
	int64_t frame_position, time_ns;
	if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &frame_position, &time_ns) == AAUDIO_OK)
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

	frame_count += numFrames;

	union
	{
		int16_t *i16;
		float *f32;
		void *data;
	} u;
	u.data = data;

	// Ideally we'll only run this once, but you never know ...
	while (numFrames)
	{
		auto to_render = std::min(numFrames, frames_per_callback);

		callback.mix_samples(mix_buffers_ptr, size_t(to_render));

		// Deal with whatever format AAudio wants.
		// Convert from deinterleaved F32 to whatever.
		if (format == AAUDIO_FORMAT_PCM_FLOAT && num_channels == 2)
		{
			DSP::interleave_stereo_f32(u.f32, mix_buffers_ptr[0], mix_buffers_ptr[1], size_t(to_render));
			u.f32 += to_render * 2;
		}
		else if (format == AAUDIO_FORMAT_PCM_FLOAT)
		{
			for (int f = 0; f < to_render; f++)
				for (unsigned c = 0; c < num_channels; c++)
					*u.f32++ = mix_buffers[c][f];
		}
		else if (format == AAUDIO_FORMAT_PCM_I16 && num_channels == 2)
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

		numFrames -= to_render;
	}
}

static aaudio_data_callback_result_t aaudio_callback(AAudioStream *, void *userData, void *audioData, int32_t numFrames)
{
	auto *backend = static_cast<AAudioBackend *>(userData);
	backend->thread_callback(audioData, numFrames);
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void aaudio_error_callback(AAudioStream *, void *userData, aaudio_result_t error)
{
	auto *backend = static_cast<AAudioBackend *>(userData);
	backend->thread_error(error);
}

// Called periodically from the main loop, just in case we need to recover from a device lost.
void AAudioBackend::heartbeat()
{
	if (!device_alive.test_and_set(std::memory_order_acquire))
	{
		// Whoops. We're dead. Try to recover.
		LOGE("AAudio device was lost, trying to recover!\n");
		if (stream)
			AAudioStream_close(stream);
		stream = nullptr;
		callback.on_backend_stop();

		if (!reinit())
		{
			// Try again next heartbeat ...
			device_alive.clear(std::memory_order_release);
		}
	}
}

bool AAudioBackend::start()
{
	if (is_active)
		return false;

	if (!stream)
		return false;

	callback.on_backend_start();
	frame_count = 0;
	old_underrun_count = 0;

	// Starts async, and will pull from callback.
	aaudio_result_t res;
	if ((res = AAudioStream_requestStart(stream)) != AAUDIO_OK)
	{
		LOGE("AAudio: Failed to request stream start: %s\n", AAudio_convertResultToText(res));
		return false;
	}

	is_active = true;
	return true;
}

bool AAudioBackend::stop()
{
	if (!is_active)
		return false;

	if (!stream)
		return false;

	aaudio_result_t res;
	if ((res = AAudioStream_requestStop(stream)) != AAUDIO_OK)
	{
		LOGE("AAudio: Failed to request stream stop: %s\n", AAudio_convertResultToText(res));
		return false;
	}

	// To be safe, wait for the stream to go idle.
	aaudio_stream_state_t current_state = AAudioStream_getState(stream);
	aaudio_stream_state_t input_state = current_state;
	while ((res == AAUDIO_OK || res == AAUDIO_ERROR_TIMEOUT) && current_state != AAUDIO_STREAM_STATE_STOPPED)
	{
		res = AAudioStream_waitForStateChange(stream, input_state, &current_state, 10000000);
		input_state = current_state;
	}

	if (input_state != AAUDIO_STREAM_STATE_STOPPED)
	{
		LOGE("AAudio: Failed to stop stream!\n");
		return false;
	}

	callback.on_backend_stop();
	is_active = false;
	return true;
}

AAudioBackend::~AAudioBackend()
{
	stop();
	if (stream)
		AAudioStream_close(stream);
}

Backend *create_aaudio_backend(BackendCallback &callback, float sample_rate, unsigned channels)
{
	if (android_api_version < 27) // Android 8.1.0
	{
		LOGE("AAudio is known to be broken on Android 8.0, falling back ...");
		return nullptr;
	}

	auto *aa = new AAudioBackend(callback);
	if (!aa->init(sample_rate, channels))
	{
		delete aa;
		return nullptr;
	}
	return aa;
}
}
}
