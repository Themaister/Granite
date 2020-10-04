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

#include "audio_fft_eq.hpp"
#include "dsp/dsp.hpp"
#include "fft.h"
#include "logging.hpp"
#include "bitops.hpp"
#include <vector>
#include <complex>
#include <string.h>
#include <algorithm>

namespace Granite
{
namespace Audio
{
namespace DSP
{
// Need to make sure we get aligned data for muFFT, so have to use raw alloc/free.
class FFTEq : public MixerStream
{
public:
	~FFTEq() override
	{
		if (source)
			source->dispose();

		mufft_free_plan_conv(fft_conv);
		mufft_free(data_fft);
		mufft_free(filter_fft);

		for (auto &iter : mix_buffers)
			for (auto &m : iter)
				mufft_free(m);

		for (auto &iter : mix_buffers_conv)
			for (auto &m : iter)
				mufft_free(m);
	}

	std::complex<float> *allocate_complex(size_t count)
	{
		return static_cast<std::complex<float> *>(mufft_calloc(count * sizeof(std::complex<float>)));
	}

	float *allocate_float(size_t count)
	{
		return static_cast<float *>(mufft_calloc(count * sizeof(float)));
	}

	bool init(MixerStream *source_, const float *filter_coeffs, unsigned coeff_count)
	{
		source = source_;
		block_size = std::max(16u, Util::next_pow2(coeff_count));
		fft_block_size = block_size * 2;

		fft_conv = mufft_create_plan_conv(fft_block_size, MUFFT_FLAG_CPU_ANY,
		                                  MUFFT_CONV_METHOD_FLAG_MONO_MONO |
		                                  MUFFT_CONV_METHOD_FLAG_ZERO_PAD_UPPER_HALF_FIRST |
		                                  MUFFT_CONV_METHOD_FLAG_ZERO_PAD_UPPER_HALF_SECOND);
		if (!fft_conv)
			return false;

		auto *tmp = allocate_float(block_size);
		filter_fft = allocate_complex(fft_block_size);
		data_fft = allocate_complex(fft_block_size);

		memcpy(tmp, filter_coeffs, coeff_count * sizeof(float));
		mufft_execute_conv_input(fft_conv, MUFFT_CONV_BLOCK_SECOND, filter_fft, tmp);
		mufft_free(tmp);

		return true;
	}

	void setup(float mixer_output_rate, unsigned mixer_channels, size_t) override
	{
		source->setup(mixer_output_rate, mixer_channels, block_size);
		num_channels = source->get_num_channels();
		sample_rate = source->get_sample_rate();

		for (auto &iter : mix_buffers)
			for (unsigned c = 0; c < mixer_channels; c++)
				iter[c] = allocate_float(block_size);

		for (auto &iter : mix_buffers_conv)
			for (unsigned c = 0; c < mixer_channels; c++)
				iter[c] = allocate_float(fft_block_size);

		current_read = block_size;
	}

	// Must increment.
	size_t accumulate_samples(float *const *channels, const float *gain, size_t num_frames) noexcept override
	{
		size_t ret = 0;

		float gains[Backend::MaxAudioChannels];
		for (auto &g : gains)
			g = 1.0f;

		float *channels_copy[Backend::MaxAudioChannels];
		for (unsigned c = 0; c < num_channels; c++)
			channels_copy[c] = channels[c];

		while (num_frames)
		{
			size_t available_in_mix_buffer = block_size - current_read;
			if (available_in_mix_buffer)
			{
				size_t to_read = std::min(num_frames, available_in_mix_buffer);
				for (unsigned c = 0; c < num_channels; c++)
				{
					DSP::accumulate_channel(channels_copy[c], mix_buffers_conv[mix_iteration][c] + current_read,
					                        gain[c], to_read);
					channels_copy[c] += to_read;
				}

				num_frames -= to_read;
				current_read += to_read;
				ret += to_read;
			}
			else
			{
				if (is_stopping)
					break;

				mix_iteration = 1 - mix_iteration;

				// Feed the FFT with more data.
				for (unsigned c = 0; c < num_channels; c++)
					memset(mix_buffers[mix_iteration][c], 0, block_size * sizeof(float));

				// We might have some samples left in the overlap buffer.
				// Next time around, we'll stop the audio rendering.
				if (!source->accumulate_samples(mix_buffers[mix_iteration], gains, block_size))
					is_stopping = true;

				current_read = 0;

				for (unsigned c = 0; c < num_channels; c++)
				{
					mufft_execute_conv_input(fft_conv,
					                         MUFFT_CONV_BLOCK_FIRST,
					                         data_fft,
					                         mix_buffers[mix_iteration][c]);

					mufft_execute_conv_output(fft_conv, mix_buffers_conv[mix_iteration][c],
					                          data_fft, filter_fft);

					// Overlapped FFT convolution, add in the results from previous block tail.
					DSP::accumulate_channel_nogain(mix_buffers_conv[mix_iteration][c],
					                               mix_buffers_conv[1 - mix_iteration][c] + block_size,
					                               block_size);
				}
			}
		}

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

private:
	MixerStream *source = nullptr;
	size_t block_size = 0;
	size_t fft_block_size = 0;
	unsigned num_channels = 0;
	float sample_rate = 0.0f;

	mufft_plan_conv *fft_conv = nullptr;

	std::complex<float> *filter_fft = nullptr;
	std::complex<float> *data_fft = nullptr;
	size_t current_read = 0;

	float *mix_buffers[2][Backend::MaxAudioChannels] = {};
	float *mix_buffers_conv[2][Backend::MaxAudioChannels] = {};
	unsigned mix_iteration = 0;
	bool is_stopping = false;
};

MixerStream *create_fft_eq_stream(MixerStream *source,
                                  const float *filter_coeffs, unsigned coeff_count)
{
	if (!source)
		return nullptr;

	auto *fft = new FFTEq;
	if (!fft->init(source, filter_coeffs, coeff_count))
	{
		delete fft;
		return nullptr;
	}
	else
		return fft;
}
}
}
}
