/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include <math.h>
#include <stdint.h>
#include "simd_headers.hpp"

namespace Granite
{
namespace Audio
{
namespace DSP
{
static inline void accumulate_channel_deinterleave_stereo(float * __restrict left, float * __restrict right,
                                                          const float * __restrict input,
                                                          const float * __restrict gain,
                                                          size_t count) noexcept
{
#if defined(__SSE__)
	size_t rounded_count = count & ~4;
	__m128 gain_left_splat = _mm_set1_ps(gain[0]);
	__m128 gain_right_splat = _mm_set1_ps(gain[1]);
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 acc_l = _mm_loadu_ps(left);
		__m128 acc_r = _mm_loadu_ps(right);
		__m128 in0 = _mm_loadu_ps(input + 0);
		__m128 in1 = _mm_loadu_ps(input + 4);
		__m128 in_l = _mm_shuffle_ps(in0, in1, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 in_r = _mm_shuffle_ps(in0, in1, _MM_SHUFFLE(3, 1, 3, 1));
		acc_l = _mm_add_ps(acc_l, _mm_mul_ps(in_l, gain_left_splat));
		acc_r = _mm_add_ps(acc_r, _mm_mul_ps(in_r, gain_right_splat));
		_mm_storeu_ps(left, acc_l);
		_mm_storeu_ps(right, acc_r);

		left += 4;
		right += 4;
		input += 8;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
	{
		left[i] += input[2 * i + 0] * gain[0];
		right[i] += input[2 * i + 1] * gain[1];
	}
#else
	for (size_t i = 0; i < count; i++)
	{
		left[i] += input[2 * i + 0] * gain[0];
		right[i] += input[2 * i + 1] * gain[1];
	}
#endif
}

static inline void accumulate_channel(float * __restrict output, const float * __restrict input, float gain, size_t count) noexcept
{
#ifdef __ARM_NEON
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		float32x4_t acc = vld1q_f32(output);
		float32x4_t in = vld1q_f32(input);
		acc = vmlaq_n_f32(acc, in, gain);
		vst1q_f32(output, acc);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] += input[i] * gain;
#elif defined(__SSE__)
	size_t rounded_count = count & ~3;
	__m128 gain_splat = _mm_set1_ps(gain);
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 acc = _mm_loadu_ps(output);
		__m128 in = _mm_loadu_ps(input);
		acc = _mm_add_ps(acc, _mm_mul_ps(in, gain_splat));
		_mm_storeu_ps(output, acc);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] += input[i] * gain;
#else
	for (size_t i = 0; i < count; i++)
		output[i] += input[i] * gain;
#endif
}

static inline void accumulate_channel_s32(float * __restrict output, const int32_t * __restrict input,
                                          float gain, size_t count) noexcept
{
	gain *= 1.0f / float(0x80000000u);
	for (size_t i = 0; i < count; i++)
		output[i] += float(input[i]) * gain;
}

static inline void accumulate_channel_deinterleave_stereo_s32(
		float * __restrict left, float * __restrict right,
		const int32_t * __restrict input, const float * __restrict gain,
		size_t count) noexcept
{
	const float left_gain = gain[0] * (1.0f / float(0x80000000u));
	const float right_gain = gain[1] * (1.0f / float(0x80000000u));
	for (size_t i = 0; i < count; i++)
	{
		left[i] += float(input[2 * i + 0]) * left_gain;
		right[i] += float(input[2 * i + 1]) * right_gain;
	}
}

static inline void accumulate_channel_s16(float * __restrict output, const int16_t * __restrict input,
                                          float gain, size_t count) noexcept
{
	gain *= 1.0f / float(0x8000u);
	for (size_t i = 0; i < count; i++)
		output[i] += float(input[i]) * gain;
}

static inline void accumulate_channel_deinterleave_stereo_s16(
		float * __restrict left, float * __restrict right,
		const int16_t * __restrict input, const float * __restrict gain,
		size_t count) noexcept
{
	const float left_gain = gain[0] * (1.0f / float(0x8000u));
	const float right_gain = gain[1] * (1.0f / float(0x8000u));
	for (size_t i = 0; i < count; i++)
	{
		left[i] += float(input[2 * i + 0]) * left_gain;
		right[i] += float(input[2 * i + 1]) * right_gain;
	}
}

static inline void replace_channel(float * __restrict output, const float * __restrict input, float gain, size_t count) noexcept
{
#ifdef __ARM_NEON
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		float32x4_t in = vld1q_f32(input);
		in = vmulq_n_f32(in, gain);
		vst1q_f32(output, in);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] = input[i] * gain;
#elif defined(__SSE__)
	size_t rounded_count = count & ~3;
	__m128 gain_splat = _mm_set1_ps(gain);
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 in = _mm_loadu_ps(input);
		in = _mm_mul_ps(in, gain_splat);
		_mm_storeu_ps(output, in);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] = input[i] * gain;
#else
	for (size_t i = 0; i < count; i++)
		output[i] = input[i] * gain;
#endif
}

