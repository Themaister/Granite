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

#include "audio_mixer.hpp"
#include "audio_resampler.hpp"
#include "util.hpp"
#include <string.h>
#include <cmath>

using namespace std;

#define NON_CRITICAL_THREAD_LOCK() \
	lock_guard<mutex> holder{non_critical_lock}

namespace Granite
{
namespace Audio
{
void Mixer::on_backend_start(float sample_rate, unsigned channels, size_t max_num_samples)
{
	this->max_num_samples = max_num_samples;
	this->sample_rate = sample_rate;
	this->num_channels = channels;

	constexpr unsigned iter = MaxSources / 32;
	for (unsigned i = 0; i < iter; i++)
	{
		uint32_t active_mask = active_channel_mask[i].load(memory_order_acquire);
		Util::for_each_bit(active_mask, [&](unsigned bit) {
			MixerStream *stream = mixer_streams[bit + 32 * i];
			if (stream)
				stream->setup(sample_rate, channels, max_num_samples);
		});
	}
	is_active = true;
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
	active_channel_mask[index].fetch_and(subindex, memory_order_release);
}

void Mixer::mix_samples(float *const *channels, size_t num_frames) noexcept
{
	for (unsigned c = 0; c < num_channels; c++)
		memset(channels[c], 0, num_frames * sizeof(float));
	float gains[Backend::MaxAudioChannels];

	constexpr unsigned iter = MaxSources / 32;
	for (unsigned i = 0; i < iter; i++)
	{
		uint32_t active_mask = active_channel_mask[i].load(memory_order_acquire);
		if (!active_mask)
			continue;

		uint32_t dead_mask = 0;

		Util::for_each_bit(active_mask, [&](unsigned bit) {
			unsigned index = bit + 32 * i;
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

			size_t got = mixer_streams[index]->accumulate_samples(channels, gains, num_frames);
			if (got < num_frames)
				dead_mask |= 1u << bit;
		});

		active_channel_mask[i].fetch_and(~dead_mask, memory_order_release);
	}
}

StreamID Mixer::add_mixer_stream(MixerStream *stream)
{
	if (!is_active)
	{
		LOGE("Mixer is not active, cannot add streams.\n");
		return StreamID(-1);
	}

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

		if (stream->get_sample_rate() != sample_rate)
		{
			auto *resample_stream = new ResampledStream(stream);
			stream = resample_stream;
		}

		mixer_streams[index] = stream;
		stream->setup(sample_rate, num_channels, max_num_samples);

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
		});
	}
}

void Mixer::set_stream_mixer_parameters(StreamID id, float gain_db, float panning)
{
	NON_CRITICAL_THREAD_LOCK();
	if (!verify_stream_id(id))
		return;

	unsigned index = get_stream_index(id);
	gain_linear[index].store(f32_to_u32(std::pow(10.0f, gain_db / 20.0f)));
	this->panning[index].store(f32_to_u32(panning));
}
}
}