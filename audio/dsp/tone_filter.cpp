/* Copyright (c) 2017-2019 Hans-Kristian Arntzen
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

#include "tone_filter.hpp"
#include "aligned_alloc.hpp"
#include "pole_zero_filter_design.hpp"
#include "dsp.hpp"
#include "util.hpp"
#include <complex>
#include <cmath>
#include <assert.h>

#ifdef TONE_DEBUG
#include "audio_events.hpp"
#endif

namespace Granite
{
namespace Audio
{
namespace DSP
{
static const double TwoPI = 2.0 * 3.141592653589793;

struct ToneFilter::Impl : Util::AlignedAllocation<ToneFilter::Impl>
{
	alignas(64) float fir_history[FilterTaps][ToneCount] = {};
	alignas(64) float iir_history[FilterTaps][ToneCount] = {};
	alignas(64) float fir_coeff[FilterTaps + 1][ToneCount] = {};
	alignas(64) float iir_coeff[FilterTaps][ToneCount] = {};
	alignas(64) float running_power[ToneCount] = {};
	alignas(64) float running_total_power = {};
	unsigned index = 0;

	unsigned iir_filter_taps = 0;
	unsigned fir_filter_taps = 0;
	float tone_power_lerp = 0.002f;
	float total_tone_power_lerp = 0.0005f;
	float final_history = 0.0f;

	void filter(float *out_samples, const float *in_samples, unsigned count);

#ifdef TONE_DEBUG
	std::vector<float> tone_buffers[ToneCount];
#endif
};

#ifdef TONE_DEBUG
void ToneFilter::flush_debug_info(Util::LockFreeMessageQueue &queue, StreamID id)
{
	for (int i = 0; i < ToneCount; i++)
	{
		emplace_padded_audio_event_on_queue<ToneFilterWave>(queue,
		                                                    impl->tone_buffers[i].size() * sizeof(float),
		                                                    id, i,
		                                                    impl->running_power[i] / (impl->running_total_power + 0.000001f),
		                                                    impl->tone_buffers[i].data(),
		                                                    impl->tone_buffers[i].size());
		impl->tone_buffers[i].clear();
	}
}
#endif

void ToneFilter::init(float sample_rate, float tuning_freq)
{
	PoleZeroFilterDesigner designer;
	for (int i = 0; i < ToneCount; i++)
	{
		designer.reset();

		double freq = tuning_freq * std::exp2(double(i - 12) / 12.0);
		double angular_freq = freq * TwoPI / sample_rate;

		// Ad-hoc sloppy IIR filter design, wooo.

		// Add some zeroes to balance out the filter.
		designer.add_zero_dc(1.0);
		designer.add_zero_nyquist(1.0);

		// We're going to create a resonator around the desired tone we're looking for.
		designer.add_pole(0.9999, angular_freq);

		// Look ma', a biquad!

		impl->fir_filter_taps = designer.get_numerator_count() - 1;
		impl->iir_filter_taps = designer.get_denominator_count() - 1;

		assert(impl->fir_filter_taps <= FilterTaps);
		assert(impl->iir_filter_taps <= FilterTaps);

		// Normalize the FIR part.
		double inv_response = 1.0 / std::abs(designer.evaluate_response(angular_freq));
		for (unsigned coeff = 0; coeff < impl->fir_filter_taps + 1; coeff++)
			impl->fir_coeff[coeff][i] = float(designer.get_numerator()[coeff] * inv_response);

		// IIR part. To apply the filter, we need to negate the Z-form coeffs.
		for (unsigned coeff = 0; coeff < impl->iir_filter_taps; coeff++)
			impl->iir_coeff[coeff][i] = float(-designer.get_denominator()[coeff + 1]);

#ifdef TONE_DEBUG
		impl->tone_buffers[i].reserve(1024);
#endif
	}
}

ToneFilter::ToneFilter()
{
	impl = new Impl;
}

ToneFilter::~ToneFilter()
{
	delete impl;
}

static float distort(float v)
{
	float abs_v = std::abs(v);
	return std::copysign(1.0f, v) * (1.0f - std::exp(-abs_v));
}

void ToneFilter::Impl::filter(float *out_samples, const float *in_samples, unsigned count)
{
	for (unsigned samp = 0; samp < count; samp++)
	{
		float final_sample = 0.0f;
		float in_sample = in_samples[samp];
		running_total_power = running_total_power * (1.0f - total_tone_power_lerp) +
		                      total_tone_power_lerp * in_sample * in_sample;
		float low_threshold = 0.01f * running_total_power;
		float high_threshold = 0.10f * running_total_power;

		float low_threshold_divider = 1.0f / (low_threshold * low_threshold * low_threshold + 0.00000001f);

		for (int tone = 0; tone < ToneCount; tone++)
		{
			float ret = fir_coeff[0][tone] * in_sample;
			for (unsigned x = 0; x < fir_filter_taps; x++)
				ret += fir_coeff[x + 1][tone] * fir_history[(index + x) & (FilterTaps - 1)][tone];
			for (unsigned x = 0; x < iir_filter_taps; x++)
				ret += iir_coeff[x][tone] * iir_history[(index + x) & (FilterTaps - 1)][tone];

			fir_history[(index - 1) & (FilterTaps - 1)][tone] = in_sample;
			iir_history[(index - 1) & (FilterTaps - 1)][tone] = ret;

			float new_power = ret * ret;

			if (new_power < low_threshold)
				new_power = new_power * new_power * new_power * new_power * low_threshold_divider;
			if (new_power > high_threshold)
				new_power = high_threshold;

			new_power = (1.0f - tone_power_lerp) * running_power[tone] + tone_power_lerp * new_power;
			running_power[tone] = new_power;

			float rms = std::sqrt(new_power);
			float final = rms * distort(ret * 40.0f / (rms + 0.001f));
			final_sample += final;

#ifdef TONE_DEBUG
			tone_buffers[tone].push_back(final);
#endif
		}

		// Trivial 1-pole IIR filter to serve as a slight low-pass to dampen the worst high-end.
		final_sample = 0.5f * final_history + 0.5f * final_sample;
		final_history = final_sample;

#if 0
		static float max_final = 0.0f;
		if (std::abs(final_sample) > max_final)
		{
			max_final = std::abs(final_sample);
			LOGI("New max final sample: %f.\n", max_final);
		}
#endif

		out_samples[samp] = distort(2.0f * final_sample);
		index = (index - 1) & (FilterTaps - 1);
	}
}

void ToneFilter::filter(float *out_samples, const float *in_samples, unsigned count)
{
	impl->filter(out_samples, in_samples, count);
}
}
}
}
