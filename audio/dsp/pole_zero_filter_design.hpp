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
#include <complex>

namespace Granite
{
namespace Audio
{
namespace DSP
{
class PoleZeroFilterDesigner
{
public:
	enum { MaxTaps = 64 };

	// Adds two taps to the numerator.
	void add_zero(double amplitude, double phase);
	// Adds two taps to the denominator.
	void add_pole(double amplitude, double phase);

	// Adds one tap, no need for conjugate pairs.
	void add_zero_nyquist(double amplitude);
	void add_zero_dc(double amplitude);

	// The output is in H(z) form.
	// H(z) = (num[0] + num[1] * z^-1 + num[2] * z^-2) / (den[0] + den[1] * z^-1 + den[2] + z^-2)
	const double *get_numerator() const;
	const double *get_denominator() const;
	unsigned get_numerator_count() const;
	unsigned get_denominator_count() const;

	void reset();

	std::complex<double> evaluate_response(double phase) const;

	void impulse_response(double *output, unsigned count) const;

private:
	double numerator[MaxTaps] = { 1.0f };
	double denominator[MaxTaps] = { 1.0f };
	unsigned numerator_count = 1;
	unsigned denominator_count = 1;
	void add_filter(double *coeffs, unsigned &count, double amplitude, double phase);
};
}
}
}