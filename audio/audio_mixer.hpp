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

#pragma once

#include "audio_interface.hpp"
#include "message_queue.hpp"
#include <atomic>
#include <mutex>
#include <vector>

namespace Granite
{
namespace Audio
{
using StreamID = uint64_t;

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

	void install_message_queue(StreamID id, Util::LockFreeMessageQueue *queue);

	virtual void setup(float mixer_output_rate, unsigned mixer_channels, size_t max_num_frames)
	{
		(void)mixer_output_rate;
		(void)mixer_channels;
		(void)max_num_frames;
	}

	// Must increment.
	virtual size_t accumulate_samples(float * const *channels, const float *gain, size_t num_frames) noexcept = 0;

	virtual unsigned get_num_channels() const = 0;
	virtual float get_sample_rate() const = 0;

	StreamID get_stream_id() const
	{
		return stream_id;
	}

protected:
	Util::LockFreeMessageQueue &get_message_queue()
	{
		return *message_queue;
	}

private:
	StreamID stream_id = StreamID(-1);
	Util::LockFreeMessageQueue *message_queue = nullptr;
};

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
	StreamID add_mixer_stream(MixerStream *stream, bool start_playing = true,
	                          float initial_gain_db = 0.0f, float initial_panning = 0.0f);
	void kill_stream(StreamID id);

	// Garbage collection. Should be called regularly from a non-critical thread.
	void dispose_dead_streams();

	// Atomically sets stream parameters, such as gain and panning.
	// Panning is -1 (left), 0 (center), 1 (right).
	void set_stream_mixer_parameters(StreamID id, float new_gain_db, float new_panning);

	// Returns latency-adjusted play cursor in seconds from add_mixer_stream.
	// The play cursor monotonically increases.
	// Returns a negative number if the stream no longer exists.
	double get_play_cursor(StreamID id);

	enum class StreamState
	{
		Playing,
		Paused,
		Dead
	};
	StreamState get_stream_state(StreamID id);

	bool pause_stream(StreamID id);
	bool play_stream(StreamID id);
	static unsigned get_stream_index(StreamID id);

	Util::LockFreeMessageQueue &get_message_queue();

private:
	enum { MaxSources = 128 };
	std::atomic<uint32_t> active_channel_mask[MaxSources / 32];
	MixerStream *mixer_streams[MaxSources] = {};

	// Actually float, bitcasted.
	std::atomic<uint32_t> panning[MaxSources];
	std::atomic<uint32_t> gain_linear[MaxSources];
	std::atomic<uint32_t> latency;
	std::atomic<bool> stream_playing[MaxSources];

	uint64_t stream_raw_play_cursors[MaxSources];
	std::atomic<uint64_t> stream_adjusted_play_cursors_usec[MaxSources];

	uint64_t stream_generation[MaxSources] = {};
	std::mutex non_critical_lock;

	size_t max_num_samples = 0;
	unsigned num_channels = 0;
	float sample_rate = 0.0f;
	double inv_sample_rate = 0.0;

	void set_backend_parameters(float sample_rate, unsigned channels, size_t max_num_sample_count) override;
	void on_backend_start() override;
	void on_backend_stop() override;
	void set_latency_usec(uint32_t usec) override;

	StreamID generate_stream_id(unsigned index);
	bool verify_stream_id(StreamID id);
	uint64_t get_stream_generation(StreamID id);

	bool is_active = false;

	void update_stream_play_cursor(unsigned index, double new_latency) noexcept;

	Util::LockFreeMessageQueue message_queue;
};

}
}