/* Copyright (c) 2015-2023 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FFT_BUTTERFLIES_H_
#define FFT_BUTTERFLIES_H_

#include "fft_common.h"

void butterfly(inout cfloat a, inout cfloat b, cfloat w)
{
	cfloat t = cmul(b, w);
	b = a - t;
	a = a + t;
}

void butterfly_p1(inout cfloat a, inout cfloat b)
{
	cfloat t = b;
	b = a - t;
	a = a + t;
}

void butterfly_p1_dir_j(inout cfloat a, inout cfloat b)
{
	cfloat t = b;
	b = cfloat(-PI_DIR_MULT, PI_DIR_MULT) * (a.yx - t.yx);
	a = a + t;
}

// FFT4
void FFT4_p1(inout cfloat a, inout cfloat b, inout cfloat c, inout cfloat d)
{
	butterfly_p1(a, b);
	butterfly_p1_dir_j(c, d);
	butterfly_p1(a, c);
	butterfly_p1(b, d);
}

void FFT4(inout cfloat a, inout cfloat b, inout cfloat c, inout cfloat d,
          uint k, uint p)
{
	cfloat w = twiddle(k, p);
	butterfly(a, b, w);
	butterfly(c, d, w);

	cfloat w0 = twiddle(k, 2u * p);
	cfloat w1 = cmul_dir_j(w0);
	butterfly(a, c, w0);
	butterfly(b, d, w1);
}

// FFT8
void FFT8_p1(inout cfloat a, inout cfloat b, inout cfloat c, inout cfloat d,
             inout cfloat e, inout cfloat f, inout cfloat g, inout cfloat h)
{
	butterfly_p1(a, b);
	butterfly_p1_dir_j(c, d);
	butterfly_p1(e, f);
	butterfly_p1_dir_j(g, h);

	butterfly_p1(a, c);
	butterfly_p1(b, d);
	butterfly_p1_dir_j(e, g);
	butterfly_p1(f, h);

	butterfly_p1(a, e);
	butterfly(b, f, TWIDDLE_1_8);
	butterfly_p1(c, g);
	butterfly(d, h, TWIDDLE_3_8);
}

void FFT8(inout cfloat a, inout cfloat b, inout cfloat c, inout cfloat d,
          inout cfloat e, inout cfloat f, inout cfloat g, inout cfloat h,
          uint k, uint p)
{
	cfloat w = twiddle(k, p);
	butterfly(a, b, w);
	butterfly(c, d, w);
	butterfly(e, f, w);
	butterfly(g, h, w);

	cfloat w0 = twiddle(k, 2u * p);
	cfloat w1 = cmul_dir_j(w0);

	butterfly(a, c, w0);
	butterfly(b, d, w1);
	butterfly(e, g, w0);
	butterfly(f, h, w1);

	cfloat W0 = twiddle(k, 4u * p);
	cfloat W1 = cmul(W0, TWIDDLE_1_8);
	cfloat W2 = cmul_dir_j(W0);
	cfloat W3 = cmul_dir_j(W1);

	butterfly(a, e, W0);
	butterfly(b, f, W1);
	butterfly(c, g, W2);
	butterfly(d, h, W3);
}

#endif