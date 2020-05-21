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

#include "audio_mixer.hpp"
#include "event.hpp"
#include "message_queue.hpp"
#include <string.h>

namespace Granite
{
namespace Audio
{
class MixerStartEvent : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(MixerStartEvent)

	explicit MixerStartEvent(Mixer &mixer_)
		: mixer(mixer_)
	{
	}

	Mixer &get_mixer() const
	{
		return mixer;
	}

private:
	Mixer &mixer;
};

class StreamStoppedEvent : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(StreamStoppedEvent)
	explicit StreamStoppedEvent(unsigned index_)
		: Event(get_type_id()), index(index_)
	{
	}

	unsigned get_index() const
	{
		return index;
	}

private:
	unsigned index;
};

class AudioStreamPerformanceEvent : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(AudioStreamPerformanceEvent)
	explicit AudioStreamPerformanceEvent(StreamID id_, double time_, unsigned samples_)
		: Event(get_type_id()), id(id_), time(time_), samples(samples_)
	{
	}

	StreamID get_stream_id() const
	{
		return id;
	}

	double get_time() const
	{
		return time;
	}

	unsigned get_sample_count() const
	{
		return samples;
	}

private:
	StreamID id;
	double time;
	unsigned samples;
};

class AudioMonitorSamplesEvent : public Event
{
public:
	GRANITE_EVENT_TYPE_DECL(AudioMonitorSamplesEvent)
	explicit AudioMonitorSamplesEvent(unsigned channel_, const float *data, unsigned count)
		: Event(get_type_id()), channel(channel_), payload_count(count)
	{
		// Must have been allocated with padding.
		payload = reinterpret_cast<float *>(this + 1);
		memcpy(payload, data, payload_count * sizeof(float));
	}

	unsigned get_channel_index() const
	{
		return channel;
	}

	const float *get_payload() const
	{
		return payload;
	}

	unsigned get_sample_count() const
	{
		return payload_count;
	}

private:
	unsigned channel;
	unsigned payload_count;
	float *payload = nullptr;
};

template <typename T, typename... Ts>
bool emplace_padded_audio_event_on_queue(Util::LockFreeMessageQueue &queue, size_t padding, Ts&&... ts) noexcept
{
	// Event has a virtual destructor so this will not work.
	// Will probably just have to make this an informal requirement.
	//static_assert(std::is_trivially_destructible<T>::value,
	//              "Event placed on queue must be trivially destructible.");
	static_assert(std::is_base_of<Event, T>::value,
	              "Can only push types which inherit from Granite::Event.");

	auto payload = queue.allocate_write_payload(sizeof(T) + padding);
	if (!payload)
		return false;

	// Construct in-place.
	// We need a pointer to the base event.
	Event *e = new (payload.get_payload_data()) T(std::forward<Ts>(ts)...);
	assert(e->get_type_id() != 0);
	payload.set_payload_handle(e);
	return queue.push_written_payload(std::move(payload));
}

template <typename T, typename... Ts>
bool emplace_audio_event_on_queue(Util::LockFreeMessageQueue &queue, Ts&&... ts) noexcept
{
	return emplace_padded_audio_event_on_queue<T>(queue, 0, std::forward<Ts>(ts)...);
}

}
}
