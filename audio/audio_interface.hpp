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

#pragma once

#include <memory>
#include <string>
#include <stddef.h>
#include <stdint.h>
#include "global_managers.hpp"

namespace Granite
{
namespace Audio
{
class BackendCallback
{
public:
	virtual ~BackendCallback() = default;
	virtual void mix_samples(float * const *channels, size_t num_frames) noexcept = 0;

	virtual void set_backend_parameters(float sample_rate, unsigned channels, size_t max_num_frames) = 0;
	virtual void on_backend_stop() = 0;
	virtual void on_backend_start() = 0;
	virtual void set_latency_usec(uint32_t usec) = 0;
};

class Backend : public BackendInterface
{
public:
	explicit Backend(BackendCallback *callback);

	enum { MaxAudioChannels = 8 };

	virtual const char *get_backend_name() = 0;
	virtual float get_sample_rate() = 0;
	virtual unsigned get_num_channels() = 0;

	inline BackendCallback *get_callback()
	{
		return callback;
	}

	// Blocking interface. Used when callback is nullptr.
	virtual bool get_buffer_status(size_t &write_avail, size_t &max_write_avail, uint32_t &latency_usec);
	virtual size_t write_frames_interleaved(const float *data, size_t frames, bool blocking);

	// Call periodically, used for automatic recovery for backends which need it.
	virtual void heartbeat();

protected:
	BackendCallback *callback = nullptr;
};

class RecordCallback
{
public:
	virtual void write_frames_interleaved_f32(const float *data, size_t frames) = 0;
};

// Simple blocking recorder interface.
// Used together with FFmpeg recording.
class RecordStream
{
public:
	virtual ~RecordStream() = default;

	virtual const char *get_backend_name() = 0;
	virtual float get_sample_rate() = 0;
	virtual unsigned get_num_channels() = 0;

	virtual size_t read_frames_deinterleaved_f32(float * const *data, size_t frames, bool blocking) = 0;
	virtual size_t read_frames_interleaved_f32(float *data, size_t frames, bool blocking) = 0;
	virtual bool get_buffer_status(size_t &read_avail, uint32_t &latency_usec) = 0;
	virtual bool start() = 0;
	virtual bool stop() = 0;

	virtual void set_record_callback(RecordCallback *callback) = 0;
};

Backend *create_default_audio_backend(BackendCallback *callback, float target_sample_rate, unsigned target_channels);
RecordStream *create_default_audio_record_backend(const char *ident, float target_sample_rate, unsigned target_channels);

class DumpBackend : public Backend
{
public:
	DumpBackend(BackendCallback *callback_, float target_sample_rate,
	            unsigned target_channels, unsigned frames_per_tick);
	~DumpBackend();

	void drain_interleaved_s16(int16_t *data, size_t frames);
	unsigned get_frames_per_tick() const;

	const char *get_backend_name() override;
	float get_sample_rate() override;
	unsigned get_num_channels() override;
	bool start() override;
	bool stop() override;

	bool get_buffer_status(size_t &write_avail, size_t &write_avail_frames, uint32_t &latency_usec) override;
	size_t write_frames_interleaved(const float *data, size_t frames, bool blocking) override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
}
