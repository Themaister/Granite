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

#include "pole_zero_filter_design.hpp"
#include <cmath>
#include <complex>
#include <assert.h>
#include <algorithm>

namespace Granite
{
namespace Audio
{
namespace DSP
{
const double *PoleZeroFilterDesigner::get_numerator() const
{
	return numerator;
}

const double *PoleZeroFilterDesigner::get_denominator() const
{
	return denominator;
}

unsigned PoleZeroFilterDesigner::get_numerator_count() const
{
	return numerator_count;
}

unsigned PoleZeroFilterDesigner::get_denominator_count() const
{
	return denominator_count;
}

static std::complex<double> rotor(double phase)
{
	return { std::cos(phase), std::sin(phase) };
}

void PoleZeroFilterDesigner::reset()
{
	numerator[0] = 1.0f;
	denominator[0] = 1.0f;
	numerator_count = 1;
	denominator_count = 1;
}

std::complex<double> PoleZeroFilterDesigner::evaluate_response(double phase) const
{
	std::complex<double> num = 0.0f;
	std::complex<double> den = 0.0f;

	for (unsigned i = 0; i < numerator_count; i++)
		num += numerator[i] * rotor(-phase * double(i));
	for (unsigned i = 0; i < denominator_count; i++)
		den += denominator[i] * rotor(-phase * double(i));
	return num / den;
}

void PoleZeroFilterDesigner::impulse_response(double *output, unsigned count) const
{
	double fir_history[MaxTaps] = {};
	double iir_history[MaxTaps] = {};
	unsigned index = 0;

	for (unsigned i = 0; i < count; i++)
	{
		double in_sample = i == 0 ? 1.0 : 0.0;
		double ret = numerator[0] * in_sample;
		for (unsigned x = 0; x < numerator_count - 1; x++)
			ret += numerator[x + 1] * fir_history[(index + x) & (MaxTaps - 1)];
		for (unsigned x = 0; x < denominator_count - 1; x++)
			ret -= denominator[x + 1] * iir_history[(index + x) & (MaxTaps - 1)];

		fir_history[(index - 1) & (MaxTaps - 1)] = in_sample;
		iir_history[(index - 1) & (MaxTaps - 1)] = ret;
		output[i] = ret;

		index = (index - 1) & (MaxTaps - 1);
	}
}

static void design_dual_tap(double (&coeffs)[3], double amplitude, double phase)
{
	// conv([1, -a * exp(j * phase)], [1, -a * exp(j * -phase)])
	coeffs[0] = 1.0;
	coeffs[1] = -2.0 * std::cos(phase) * amplitude;
	coeffs[2] = amplitude * amplitude;
}

static void add_convolve(double *coeffs, unsigned &count, const double *new_coeffs, unsigned new_count)
{
	double tmp_coeffs[PoleZeroFilterDesigner::MaxTaps];
	std::copy(coeffs, coeffs + count, tmp_coeffs);

	int output_count = int(count) + int(new_count) - 1;
	for (int x = 0; x < output_count; x++)
	{
		double result = 0.0f;
		int max_t = std::min(int(new_count) - 1, x);
		int min_t = std::max(0, x - int(count) + 1);
		for (int t = min_t; t <= max_t; t++)
			result += new_coeffs[t] * tmp_coeffs[x - t];

		coeffs[x] = result;
	}

	count += new_count - 1;
}

void PoleZeroFilterDesigner::add_filter(double *coeffs, unsigned &count, double amplitude, double phase)
{
	assert(count + 2 < MaxTaps);
	double tap_coeffs[3];
	design_dual_tap(tap_coeffs, amplitude, phase);
	add_convolve(coeffs, count, tap_coeffs, 3);
}

void PoleZeroFilterDesigner::add_pole(double amplitude, double phase)
{
	add_filter(denominator, denominator_count, amplitude, phase);
}

void PoleZeroFilterDesigner::add_zero(double amplitude, double phase)
{
	add_filter(numerator, numerator_count, amplitude, phase);
}

void PoleZeroFilterDesigner::add_zero_dc(double amplitude)
{
	double tap_coeffs[2] = { 1.0, -amplitude };
	add_convolve(numerator, numerator_count, tap_coeffs, 2);
}

void PoleZeroFilterDesigner::add_zero_nyquist(double amplitude)
{
	double tap_coeffs[2] = { 1.0, amplitude };
	add_convolve(numerator, numerator_count, tap_coeffs, 2);
}
}
}
}