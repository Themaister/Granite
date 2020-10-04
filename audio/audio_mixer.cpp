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

#include "audio_mixer.hpp"
#include "audio_resampler.hpp"
#include "audio_events.hpp"
#include "timer.hpp"
#include "logging.hpp"
#include "bitops.hpp"
#include <string.h>
#include <cmath>

using namespace std;

#define NON_CRITICAL_THREAD_LOCK() \
	lock_guard<mutex> holder{non_critical_lock}

//#define AUDIO_MIXER_DEBUG

namespace Granite
{
namespace Audio
{
void MixerStream::install_message_queue(StreamID id, Util::LockFreeMessageQueue *queue)
{
	stream_id = id;
	message_queue = queue;
}

void Mixer::set_backend_parameters(float sample_rate_, unsigned channels_, size_t max_num_samples_)
{
	max_num_samples = max_num_samples_;
	sample_rate = sample_rate_;
	num_channels = channels_;
	inv_sample_rate = 1.0 / sample_rate;
}

void Mixer::on_backend_start()
{
	is_active = true;
}

void Mixer::set_latency_usec(uint32_t usec)
{
	latency.store(usec, memory_order_release);
}

static float u32_to_f32(uint32_t v)
{
	union
	{
		float f32;
		uint32_t u32;
	} u;
	u.u32 = v;
	return u.f32;
}

static uint32_t f32_to_u32(float v)
{
	union
	{
		float f32;
		uint32_t u32;
	} u;
	u.f32 = v;
	return u.u32;
}

static float saturate(float v)
{
	if (v < 0.0f)
		return 0.0f;
	else if (v > 1.0f)
		return 1.0f;
	else
		return v;
}

Mixer::Mixer()
{
	for (auto &pan : panning)
		pan = f32_to_u32(0.0f);
	for (auto &gain : gain_linear)
		gain = f32_to_u32(1.0f);
	for (auto &active : active_channel_mask)
		active = 0;
	latency = 0;
}

void Mixer::on_backend_stop()
{
	dispose_dead_streams();
	is_active = false;
}

Mixer::~Mixer()
{
	on_backend_stop();
	for (auto *stream : mixer_streams)
		if (stream)
			stream->dispose();
}

unsigned Mixer::get_stream_index(StreamID id)
{
	static_assert((MaxSources & (MaxSources - 1)) == 0, "MaxSources must be POT.");
	return unsigned(id & (MaxSources - 1));
}

StreamID Mixer::generate_stream_id(unsigned index)
{
	uint64_t generation = ++stream_generation[index];
	return generation * MaxSources + index;
}

uint64_t Mixer::get_stream_generation(StreamID id)
{
	return id / unsigned(MaxSources);
}

bool Mixer::verify_stream_id(StreamID id)
{
	if (id == 0)
		return false;

	unsigned index = get_stream_index(id);
	uint64_t generation = get_stream_generation(id);
	uint64_t actual_generation = stream_generation[index];
	return actual_generation == generation;
}

void Mixer::kill_stream(StreamID id)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return;

	unsigned index = get_stream_index(id);
	unsigned subindex = index & 31;
	index /= 32;
	active_channel_mask[index].fetch_and(~(1u << subindex), memory_order_release);
}

double Mixer::get_play_cursor(StreamID id)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return -1.0;

	unsigned index = get_stream_index(id);
	return stream_adjusted_play_cursors_usec[index].load(memory_order_acquire) * 1e-6;
}

Mixer::StreamState Mixer::get_stream_state(Granite::Audio::StreamID id)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return StreamState::Dead;

	unsigned index = get_stream_index(id);
	if ((active_channel_mask[index / 32].load(memory_order_acquire) & (1u << (index & 31))) == 0)
		return StreamState::Dead;

	return stream_playing[index].load(memory_order_relaxed) ? StreamState::Playing : StreamState::Paused;
}

void Mixer::update_stream_play_cursor(unsigned index, double new_latency) noexcept
{
	double t = double(stream_raw_play_cursors[index]) * inv_sample_rate;
	t -= new_latency;
	if (t < 0.0)
		t = 0.0;
	auto t_usec = uint64_t(t * 1e6);

	uint64_t old_cursor = stream_adjusted_play_cursors_usec[index].load(memory_order_relaxed);
	if (t_usec > old_cursor)
		stream_adjusted_play_cursors_usec[index].store(t_usec, memory_order_release);
}

