/* Copyright (C) 2015 Hans-Kristian Arntzen <maister@archlinux.us>
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

// P is the current accumulated radix factor.
// First pass in an FFT, P == 1, then P will be pass0.radix, then pass0.radix * pass1.radix, and so on ...
// Used to compute twiddle factors.

#ifndef FFT_P1
#define uP constant_data.p_stride_padding.x
#endif

#if FFT_RADIX == 4
// FFT4 implementation.
void FFT4_horiz()
{
#ifdef FFT_P1
    FFT4_p1_horiz(gl_GlobalInvocationID.xy);
#else
    FFT4_horiz(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT4_vert()
{
#ifdef FFT_P1
    FFT4_p1_vert(gl_GlobalInvocationID.xy);
#else
    FFT4_vert(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT4()
{
#ifdef FFT_HORIZ
    FFT4_horiz();
#else
    FFT4_vert();
#endif
}
#endif

#if FFT_RADIX == 8
// FFT8 implementation.
void FFT8_horiz()
{
#ifdef FFT_P1
    FFT8_p1_horiz(gl_GlobalInvocationID.xy);
#else
    FFT8_horiz(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT8_vert()
{
#ifdef FFT_P1
    FFT8_p1_vert(gl_GlobalInvocationID.xy);
#else
    FFT8_vert(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT8()
{
#ifdef FFT_HORIZ
    FFT8_horiz();
#else
    FFT8_vert();
#endif
}
#endif

#if FFT_RADIX == 16
void FFT16_horiz()
{
#ifdef FFT_P1
    FFT16_p1_horiz(gl_GlobalInvocationID.xy);
#else
    FFT16_horiz(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT16_vert()
{
#ifdef FFT_P1
    FFT16_p1_vert(gl_GlobalInvocationID.xy);
#else
    FFT16_vert(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT16()
{
#ifdef FFT_HORIZ
    FFT16_horiz();
#else
    FFT16_vert();
#endif
}
#endif

#if FFT_RADIX == 64
void FFT64_horiz()
{
#ifdef FFT_P1
    FFT64_p1_horiz(gl_GlobalInvocationID.xy);
#else
    FFT64_horiz(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT64_vert()
{
#ifdef FFT_P1
    FFT64_p1_vert(gl_GlobalInvocationID.xy);
#else
    FFT64_vert(gl_GlobalInvocationID.xy, uP);
#endif
}

void FFT64()
{
#ifdef FFT_HORIZ
    FFT64_horiz();
#else
    FFT64_vert();
#endif
}
#endif

void main()
{
#if defined(FFT_RESOLVE_REAL_TO_COMPLEX)
    FFT_real_to_complex(gl_GlobalInvocationID.xy);
#elif defined(FFT_RESOLVE_COMPLEX_TO_REAL)
    FFT_complex_to_real(gl_GlobalInvocationID.xy);
#elif FFT_RADIX == 4
    FFT4();
#elif FFT_RADIX == 8
    FFT8();
#elif FFT_RADIX == 16
    FFT16();
#elif FFT_RADIX == 64
    FFT64();
#else
#error Unimplemented FFT radix.
#endif
}

