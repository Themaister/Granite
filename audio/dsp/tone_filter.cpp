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
#include "dsp.hpp"
#include <complex>
#include <cmath>

namespace Granite
{
namespace Audio
{
namespace DSP
{
enum { ToneCount = 48 };

static const float TwoPI = 2.0f * 3.141592653589793f;

struct ToneFilter::Impl : Util::AlignedAllocation<ToneFilter::Impl>
{
	alignas(64) std::complex<float> tone_oscillator[ToneCount] = {};
	alignas(64) std::complex<float> tone_speed[ToneCount] = {};
	alignas(64) std::complex<float> last_value[ToneCount] = {};
	alignas(64) float last_power[ToneCount] = {};

	void filter(float *out_samples, const float *in_samples, unsigned count);
};

void ToneFilter::init(float sample_rate, float tuning_freq)
{
	for (int i = 0; i < ToneCount; i++)
	{
		float freq = tuning_freq * std::exp2(float(i + 0) / 12.0f);
		float c = std::cos(freq * TwoPI / sample_rate);
		float s = std::sin(freq * TwoPI / sample_rate);
		impl->tone_speed[i] = std::complex<float>(c, s);
		impl->tone_oscillator[i] = 1.0f;
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

static std::complex<float> normalize(std::complex<float> v)
{
	float l2 = v.real() * v.real() + v.imag() * v.imag();
	l2 = 1.5f - 0.5f * l2;
	return v * l2;
}

static float distort(float v)
{
	// TODO: Find something faster.
	return std::atan(v);
}

void ToneFilter::Impl::filter(float *out_samples, const float *in_samples, unsigned count)
{
	for (unsigned samp = 0; samp < count; samp++)
	{
		float ret = 0.0f;
		float in_sample = in_samples[samp];

		for (int i = 0; i < ToneCount; i++)
		{
			tone_oscillator[i] *= tone_speed[i];
			auto v = in_sample * std::conj(tone_oscillator[i]);

			v = 0.005f * v + 0.995f * last_value[i];
			last_value[i] = v;

			float power = v.real() * v.real() + v.imag() * v.imag();
			power = 0.001f * power + 0.999f * last_power[i];
			last_power[i] = power;

			float modifier = std::sqrt(power);
			float output = tone_oscillator[i].real();
			ret += 1.0f * distort(output * 5.0f) * modifier;
		}

		out_samples[samp] = ret;
	}

	for (auto &o : tone_oscillator)
		o = normalize(o);
}

void ToneFilter::filter(float *out_samples, const float *in_samples, unsigned count)
{
	impl->filter(out_samples, in_samples, count);
}
}
}
}
