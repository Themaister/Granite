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

#include <vector>
#include <memory>
#include "audio_mixer.hpp"

#define TONE_DEBUG
#include <string.h>
#include "event.hpp"

namespace Util
{
class LockFreeMessageQueue;
}

namespace Granite
{
namespace Audio
{
namespace DSP
{
class ToneFilter
{
public:
	enum { ToneCount = 48, FilterTaps = 2 };

	ToneFilter();
	~ToneFilter();

	void init(float sample_rate, float tuning_freq = 440.0f);
	void filter(float *out_samples, const float *in_samples, unsigned count);

#ifdef TONE_DEBUG
	void flush_debug_info(Util::LockFreeMessageQueue &queue, StreamID id);
#endif

	ToneFilter(const ToneFilter &) = delete;
	void operator=(const ToneFilter &) = delete;

private:
	struct Impl;
	Impl *impl;
};

class ToneFilterWave : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(ToneFilterWave)
	ToneFilterWave(StreamID id, unsigned index_, float power_ratio_, const float *data, unsigned count_)
		: Event(get_type_id()), stream_id(id), power_ratio(power_ratio_), index(index_), count(count_)
	{
		// Allocation must be padded.
		payload = reinterpret_cast<float *>(this + 1);
		memcpy(payload, data, count * sizeof(float));
	}

	StreamID get_stream_id() const
	{
		return stream_id;
	}

	unsigned get_tone_index() const
	{
		return index;
	}

	float get_power_ratio() const
	{
		return power_ratio;
	}

	const float *get_payload() const
	{
		return payload;
	}

	unsigned get_sample_count() const
	{
		return count;
	}

private:
	StreamID stream_id;
	float power_ratio;
	unsigned index;
	float *payload = nullptr;
	unsigned count = 0;
};
}
}
}
