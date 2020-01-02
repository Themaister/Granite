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
#include "dsp/sinc_resampler.hpp"
#include <memory>

namespace Granite
{
namespace Audio
{
class ResampledStream : public MixerStream
{
public:
	ResampledStream(MixerStream *source);
	~ResampledStream();

	void setup(float output_rate, unsigned channels, size_t frames) override;
	size_t accumulate_samples(float * const *channels, const float *gain, size_t num_frames) noexcept override;

	float get_sample_rate() const override
	{
		return sample_rate;
	}

	unsigned get_num_channels() const override
	{
		return num_channels;
	}

private:
	MixerStream *source;
	float sample_rate = 0.0f;
	unsigned num_channels = 0;
	size_t max_num_frames = 0;

	std::vector<float> input_buffer[Backend::MaxAudioChannels];
	std::unique_ptr<DSP::SincResampler> resamplers[Backend::MaxAudioChannels];
};
}
}