void Mixer::mix_samples(float *const *channels, size_t num_frames) noexcept
{
	for (unsigned c = 0; c < num_channels; c++)
		memset(channels[c], 0, num_frames * sizeof(float));
	float gains[Backend::MaxAudioChannels];

	auto current_latency = double(latency.load(memory_order_acquire)) * 1e-6;

	constexpr unsigned iter = MaxSources / 32;
	for (unsigned i = 0; i < iter; i++)
	{
		uint32_t active_mask = active_channel_mask[i].load(memory_order_acquire);
		if (!active_mask)
			continue;

		uint32_t dead_mask = 0;

		Util::for_each_bit(active_mask, [&](unsigned bit) {
			unsigned index = bit + 32 * i;
			if (!stream_playing[index].load(memory_order_acquire))
				return;

			float gain = u32_to_f32(gain_linear[index].load(memory_order_relaxed));
			float pan = u32_to_f32(panning[index].load(memory_order_relaxed));

			if (num_channels != 2)
			{
				for (unsigned c = 0; c < num_channels; c++)
					gains[c] = gain;
			}
			else
			{
				gains[0] = gain * saturate(1.0f - pan);
				gains[1] = gain * saturate(1.0f + pan);
			}

#ifdef AUDIO_MIXER_DEBUG
			auto start_time = Util::get_current_time_nsecs();
#endif

			size_t got = mixer_streams[index]->accumulate_samples(channels, gains, num_frames);

#ifdef AUDIO_MIXER_DEBUG
			auto end_time = Util::get_current_time_nsecs();
			emplace_audio_event_on_queue<AudioStreamPerformanceEvent>(message_queue, mixer_streams[index]->get_stream_id(),
			                                                          1e-9 * (end_time - start_time), got);

#endif

			stream_raw_play_cursors[index] += got;
			update_stream_play_cursor(index, current_latency);

			if (got < num_frames)
			{
				dead_mask |= 1u << bit;
				emplace_audio_event_on_queue<StreamStoppedEvent>(message_queue, bit + 32 * i);
			}
		});

		active_channel_mask[i].fetch_and(~dead_mask, memory_order_release);
	}

#ifdef AUDIO_MIXER_DEBUG
	// Pump audio data to the event queue, so applications can monitor the audio backend visually :3
	for (unsigned c = 0; c < num_channels; c++)
	{
		emplace_padded_audio_event_on_queue<AudioMonitorSamplesEvent>(message_queue, num_frames * sizeof(float),
		                                                              c, channels[c], num_frames);
	}
#endif
}

StreamID Mixer::add_mixer_stream(MixerStream *stream, bool start_playing,
                                 float initial_gain_db, float initial_panning)
{
	if (!stream)
		return StreamID(-1);

	// Cannot deal with this yet.
	if (stream->get_num_channels() != num_channels)
	{
		LOGE("Number of audio channels in stream does not match mixer.\n");
		return StreamID(-1);
	}

	// add_mixer_stream is only called by non-critical threads,
	// so it's fine to lock.
	// It is unsafe for multiple threads to create a stream here, since they might allocate
	// the same index.

	// The only important non-locking code is the audio thread, which can only use atomics.
	NON_CRITICAL_THREAD_LOCK();

	constexpr unsigned iter = MaxSources / 32;
	for (unsigned i = 0; i < iter; i++)
	{
		uint32_t vacant_mask = ~active_channel_mask[i].load(memory_order_acquire);
		if (!vacant_mask)
			continue;

		uint32_t subindex = trailing_zeroes(vacant_mask);
		uint32_t index = i * 32 + subindex;

		MixerStream *old_stream = mixer_streams[index];
		StreamID id = generate_stream_id(index);
		stream->install_message_queue(id, &message_queue);

		stream->setup(sample_rate, num_channels, max_num_samples);

		if (stream->get_sample_rate() != sample_rate)
		{
			auto *resample_stream = new ResampledStream(stream);
			stream = resample_stream;
			stream->setup(sample_rate, num_channels, max_num_samples);
		}

		// Can all be relaxed here.
		// The mixer thread will be dependent on the active_channel_mask having been kicked.
		mixer_streams[index] = stream;
		stream_raw_play_cursors[index] = 0;
		stream_adjusted_play_cursors_usec[index].store(0, memory_order_relaxed);
		gain_linear[index].store(f32_to_u32(std::pow(10.0f, initial_gain_db / 20.0f)), memory_order_relaxed);
		panning[index].store(f32_to_u32(initial_panning), memory_order_relaxed);
		stream_playing[index].store(start_playing, memory_order_relaxed);

		// Kick mixer thread.
		active_channel_mask[i].fetch_or(1u << subindex, memory_order_release);

		if (old_stream)
			old_stream->dispose();

		return id;
	}

	return unsigned(-1);
}

void Mixer::dispose_dead_streams()
{
	NON_CRITICAL_THREAD_LOCK();
	constexpr unsigned iter = MaxSources / 32;
	for (unsigned i = 0; i < iter; i++)
	{
		uint32_t dead_mask = ~active_channel_mask[i].load(memory_order_acquire);
		Util::for_each_bit(dead_mask, [&](unsigned bit) {
			MixerStream *old_stream = mixer_streams[bit + 32 * i];
			if (old_stream)
				old_stream->dispose();
			mixer_streams[bit + 32 * i] = nullptr;
			stream_generation[bit + 32 * i] = 0;
		});
	}
}

Util::LockFreeMessageQueue &Mixer::get_message_queue()
{
	return message_queue;
}

bool Mixer::play_stream(StreamID id)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return false;

	unsigned index = get_stream_index(id);
	stream_playing[index].store(true, memory_order_release);
	return true;
}

bool Mixer::pause_stream(StreamID id)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return false;

	unsigned index = get_stream_index(id);
	stream_playing[index].store(false, memory_order_release);
	return true;
}

void Mixer::set_stream_mixer_parameters(StreamID id, float new_gain_db, float new_panning)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return;

	unsigned index = get_stream_index(id);
	gain_linear[index].store(f32_to_u32(std::pow(10.0f, new_gain_db / 20.0f)), memory_order_release);
	panning[index].store(f32_to_u32(new_panning), memory_order_release);
}
}
}
