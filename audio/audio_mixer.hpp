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

#pragma once

#include "audio_interface.hpp"
#include <atomic>
#include <vector>
#include <mutex>

namespace Granite
{
namespace Audio
{
class MixerStream
{
public:
	virtual ~MixerStream() = default;

	// Basically, a destructor.
	// Allows for more flexible resource recycling beyond operator delete.
	virtual void dispose()
	{
		delete this;
	}

	virtual void set_max_num_frames(size_t)
	{
	}

	// Must increment.
	virtual size_t accumulate_samples(float * const *channels, const float *gain, size_t num_frames) noexcept = 0;

	virtual unsigned get_num_channels() const = 0;
	virtual float get_sample_rate() const = 0;
};

using StreamID = uint64_t;

class Mixer : public BackendCallback
{
public:
	Mixer();
	~Mixer();

	// Will run in a critical thread.
	void mix_samples(float * const *channels, size_t num_frames) noexcept override;

	// Atomically adds a mixer stream. Might also dispose and replace an old stream.
	// Can only be called from a non-critical thread.
	// Returns StreamID(-1) if a mixer stream slot cannot be found.
	StreamID add_mixer_stream(MixerStream *stream);
	void kill_stream(StreamID index);

	// Garbage collection. Should be called regularly from a non-critical thread.
	void dispose_dead_streams();

	// Atomically sets stream parameters, such as gain and panning.
	// Panning is -1 (left), 0 (center), 1 (right).
	void set_stream_mixer_parameters(StreamID index, float gain_db, float panning);

private:
	enum { MaxSources = 128 };
	std::atomic<uint32_t> active_channel_mask[MaxSources / 32];
	MixerStream *mixer_streams[MaxSources] = {};

	// Actually float, bitcasted.
	std::atomic<uint32_t> panning[MaxSources];
	std::atomic<uint32_t> gain_linear[MaxSources];

	uint64_t stream_generation[MaxSources] = {};
	std::mutex non_critical_lock;

	size_t max_num_samples = 0;
	unsigned num_channels = 0;
	float sample_rate = 0.0f;

	void on_backend_start(float sample_rate, unsigned channels, size_t max_num_samples) override;
	void on_backend_stop() override;

	StreamID generate_stream_id(unsigned index);
	bool verify_stream_id(StreamID id);
	unsigned get_stream_index(StreamID id);
	uint64_t get_stream_generation(StreamID id);
};

namespace DSP
{
void accumulate_channel(float *output, const float *input, float gain, size_t count);
}

}
}