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

#include <memory>
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

	virtual void on_backend_start(float sample_rate, unsigned channels, size_t max_num_frames);
	virtual void on_backend_stop();
};

class Backend
{
public:
	enum { MaxAudioChannels = 8 };
	virtual ~Backend() = default;

	virtual const char *get_backend_name() = 0;
	virtual float get_sample_rate() = 0;
	virtual unsigned get_num_channels() = 0;

	virtual bool start(BackendCallback *callback) = 0;
	virtual bool stop() = 0;
};

std::unique_ptr<Backend> create_default_audio_backend(float target_sample_rate, unsigned target_channels);
}
}