static inline void accumulate_channel_nogain(float * __restrict output, const float * __restrict input, size_t count) noexcept
{
#ifdef __ARM_NEON
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		float32x4_t acc = vld1q_f32(output);
		float32x4_t in = vld1q_f32(input);
		acc = vaddq_f32(acc, in);
		vst1q_f32(output, acc);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] += input[i];
#elif defined(__SSE__)
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 acc = _mm_loadu_ps(output);
		__m128 in = _mm_loadu_ps(input);
		acc = _mm_add_ps(acc, in);
		_mm_storeu_ps(output, acc);

		output += 4;
		input += 4;
	}

	size_t overflow_count = count & 3;
	for (size_t i = 0; i < overflow_count; i++)
		output[i] += input[i];
#else
	for (size_t i = 0; i < count; i++)
		output[i] += input[i];
#endif
}

static inline void convert_to_mono(float * __restrict output,
                                   const float * __restrict const *input,
                                   unsigned num_channels,
                                   size_t count) noexcept
{
	float inv_channels = 1.0f / num_channels;
	for (size_t i = 0; i < count; i++)
	{
		float ret = 0.0f;
		for (unsigned c = 0; c < num_channels; c++)
			ret += input[c][i];
		output[i] = ret * inv_channels;
	}
}

static int16_t f32_to_i16(float v) noexcept
{
	auto i = int32_t(roundf(v * 0x8000));
	if (i > 0x7fff)
		return 0x7fff;
	else if (i < -0x8000)
		return -0x8000;
	else
		return int16_t(i);
}

static inline void deinterleave_stereo_f32(float * __restrict left,
                                           float * __restrict right,
                                           const float * __restrict input,
                                           size_t count) noexcept
{
#ifdef __SSE__
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 s0 = _mm_loadu_ps(input);
		input += 4;
		__m128 s1 = _mm_loadu_ps(input);
		input += 4;

		__m128 l = _mm_shuffle_ps(s0, s1, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 r = _mm_shuffle_ps(s0, s1, _MM_SHUFFLE(3, 1, 3, 1));
		_mm_storeu_ps(left, l);
		_mm_storeu_ps(right, r);
		left += 4;
		right += 4;
	}

	for (size_t i = rounded_count; i < count; i++)
	{
		*left++ = *input++;
		*right++ = *input++;
	}
#else
	for (size_t i = 0; i < count; i++)
	{
		*left++ = *input++;
		*right++ = *input++;
	}
#endif
}

static inline void interleave_stereo_f32(float * __restrict target,
                                         const float * __restrict left,
                                         const float * __restrict right,
                                         size_t count) noexcept
{
#ifdef __SSE__
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		__m128 l = _mm_loadu_ps(left);
		__m128 r = _mm_loadu_ps(right);
		left += 4;
		right += 4;
		__m128 interleaved0 = _mm_unpacklo_ps(l, r);
		__m128 interleaved1 = _mm_unpackhi_ps(l, r);
		_mm_storeu_ps(target, interleaved0);
		_mm_storeu_ps(target + 4, interleaved1);
		target += 8;
	}

	for (size_t i = rounded_count; i < count; i++)
	{
		*target++ = *left++;
		*target++ = *right++;
	}
#else
	for (size_t i = 0; i < count; i++)
	{
		*target++ = *left++;
		*target++ = *right++;
	}
#endif
}

static inline void interleave_stereo_f32_i16(int16_t * __restrict target,
                                             const float * __restrict left,
                                             const float * __restrict right,
                                             size_t count) noexcept
{
#ifdef __ARM_NEON
	size_t rounded_count = count & ~3;
	for (size_t i = 0; i < rounded_count; i += 4)
	{
		float32x4_t l = vld1q_f32(left);
		float32x4_t r = vld1q_f32(right);

		l = vmulq_n_f32(l, float(0x8000));
		r = vmulq_n_f32(r, float(0x8000));

		int32x4_t il = vcvtq_s32_f32(l);
		int32x4_t ir = vcvtq_s32_f32(r);
		int16x4_t sl = vqmovn_s32(il);
		int16x4_t sr = vqmovn_s32(ir);
		int16x4x2_t stereo = { sl, sr };
		vst2_s16(target, stereo);

		left += 4;
		right += 4;
		target += 8;
	}

	for (size_t i = rounded_count; i < count; i++)
	{
		*target++ = f32_to_i16(*left++);
		*target++ = f32_to_i16(*right++);
	}
#else
	for (size_t i = 0; i < count; i++)
	{
		*target++ = f32_to_i16(*left++);
		*target++ = f32_to_i16(*right++);
	}
#endif
}

static inline void f32_to_i16(int16_t * __restrict target,
                              const float * __restrict data,
                              size_t count)
{
	for (size_t i = 0; i < count; i++)
		*target++ = f32_to_i16(*data++);
}

struct EqualizerParameter
{
	float freq;
	float gain_db;
};

float gain_to_db(float gain);
float db_to_gain(float db);

// Parameters must come in sorted order.
void create_parametric_eq_filter(float *coeffs, unsigned num_coeffs,
                                 float sample_rate,
                                 const EqualizerParameter *parameters,
                                 unsigned num_parameters);

double sinc(double val);
double kaiser_window_function(double index, double beta);

}
}
}
