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

#include "audio_pulse.hpp"
#include <pulse/pulseaudio.h>
#include "dsp/dsp.hpp"
#include "logging.hpp"
#include <string.h>

static constexpr size_t MAX_NUM_SAMPLES = 256;

namespace Granite
{
namespace Audio
{
struct Pulse final : Backend
{
	explicit Pulse(BackendCallback *callback_)
		: Backend(callback_)
	{
	}

	~Pulse() override;

	bool init(float sample_rate_, unsigned channels_);
	bool start() override;
	bool stop() override;

	bool get_buffer_status(size_t &write_avail, size_t &write_avail_frames, uint32_t &latency_usec) override;
	size_t write_frames_interleaved(const float *data, size_t frames, bool blocking) override;

	const char *get_backend_name() override
	{
		return "pulse";
	}

	float get_sample_rate() override
	{
		return sample_rate;
	}

	unsigned get_num_channels() override
	{
		return channels;
	}

	float sample_rate = 0.0f;
	unsigned channels = 0;

	pa_threaded_mainloop *mainloop = nullptr;
	pa_context *context = nullptr;
	pa_stream *stream = nullptr;
	size_t buffer_frames = 0;
	int success = -1;
	bool has_success = false;
	bool is_active = false;

	void update_buffer_attr(const pa_buffer_attr &attr) noexcept;
	size_t to_frames(size_t size) const noexcept;
};

Pulse::~Pulse()
{
	if (is_active)
		stop();

	if (mainloop)
		pa_threaded_mainloop_stop(mainloop);

	if (stream)
	{
		pa_stream_disconnect(stream);
		pa_stream_unref(stream);
	}

	if (context)
	{
		pa_context_disconnect(context);
		pa_context_unref(context);
	}

	if (mainloop)
		pa_threaded_mainloop_free(mainloop);
}

static void stream_success_cb(pa_stream *, int success, void *data)
{
	auto *pa = static_cast<Pulse *>(data);
	pa->success = success;
	pa->has_success = true;
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void context_state_cb(pa_context *, void *data)
{
	auto *pa = static_cast<Pulse *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_state_cb(pa_stream *, void *data)
{
	auto *pa = static_cast<Pulse *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_buffer_attr_cb(pa_stream *s, void *data)
{
	auto *pa = static_cast<Pulse *>(data);
	auto *server_attr = pa_stream_get_buffer_attr(s);
	if (server_attr)
		pa->update_buffer_attr(*server_attr);
}

size_t Pulse::write_frames_interleaved(const float *data, size_t frames, bool blocking)
{
	if (callback)
		return 0;

	size_t written_frames = 0;
	pa_threaded_mainloop_lock(mainloop);

	while (frames)
	{
		size_t to_write = std::min(frames, to_frames(pa_stream_writable_size(stream)));
		if (to_write)
		{
			if (pa_stream_write(stream, data, to_write * channels * sizeof(float), nullptr, 0, PA_SEEK_RELATIVE))
			{
				LOGE("Failed to write to pulse stream.\n");
				break;
			}

			data += to_write * channels;
			written_frames += to_write;
			frames -= to_write;
		}
		else if (blocking)
			pa_threaded_mainloop_wait(mainloop);
		else
			break;
	}

	pa_threaded_mainloop_unlock(mainloop);
	return written_frames;
}

static void stream_request_cb(pa_stream *s, size_t length, void *data)
{
	auto *pa = static_cast<Pulse *>(data);
	auto *cb = pa->get_callback();

	// If we're not doing pull-based audio, just wake up main thread.
	if (!cb)
	{
		pa_threaded_mainloop_signal(pa->mainloop, 0);
		return;
	}

	// For callback based audio, render out audio immediately as requested.

	float mix_channels[Backend::MaxAudioChannels][MAX_NUM_SAMPLES];
	float *mix_channel_ptr[Backend::MaxAudioChannels];
	for (unsigned i = 0; i < pa->channels; i++)
		mix_channel_ptr[i] = mix_channels[i];

	void *out_data;
	if (pa_stream_begin_write(s, &out_data, &length) < 0)
	{
		LOGE("pa_stream_begin_write() failed.\n");
		return;
	}

	auto *out_interleaved = static_cast<float *>(out_data);
	size_t out_frames = pa->to_frames(length);
	unsigned channels = pa->channels;

	if (pa->is_active)
	{
		while (out_frames != 0)
		{
			size_t to_write = std::min<size_t>(out_frames, MAX_NUM_SAMPLES);
			cb->mix_samples(mix_channel_ptr, to_write);
			out_frames -= to_write;

			if (channels == 2)
			{
				DSP::interleave_stereo_f32(out_interleaved, mix_channels[0], mix_channels[1], to_write);
				out_interleaved += to_write * channels;
			}
			else
			{
				for (size_t f = 0; f < to_write; f++)
					for (unsigned c = 0; c < channels; c++)
						*out_interleaved++ = mix_channels[c][f];
			}
		}
	}
	else
		memset(out_interleaved, 0, sizeof(float) * channels * out_frames);

	if (pa_stream_write(s, out_data, length, nullptr, 0, PA_SEEK_RELATIVE) < 0)
	{
		LOGE("pa_stream_write() failed.\n");
		return;
	}

	// Update latency information.
	pa_usec_t latency_usec;
	int negative = 0;
	if (pa_stream_get_latency(s, &latency_usec, &negative) != 0)
		latency_usec = 0;
	if (negative)
		latency_usec = 0;

	cb->set_latency_usec(uint32_t(latency_usec));
}

size_t Pulse::to_frames(size_t size) const noexcept
{
	return size / (channels * sizeof(float));
}

void Pulse::update_buffer_attr(const pa_buffer_attr &attr) noexcept
{
	buffer_frames = to_frames(attr.tlength);
}

bool Pulse::init(float sample_rate_, unsigned channels_)
{
	channels = channels_;

	if (channels_ > MaxAudioChannels)
		return false;

	mainloop = pa_threaded_mainloop_new();
	if (!mainloop)
		return false;

	context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "Granite");
	if (!context)
		return false;

	pa_context_set_state_callback(context, context_state_cb, this);

	if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
		return false;

	pa_threaded_mainloop_lock(mainloop);
	if (pa_threaded_mainloop_start(mainloop) < 0)
		return false;

	while (pa_context_get_state(context) < PA_CONTEXT_READY)
		pa_threaded_mainloop_wait(mainloop);

	if (pa_context_get_state(context) != PA_CONTEXT_READY)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	if (sample_rate_ <= 0.0f)
		sample_rate_ = 48000.0f;

	pa_sample_spec spec = {};
	spec.format = PA_SAMPLE_FLOAT32NE;
	spec.channels = uint8_t(channels_);
	spec.rate = uint32_t(sample_rate_);

	stream = pa_stream_new(context, "audio", &spec, nullptr);
	if (!stream)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	pa_stream_set_state_callback(stream, stream_state_cb, this);
	pa_stream_set_write_callback(stream, stream_request_cb, this);
	pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_cb, this);

	pa_buffer_attr buffer_attr = {};
	buffer_attr.maxlength = -1u;
	buffer_attr.tlength = pa_usec_to_bytes(30000, &spec);
	buffer_attr.prebuf = -1u;
	buffer_attr.minreq = -1u;
	buffer_attr.fragsize = -1u;
	update_buffer_attr(buffer_attr);

	if (pa_stream_connect_playback(stream, nullptr, &buffer_attr,
	                               static_cast<pa_stream_flags_t>(PA_STREAM_AUTO_TIMING_UPDATE |
	                                                              PA_STREAM_ADJUST_LATENCY |
	                                                              PA_STREAM_INTERPOLATE_TIMING |
	                                                              PA_STREAM_FIX_RATE |
	                                                              PA_STREAM_START_CORKED),
	                               nullptr, nullptr) < 0)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	pa_stream_state_t state;
	while ((state = pa_stream_get_state(stream)) != PA_STREAM_READY)
	{
		if (!PA_STREAM_IS_GOOD(state))
		{
			pa_threaded_mainloop_unlock(mainloop);
			return false;
		}

		pa_threaded_mainloop_wait(mainloop);
	}

	auto *stream_spec = pa_stream_get_sample_spec(stream);
	sample_rate = float(stream_spec->rate);
	if (callback)
		callback->set_backend_parameters(sample_rate, channels_, MAX_NUM_SAMPLES);

	if (const auto *attr = pa_stream_get_buffer_attr(stream))
		update_buffer_attr(*attr);

	pa_threaded_mainloop_unlock(mainloop);
	return true;
}

bool Pulse::start()
{
	if (is_active)
		return false;

	has_success = false;
	pa_threaded_mainloop_lock(mainloop);
	if (callback)
		callback->on_backend_start();
	pa_operation_unref(pa_stream_cork(stream, 0, stream_success_cb, this));

	while (!has_success)
		pa_threaded_mainloop_wait(mainloop);
	pa_threaded_mainloop_unlock(mainloop);

	is_active = true;
	has_success = false;
	if (success < 0)
		LOGE("Pulse::start() failed.\n");
	return success >= 0;
}

bool Pulse::stop()
{
	if (!is_active)
		return false;

	has_success = false;
	pa_threaded_mainloop_lock(mainloop);
	pa_operation_unref(pa_stream_cork(stream, 1, stream_success_cb, this));

	while (!has_success)
		pa_threaded_mainloop_wait(mainloop);

	if (callback)
		callback->on_backend_stop();
	pa_threaded_mainloop_unlock(mainloop);

	is_active = false;
	has_success = false;
	if (success < 0)
		LOGE("Pulse::stop() failed.\n");
	return success >= 0;
}

bool Pulse::get_buffer_status(size_t &write_avail, size_t &max_write_avail, uint32_t &latency_usec)
{
	pa_threaded_mainloop_lock(mainloop);
	size_t writable_size = pa_stream_writable_size(stream);

	// Update latency information.
	pa_usec_t usec;
	int negative = 0;
	if (pa_stream_get_latency(stream, &usec, &negative) != 0)
		usec = 0;
	if (negative)
		usec = 0;
	latency_usec = usec;

	pa_threaded_mainloop_unlock(mainloop);

	write_avail = to_frames(writable_size);
	max_write_avail = buffer_frames;

	if (write_avail > max_write_avail)
		LOGW("Write avail %zu > max write avail %zu?\n", write_avail, max_write_avail);

	return true;
}

Backend *create_pulse_backend(BackendCallback *callback, float sample_rate, unsigned channels)
{
	auto *backend = new Pulse(callback);
	if (!backend->init(sample_rate, channels))
	{
		delete backend;
		return nullptr;
	}

	return backend;
}

struct PulseRecord final : RecordStream
{
	~PulseRecord() override;

	const char *get_backend_name() override
	{
		return "pulse";
	}

	float get_sample_rate() override
	{
		return sample_rate;
	}

	unsigned get_num_channels() override
	{
		return num_channels;
	}

	size_t read_frames_deinterleaved_f32(float * const *data, size_t frames, bool blocking) override;
	size_t read_frames_interleaved_f32(float *data, size_t frames, bool blocking) override;
	size_t read_frames_f32(float * const *data, size_t frames, bool blocking, bool interleaved);
	bool get_buffer_status(size_t &read_avail, uint32_t &latency_usec) override;
	void set_record_callback(RecordCallback *callback_) override;
	bool init(const char *ident, float sample_rate_, unsigned channels_);

	bool start() override;
	bool stop() override;

	RecordCallback *callback = nullptr;
	pa_threaded_mainloop *mainloop = nullptr;
	pa_context *context = nullptr;
	pa_stream *stream = nullptr;
	float sample_rate = 0.0f;
	unsigned num_channels = 0;

	const float *peek_buffer = nullptr;
	size_t peek_buffer_frames = 0;
	size_t pull_buffer_offset = 0;

	void drop_current_peek_locked();
	bool is_running = false;

	int success = 0;
	bool has_success = false;
};

static void stream_record_success_cb(pa_stream *, int success, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa->success = success;
	pa->has_success = true;
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_record_context_state_cb(pa_context *, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_record_state_cb(pa_stream *, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_record_request_cb(pa_stream *, size_t, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);

	// If we're not doing callback, just wake up pollers.
	if (!pa->callback)
	{
		pa_threaded_mainloop_signal(pa->mainloop, 0);
		return;
	}

	const float *peek_buffer;
	size_t peek_size;
	while (pa_stream_peek(pa->stream, reinterpret_cast<const void **>(&peek_buffer), &peek_size) == 0 && peek_size != 0)
	{
		size_t peek_buffer_frames = peek_size / (sizeof(float) * pa->num_channels);
		pa->callback->write_frames_interleaved_f32(peek_buffer, peek_buffer_frames);
		pa_stream_drop(pa->stream);
	}
}

static void stream_record_moved_cb(pa_stream *, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_record_suspended_cb(pa_stream *, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

static void stream_record_latency_update_cb(pa_stream *, void *data)
{
	auto *pa = static_cast<PulseRecord *>(data);
	pa_threaded_mainloop_signal(pa->mainloop, 0);
}

void PulseRecord::drop_current_peek_locked()
{
	if (peek_buffer_frames)
	{
		pa_stream_drop(stream);
		peek_buffer_frames = 0;
		pull_buffer_offset = 0;
		peek_buffer = nullptr;
	}
}

PulseRecord::~PulseRecord()
{
	if (mainloop)
	{
		pa_threaded_mainloop_lock(mainloop);
		drop_current_peek_locked();
		pa_threaded_mainloop_unlock(mainloop);
	}

	if (mainloop)
		pa_threaded_mainloop_stop(mainloop);

	if (stream)
	{
		pa_stream_disconnect(stream);
		pa_stream_unref(stream);
	}

	if (context)
	{
		pa_context_disconnect(context);
		pa_context_unref(context);
	}

	if (mainloop)
		pa_threaded_mainloop_free(mainloop);
}

void PulseRecord::set_record_callback(RecordCallback *callback_)
{
	callback = callback_;
}

bool PulseRecord::init(const char *ident, float sample_rate_, unsigned int channels_)
{
	sample_rate = sample_rate_;
	num_channels = channels_;

	// Only bother with stereo recording for now.
	if (channels_ != 2)
		return false;

	mainloop = pa_threaded_mainloop_new();
	if (!mainloop)
		return false;

	context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "Granite");
	if (!context)
		return false;

	pa_context_set_state_callback(context, stream_record_context_state_cb, this);

	if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
		return false;

	pa_threaded_mainloop_lock(mainloop);
	if (pa_threaded_mainloop_start(mainloop) < 0)
		return false;

	while (pa_context_get_state(context) < PA_CONTEXT_READY)
		pa_threaded_mainloop_wait(mainloop);

	if (pa_context_get_state(context) != PA_CONTEXT_READY)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	pa_sample_spec spec = {};
	spec.format = PA_SAMPLE_FLOAT32NE;
	spec.channels = uint8_t(channels_);
	spec.rate = uint32_t(sample_rate_);

	stream = pa_stream_new(context, ident, &spec, nullptr);
	if (!stream)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	pa_stream_set_state_callback(stream, stream_record_state_cb, this);
	pa_stream_set_read_callback(stream, stream_record_request_cb, this);
	pa_stream_set_moved_callback(stream, stream_record_moved_cb, this);
	pa_stream_set_suspended_callback(stream, stream_record_suspended_cb, this);
	pa_stream_set_latency_update_callback(stream, stream_record_latency_update_cb, this);

	pa_buffer_attr buffer_attr = {};
	buffer_attr.maxlength = pa_usec_to_bytes(200000, &spec);
	buffer_attr.tlength = -1u;
	buffer_attr.prebuf = -1u;
	buffer_attr.minreq = -1u;
	buffer_attr.fragsize = pa_usec_to_bytes(10000, &spec);

	if (pa_stream_connect_record(stream, nullptr, &buffer_attr,
	                             static_cast<pa_stream_flags_t>(PA_STREAM_AUTO_TIMING_UPDATE |
	                                                            PA_STREAM_START_CORKED |
	                                                            PA_STREAM_INTERPOLATE_TIMING)) < 0)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	pa_stream_state_t state;
	while ((state = pa_stream_get_state(stream)) != PA_STREAM_READY)
	{
		if (!PA_STREAM_IS_GOOD(state))
		{
			pa_threaded_mainloop_unlock(mainloop);
			return false;
		}

		pa_threaded_mainloop_wait(mainloop);
	}

	const auto *attr = pa_stream_get_buffer_attr(stream);
	LOGI("attr->fragsize = %u\n", attr->fragsize);
	LOGI("attr->maxlength = %u\n", attr->maxlength);

	pa_threaded_mainloop_unlock(mainloop);
	return true;
}

bool PulseRecord::start()
{
	if (is_running)
		return false;

	has_success = false;
	pa_threaded_mainloop_lock(mainloop);
	pa_operation_unref(pa_stream_cork(stream, 0, stream_record_success_cb, this));
	while (!has_success)
		pa_threaded_mainloop_wait(mainloop);
	pa_threaded_mainloop_unlock(mainloop);

	has_success = false;
	if (success < 0)
		LOGE("PulseRecord::start() failed.\n");
	is_running = success >= 0;
	return is_running;
}

bool PulseRecord::stop()
{
	if (!is_running)
		return false;

	has_success = false;
	pa_threaded_mainloop_lock(mainloop);
	pa_operation_unref(pa_stream_cork(stream, 1, stream_record_success_cb, this));
	while (!has_success)
		pa_threaded_mainloop_wait(mainloop);
	pa_threaded_mainloop_unlock(mainloop);

	has_success = false;
	if (success < 0)
		LOGE("PulseRecord::stop() failed.\n");
	is_running = success < 0;
	return !is_running;
}

bool PulseRecord::get_buffer_status(size_t &read_avail, uint32_t &latency_usec)
{
	pa_threaded_mainloop_lock(mainloop);

	size_t avail = pa_stream_readable_size(stream);
	if (avail == size_t(-1))
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	read_avail = avail / (sizeof(float) * num_channels);

	if (pull_buffer_offset > read_avail)
	{
		LOGE("pull_buffer_offset %zu > read_avail %zu\n", pull_buffer_offset, read_avail);
		read_avail = 0;
	}
	else
		read_avail -= pull_buffer_offset;

	pa_usec_t usecs;
	int negative;
	if (pa_stream_get_latency(stream, &usecs, &negative) != 0)
	{
		pa_threaded_mainloop_unlock(mainloop);
		return false;
	}

	if (negative)
		latency_usec = 0;
	else
		latency_usec = uint32_t(usecs);

	pa_threaded_mainloop_unlock(mainloop);
	return true;
}

size_t PulseRecord::read_frames_f32(float * const *data, size_t frames, bool blocking, bool interleaved)
{
	if (!is_running)
		return 0;

	size_t num_read_frames = 0;
	pa_threaded_mainloop_lock(mainloop);

	while (frames)
	{
		size_t peek_avail = peek_buffer_frames - pull_buffer_offset;
		if (peek_avail)
		{
			size_t to_write = std::min(peek_avail, frames);

			if (data)
			{
				if (interleaved)
				{
					if (peek_buffer)
					{
						memcpy(data[0] + num_read_frames * num_channels,
						       peek_buffer + num_channels * pull_buffer_offset,
						       to_write * num_channels * sizeof(float));
					}
					else
					{
						memset(data[0] + num_read_frames * num_channels, 0, to_write * num_channels * sizeof(float));
					}
				}
				else if (peek_buffer)
				{
					if (num_channels == 2)
					{
						DSP::deinterleave_stereo_f32(data[0] + num_read_frames, data[1] + num_read_frames,
						                             peek_buffer + 2 * pull_buffer_offset, to_write);
					}
					else
					{
						for (size_t i = 0; i < to_write; i++)
							for (unsigned c = 0; c < num_channels; c++)
								data[c][num_read_frames + i] = peek_buffer[num_channels * (i + pull_buffer_offset) + c];
					}
				}
				else
				{
					for (unsigned c = 0; c < num_channels; c++)
						memset(data[c] + num_read_frames, 0, to_write * sizeof(float));
				}
			}

			pull_buffer_offset += to_write;
			frames -= to_write;
			num_read_frames += to_write;
		}
		else
		{
			// We've drained a fragment, peek into a new one.
			drop_current_peek_locked();

			size_t peek_size;
			if (pa_stream_peek(stream, reinterpret_cast<const void **>(&peek_buffer), &peek_size) < 0)
				break;

			if (peek_size == 0)
			{
				if (blocking)
					pa_threaded_mainloop_wait(mainloop);
				else
					break;
			}

			peek_buffer_frames = peek_size / (sizeof(float) * num_channels);
		}
	}

	// If we ended up reading exactly one fragment, drop the fragment here.
	if (peek_buffer_frames == pull_buffer_offset)
		drop_current_peek_locked();

	pa_threaded_mainloop_unlock(mainloop);
	return num_read_frames;
}

size_t PulseRecord::read_frames_deinterleaved_f32(float *const *data, size_t frames, bool blocking)
{
	return read_frames_f32(data, frames, blocking, false);
}

size_t PulseRecord::read_frames_interleaved_f32(float *data, size_t frames, bool blocking)
{
	return read_frames_f32(&data, frames, blocking, true);
}

RecordStream *create_pulse_record_backend(const char *ident, float sample_rate, unsigned channels)
{
	auto *backend = new PulseRecord();
	if (!backend->init(ident, sample_rate, channels))
	{
		delete backend;
		return nullptr;
	}

	return backend;
}
}
}
