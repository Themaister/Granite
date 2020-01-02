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

#include "tone_filter_stream.hpp"
#include "tone_filter.hpp"
#include "dsp.hpp"
#include <string.h>

namespace Granite
{
namespace Audio
{
namespace DSP
{
struct ToneFilterStream : MixerStream
{
	~ToneFilterStream() override
	{
		if (source)
			source->dispose();
	}

	bool init(MixerStream *source_, float tuning_freq_)
	{
		source = source_;
		tuning_freq = tuning_freq_;
		return true;
	}

	void setup(float mixer_output_rate, unsigned mixer_channels, size_t max_frames) override
	{
		source->setup(mixer_output_rate, mixer_channels, max_frames);
		filter.init(source->get_sample_rate(), tuning_freq);
		num_channels = get_num_channels();

		for (unsigned c = 0; c < mixer_channels; c++)
		{
			mix_channels[c].resize(max_frames);
			mix_ptrs[c] = mix_channels[c].data();
		}
		mix_channels_mono.resize(max_frames);
	}

	// Must increment.
	size_t accumulate_samples(float *const *channels, const float *gain, size_t num_frames) noexcept override
	{
		float gains[Backend::MaxAudioChannels];
		for (auto &g : gains)
			g = 1.0f;

		auto channel_count = get_num_channels();
		for (unsigned c = 0; c < channel_count; c++)
			memset(mix_ptrs[c], 0, num_frames * sizeof(float));

		auto ret = source->accumulate_samples(mix_ptrs, gains, num_frames);
		for (unsigned c = 0; c < channel_count; c++)
			convert_to_mono(mix_channels_mono.data(), mix_ptrs, source->get_num_channels(), ret);

		filter.filter(mix_channels_mono.data(), mix_channels_mono.data(), ret);

		for (unsigned c = 0; c < channel_count; c++)
		{
			accumulate_channel(channels[c], mix_ptrs[c], gain[c] * 0.5f, ret);
			accumulate_channel(channels[c], mix_channels_mono.data(), gain[c] * 0.5f, ret);
		}

#ifdef TONE_DEBUG
		filter.flush_debug_info(get_message_queue(), get_stream_id());
#endif

		return ret;
	}

	unsigned get_num_channels() const override
	{
		return source->get_num_channels();
	}

	float get_sample_rate() const override
	{
		return source->get_sample_rate();
	}

	MixerStream *source = nullptr;
	ToneFilter filter;
	std::vector<float> mix_channels[Backend::MaxAudioChannels];
	std::vector<float> mix_channels_mono;
	float tuning_freq = 0.0f;
	float *mix_ptrs[Backend::MaxAudioChannels] = {};
	unsigned num_channels = 0;
};

MixerStream *create_tone_filter_stream(MixerStream *source, float tuning_rate)
{
	if (!source)
		return nullptr;

	auto *filt = new ToneFilterStream;
	if (!filt->init(source, tuning_rate))
	{
		delete filt;
		return nullptr;
	}
	else
		return filt;
}
}
}
}

