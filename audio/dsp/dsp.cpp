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

#include "dsp.hpp"
#include "fft.h"
#include <assert.h>
#include <math.h>
#include <complex>
#include "muglm/muglm_impl.hpp"

namespace Granite
{
namespace Audio
{
namespace DSP
{
float gain_to_db(float gain)
{
	return 20.0f * log10f(gain);
}

float db_to_gain(float db)
{
	return powf(10.0f, db / 20.0f);
}

static float interpolate_gain(float freq,
                              const EqualizerParameter *parameters,
                              unsigned num_parameters)
{
	if (num_parameters < 1)
		return 1.0f;
	if (freq == 0.0f)
		return DSP::db_to_gain(parameters[0].gain_db);

	for (unsigned i = 0; i + 1 < num_parameters; i++)
	{
		if (freq >= parameters[i].freq && freq <= parameters[i + 1].freq)
		{
			float lo_freq_log = log2f(parameters[i].freq);
			float hi_freq_log = log2f(parameters[i + 1].freq);
			float freq_log = log2f(freq);
			float freq_delta = hi_freq_log - lo_freq_log;
			assert(freq_delta > 0.0f);

			float l = (freq_log - lo_freq_log) / freq_delta;
			float gain_db = muglm::mix(parameters[i].gain_db, parameters[i + 1].gain_db, l);
			return DSP::db_to_gain(gain_db);
		}
	}

	return 1.0f;
}

void create_parametric_eq_filter(float *coeffs, unsigned num_coeffs,
                                 float sample_rate,
                                 const EqualizerParameter *parameters,
                                 unsigned num_parameters)
{
	assert((num_coeffs & (num_coeffs - 1)) == 0);

	auto *fft = mufft_create_plan_1d_c2r(num_coeffs, MUFFT_FLAG_CPU_ANY);
	auto *freq = static_cast<std::complex<float> *>(mufft_calloc(num_coeffs * sizeof(std::complex<float>)));
	auto *fft_output = static_cast<float *>(mufft_calloc(num_coeffs * sizeof(float)));

	unsigned nyquist_index = num_coeffs / 2;
	float normalization_gain = 1.0f / float(num_coeffs);
	for (unsigned i = 0; i <= nyquist_index; i++)
	{
		freq[i] = normalization_gain *
		          interpolate_gain(float(i) * sample_rate / float(num_coeffs),
		                           parameters,
		                           num_parameters);
	}

	mufft_execute_plan_1d(fft, fft_output, freq);

	// Add the expected delay we need to get a causal filter.
	for (unsigned i = 0; i < num_coeffs; i++)
		coeffs[i] = fft_output[(i + nyquist_index) & (num_coeffs - 1)];

	// Apply a kaiser window on the filter to get a smoother frequency response.
	double window_mod = 1.0 / kaiser_window_function(0.0, 4.0);
	for (unsigned i = 0; i < num_coeffs; i++)
	{
		double index = double(int(i) - int(nyquist_index)) / double(nyquist_index);
		coeffs[i] *= kaiser_window_function(index, 4.0) * window_mod;
	}

	mufft_free_plan_1d(fft);
	mufft_free(freq);
	mufft_free(fft_output);
}

/* Modified Bessel function of first order.
 * Check Wiki for mathematical definition ... */
static double besseli0(double x)
{
	double sum = 0.0;
	double factorial = 1.0;
	double factorial_mult = 0.0;
	double x_pow = 1.0;
	double two_div_pow = 1.0;
	double x_sqr = x * x;

	/* Approximate. This is an infinite sum.
	 * Luckily, it converges rather fast. */
	for (unsigned i = 0; i < 18; i++)
	{
		sum += x_pow * two_div_pow / (factorial * factorial);

		factorial_mult += 1.0;
		x_pow *= x_sqr;
		two_div_pow *= 0.25;
		factorial *= factorial_mult;
	}

	return sum;
}

double sinc(double val)
{
	if (fabs(val) < 0.00001)
		return 1.0;
	return sin(val) / val;
}

double kaiser_window_function(double index, double beta)
{
	return besseli0(beta * sqrt(1.0 - index * index));
}
}
}
}