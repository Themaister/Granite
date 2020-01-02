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

#include "audio_resampler.hpp"
#include <string.h>
#include <assert.h>

namespace Granite
{
namespace Audio
{
ResampledStream::ResampledStream(Granite::Audio::MixerStream *source_)
	: source(source_)
{
}

ResampledStream::~ResampledStream()
{
	if (source)
		source->dispose();
}

void ResampledStream::setup(float output_rate, unsigned channels, size_t num_frames)
{
	num_channels = channels;
	max_num_frames = num_frames;
	sample_rate = output_rate;

	for (auto &re : resamplers)
		re.reset();
	for (unsigned i = 0; i < channels; i++)
		resamplers[i].reset(new DSP::SincResampler(output_rate, source->get_sample_rate(), DSP::SincResampler::Quality::Medium));

	size_t maximum_input = resamplers[0]->get_maximum_input_for_output_frames(max_num_frames);
	for (auto &buffer : input_buffer)
		buffer.clear();
	for (unsigned i = 0; i < channels; i++)
		input_buffer[i].resize(maximum_input);

	source->setup(source->get_sample_rate(), source->get_num_channels(), maximum_input);
}

size_t ResampledStream::accumulate_samples(float *const *channels, const float *gain, size_t num_frames) noexcept
{
	size_t need_samples = resamplers[0]->get_current_input_for_output_frames(num_frames);
	float *output_channels[Backend::MaxAudioChannels];
	for (unsigned c = 0; c < num_channels; c++)
	{
		output_channels[c] = input_buffer[c].data();
		memset(output_channels[c], 0, need_samples * sizeof(float));
	}

	size_t source_input = source->accumulate_samples(output_channels, gain, need_samples);

	for (unsigned c = 0; c < num_channels; c++)
	{
		size_t output = resamplers[c]->process_and_accumulate(channels[c], output_channels[c], num_frames);
		(void)output;
		assert(output == need_samples);
	}

	return source_input ? num_frames : 0;
}

}
}
