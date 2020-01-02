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

#include <memory>
#include <string>
#include <stddef.h>

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

class Backend
{
public:
	Backend(BackendCallback &callback);

	enum { MaxAudioChannels = 8 };
	virtual ~Backend() = default;

	virtual const char *get_backend_name() = 0;
	virtual float get_sample_rate() = 0;
	virtual unsigned get_num_channels() = 0;

	inline BackendCallback &get_callback()
	{
		return callback;
	}

	virtual bool start() = 0;
	virtual bool stop() = 0;

	// Call periodically, used for automatic recovery for backends which need it.
	virtual void heartbeat();

protected:
	BackendCallback &callback;
};

Backend *create_default_audio_backend(BackendCallback &callback, float target_sample_rate, unsigned target_channels);

class DumpBackend : public Backend
{
public:
	DumpBackend(BackendCallback &callback_, const std::string &path,
	            float target_sample_rate, unsigned target_channels,
	            unsigned frames_per_tick, unsigned frames);
	~DumpBackend();
	void frame();

	const char *get_backend_name() override;
	float get_sample_rate() override;
	unsigned get_num_channels() override;
	bool start() override;
	bool stop() override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
}
}
