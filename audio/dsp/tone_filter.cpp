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

#include "tone_filter.hpp"
#include "aligned_alloc.hpp"
#include "pole_zero_filter_design.hpp"
#include "dsp.hpp"
#include "logging.hpp"
#include <complex>
#include <cmath>
#include <algorithm>
#include <assert.h>

#ifdef TONE_DEBUG
#include "audio_events.hpp"
#endif

#define ENABLE_SIMD 1
#if ENABLE_SIMD
#include "simd_headers.hpp"
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
	alignas(64) float fir_history[FilterTaps] = {};
	alignas(64) float iir_history[FilterTaps][ToneCount] = {};
	alignas(64) float fir_coeff[FilterTaps + 1][ToneCount] = {};
	alignas(64) float iir_coeff[FilterTaps][ToneCount] = {};
	alignas(64) float running_power[ToneCount] = {};
	alignas(64) float running_total_power = {};
	unsigned index = 0;

	unsigned iir_filter_taps = 0;
	unsigned fir_filter_taps = 0;
	float tone_power_lerp = 0.00012f;
	float total_tone_power_lerp = 0.0001f;
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
		                                                    impl->tone_buffers[i].size() *
		                                                    sizeof(float),
		                                                    id, i,
		                                                    impl->running_power[i] /
		                                                    (impl->running_total_power + 0.000001f),
		                                                    impl->tone_buffers[i].data(),
		                                                    impl->tone_buffers[i].size());
		impl->tone_buffers[i].clear();
	}
}

#endif

void ToneFilter::init(float sample_rate, float tuning_freq)
{
	// Readjust falloff based on sample rate.
	impl->tone_power_lerp = float(1.0 - exp(log(0.00503) / sample_rate));
	impl->total_tone_power_lerp = float(1.0 - exp(log(0.01215) / sample_rate));

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

static inline float distort(float v)
{
	float abs_v = std::abs(v);
	return v / (1.0f + abs_v);
}

#if ENABLE_SIMD
#if defined(__AVX__)
alignas(16) static const uint32_t absmask = 0x7fffffffu;
static inline __m256 div_ps(__m256 a, __m256 b)
{
	return _mm256_mul_ps(a, _mm256_rcp_ps(b));
}

static inline __m256 sqrt_ps(__m256 v)
{
	return _mm256_mul_ps(v, _mm256_rsqrt_ps(_mm256_max_ps(v, _mm256_set1_ps(1e-30f))));
}

static inline __m256 fma_ps(__m256 c, __m256 a, __m256 b)
{
#ifdef __FMA__
	return _mm256_fmadd_ps(a, b, c);
#else
	return _mm256_add_ps(_mm256_mul_ps(a, b), c);
#endif
}
#elif defined(__SSE__)
alignas(16) static const uint32_t absmask[4] = {0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu};

static inline __m128 div_ps(__m128 a, __m128 b)
{
	return _mm_mul_ps(a, _mm_rcp_ps(b));
}

static inline __m128 sqrt_ps(__m128 v)
{
	return _mm_mul_ps(v, _mm_rsqrt_ps(_mm_max_ps(v, _mm_set1_ps(1e-30f))));
}
#elif defined(__ARM_NEON)
static inline float32x4_t div_ps(float32x4_t a, float32x4_t b)
{
	return vmulq_f32(a, vrecpeq_f32(b));
}

static inline float32x4_t sqrt_ps(float32x4_t v)
{
	return vmulq_f32(v, vrsqrteq_f32(vmaxq_f32(v, vdupq_n_f32(1e-30f))));
}
#endif
#endif

void ToneFilter::Impl::filter(float *out_samples, const float *in_samples, unsigned count)
{
	for (unsigned samp = 0; samp < count; samp++)
	{
		float in_sample = in_samples[samp];
		running_total_power =
				running_total_power * (1.0f - total_tone_power_lerp) +
				total_tone_power_lerp * in_sample * in_sample;
		float low_threshold = 0.0002f * running_total_power;
		float high_threshold = 0.10f * running_total_power;
		float low_threshold_divider =
				1.0f / std::max(0.00000000001f, low_threshold * low_threshold * low_threshold);

#if defined(__AVX__) && ENABLE_SIMD
		__m256 final_sample_vec = _mm256_setzero_ps();
		__m256 in_sample_splat = _mm256_set1_ps(in_sample);
		for (int tone = 0; tone < ToneCount; tone += 8)
		{
			__m256 ret = _mm256_mul_ps(_mm256_load_ps(fir_coeff[0] + tone), in_sample_splat);

			for (unsigned x = 0; x < fir_filter_taps; x++)
			{
				__m256 history = _mm256_broadcast_ss(&fir_history[(index + x) & (FilterTaps - 1)]);
				ret = fma_ps(ret,
				             _mm256_load_ps(fir_coeff[x + 1] + tone),
				             history);
			}

			for (unsigned x = 0; x < iir_filter_taps; x++)
			{
				ret = fma_ps(ret,
				             _mm256_load_ps(iir_coeff[x] + tone),
				             _mm256_load_ps(iir_history[(index + x) & (FilterTaps - 1)] + tone));
			}

			_mm256_store_ps(iir_history[(index - 1) & (FilterTaps - 1)] + tone, ret);

			__m256 new_power = _mm256_mul_ps(ret, ret);
			__m256 new_power_4 = _mm256_mul_ps(new_power, new_power);
			new_power_4 = _mm256_mul_ps(new_power_4, new_power_4);
			new_power_4 = _mm256_mul_ps(new_power_4, _mm256_set1_ps(low_threshold_divider));

			new_power = _mm256_min_ps(new_power, new_power_4);
			new_power = _mm256_min_ps(new_power, _mm256_set1_ps(high_threshold));

			new_power = _mm256_mul_ps(new_power, _mm256_set1_ps(tone_power_lerp));
			new_power = fma_ps(new_power,
			                   _mm256_load_ps(running_power + tone),
			                   _mm256_set1_ps(1.0f - tone_power_lerp));

			_mm256_store_ps(running_power + tone, new_power);
			__m256 rms = sqrt_ps(new_power);

			__m256 distorted = div_ps(
					_mm256_mul_ps(ret, _mm256_set1_ps(40.0f)),
					_mm256_add_ps(rms, _mm256_set1_ps(0.001f)));
			__m256 distorted_abs = _mm256_and_ps(distorted, _mm256_broadcast_ss(reinterpret_cast<const float *>(&absmask)));
			__m256 distorted_div = _mm256_add_ps(_mm256_set1_ps(1.0f), distorted_abs);
			distorted = div_ps(distorted, distorted_div);
			final_sample_vec = fma_ps(final_sample_vec, rms, distorted);

#ifdef TONE_DEBUG
			__m256 final = _mm256_mul_ps(rms, distorted);
			float final_buffers[8];
			_mm256_storeu_ps(final_buffers, final);
			for (unsigned j = 0; j < 8; j++)
				tone_buffers[tone + j].push_back(final_buffers[j]);
#endif
		}
#elif defined(__SSE__) && ENABLE_SIMD
		__m128 final_sample_vec = _mm_setzero_ps();
		__m128 in_sample_splat = _mm_set1_ps(in_sample);
		for (int tone = 0; tone < ToneCount; tone += 4)
		{
			__m128 ret = _mm_mul_ps(_mm_load_ps(fir_coeff[0] + tone), in_sample_splat);

			for (unsigned x = 0; x < fir_filter_taps; x++)
			{
				__m128 history = _mm_load_ss(&fir_history[(index + x) & (FilterTaps - 1)]);
				history = _mm_shuffle_ps(history, history, _MM_SHUFFLE(0, 0, 0, 0));

				ret = _mm_add_ps(ret, _mm_mul_ps(
						_mm_load_ps(fir_coeff[x + 1] + tone),
						history));
			}

			for (unsigned x = 0; x < iir_filter_taps; x++)
			{
				ret = _mm_add_ps(ret, _mm_mul_ps(
						_mm_load_ps(iir_coeff[x] + tone),
						_mm_load_ps(iir_history[(index + x) & (FilterTaps - 1)] + tone)));
			}

			_mm_store_ps(iir_history[(index - 1) & (FilterTaps - 1)] + tone, ret);

			__m128 new_power = _mm_mul_ps(ret, ret);
			__m128 new_power_4 = _mm_mul_ps(new_power, new_power);
			new_power_4 = _mm_mul_ps(new_power_4, new_power_4);
			new_power_4 = _mm_mul_ps(new_power_4, _mm_set1_ps(low_threshold_divider));

			new_power = _mm_min_ps(new_power, new_power_4);
			new_power = _mm_min_ps(new_power, _mm_set1_ps(high_threshold));

			new_power = _mm_add_ps(
					_mm_mul_ps(_mm_load_ps(running_power + tone), _mm_set1_ps(1.0f - tone_power_lerp)),
					_mm_mul_ps(new_power, _mm_set1_ps(tone_power_lerp)));
			_mm_store_ps(running_power + tone, new_power);

			__m128 rms = sqrt_ps(new_power);

			__m128 distorted = div_ps(
					_mm_mul_ps(ret, _mm_set1_ps(40.0f)),
					_mm_add_ps(rms, _mm_set1_ps(0.001f)));
			__m128 distorted_abs = _mm_and_ps(distorted, _mm_load_ps(reinterpret_cast<const float *>(absmask)));
			__m128 distorted_div = _mm_add_ps(_mm_set1_ps(1.0f), distorted_abs);
			distorted = div_ps(distorted, distorted_div);
			__m128 final = _mm_mul_ps(rms, distorted);
			final_sample_vec = _mm_add_ps(final, final_sample_vec);

#ifdef TONE_DEBUG
			float final_buffers[4];
			_mm_storeu_ps(final_buffers, final);
			for (unsigned j = 0; j < 4; j++)
				tone_buffers[tone + j].push_back(final_buffers[j]);
#endif
		}
#elif defined(__ARM_NEON) && ENABLE_SIMD
		float32x4_t final_sample_vec = vdupq_n_f32(0.0f);
		for (int tone = 0; tone < ToneCount; tone += 4)
		{
			float32x4_t ret = vmulq_n_f32(vld1q_f32(fir_coeff[0] + tone), in_sample);

			for (unsigned x = 0; x < fir_filter_taps; x++)
			{
				float history = fir_history[(index + x) & (FilterTaps - 1)];
				ret = vmlaq_n_f32(ret,
				                  vld1q_f32(fir_coeff[x + 1] + tone),
				                  history);
			}

			for (unsigned x = 0; x < iir_filter_taps; x++)
			{
				ret = vmlaq_f32(ret,
				                vld1q_f32(iir_coeff[x] + tone),
				                vld1q_f32(iir_history[(index + x) & (FilterTaps - 1)] + tone));
			}

			vst1q_f32(iir_history[(index - 1) & (FilterTaps - 1)] + tone, ret);

			float32x4_t new_power = vmulq_f32(ret, ret);
			float32x4_t new_power_4 = vmulq_f32(new_power, new_power);
			new_power_4 = vmulq_f32(new_power_4, new_power_4);
			new_power_4 = vmulq_n_f32(new_power_4, low_threshold_divider);

			new_power = vminq_f32(new_power, new_power_4);
			new_power = vminq_f32(new_power, vdupq_n_f32(high_threshold));

			new_power = vmulq_n_f32(new_power, tone_power_lerp);
			new_power = vmlaq_n_f32(new_power, vld1q_f32(running_power + tone),
			                        1.0f - tone_power_lerp);
			vst1q_f32(running_power + tone, new_power);

			float32x4_t rms = sqrt_ps(new_power);

			float32x4_t distorted = div_ps(
					vmulq_n_f32(ret, 40.0f),
					vaddq_f32(rms, vdupq_n_f32(0.001f)));
			float32x4_t distorted_abs = vabsq_f32(distorted);
			float32x4_t distorted_div = vaddq_f32(vdupq_n_f32(1.0f), distorted_abs);
			distorted = div_ps(distorted, distorted_div);
			final_sample_vec = vmlaq_f32(final_sample_vec, rms, distorted);
#ifdef TONE_DEBUG
			float32x4_t final = vmulq_f32(rms, distorted);
			float final_buffers[4];
			vst1q_f32(final_buffers, final);
			for (unsigned j = 0; j < 4; j++)
				tone_buffers[tone + j].push_back(final_buffers[j]);
#endif
		}
#else
		float final_sample = 0.0f;
		for (int tone = 0; tone < ToneCount; tone++)
		{
			float ret = fir_coeff[0][tone] * in_sample;
			for (unsigned x = 0; x < fir_filter_taps; x++)
				ret += fir_coeff[x + 1][tone] * fir_history[(index + x) & (FilterTaps - 1)];
			for (unsigned x = 0; x < iir_filter_taps; x++)
				ret += iir_coeff[x][tone] * iir_history[(index + x) & (FilterTaps - 1)][tone];

			iir_history[(index - 1) & (FilterTaps - 1)][tone] = ret;

			float new_power = ret * ret;

			new_power = std::min(new_power, new_power * new_power * new_power * new_power * low_threshold_divider);
			new_power = std::min(new_power, high_threshold);

			new_power = (1.0f - tone_power_lerp) * running_power[tone] + tone_power_lerp * new_power;
			running_power[tone] = new_power;

			float rms = std::sqrt(new_power);
			float final = rms * distort(ret * 40.0f / (rms + 0.001f));
			final_sample += final;

#ifdef TONE_DEBUG
			tone_buffers[tone].push_back(final);
#endif
		}
#endif

#if defined(__AVX__) && ENABLE_SIMD
		float final_sample;
		__m128 final_sample128 = _mm_add_ps(
				_mm256_extractf128_ps(final_sample_vec, 0),
				_mm256_extractf128_ps(final_sample_vec, 1));
		__m128 final_sample_half = _mm_add_ps(final_sample128,
		                                      _mm_movehl_ps(final_sample128, final_sample128));
		final_sample_half =
				_mm_add_ss(final_sample_half,
				           _mm_shuffle_ps(final_sample_half, final_sample_half, _MM_SHUFFLE(1, 1, 1, 1)));
		final_sample_half = _mm_mul_ss(_mm_set1_ps(0.5f), _mm_add_ss(final_sample_half, _mm_load_ss(&final_history)));
		_mm_store_ss(&final_history, final_sample_half);
		_mm_store_ss(&final_sample, final_sample_half);
#elif defined(__SSE__) && ENABLE_SIMD
		float final_sample;
		__m128 final_sample_half = _mm_add_ps(final_sample_vec,
											  _mm_movehl_ps(final_sample_vec, final_sample_vec));
		final_sample_half =
				_mm_add_ss(final_sample_half,
						   _mm_shuffle_ps(final_sample_half, final_sample_half, _MM_SHUFFLE(1, 1, 1, 1)));
		final_sample_half = _mm_mul_ss(_mm_set1_ps(0.5f), _mm_add_ss(final_sample_half, _mm_load_ss(&final_history)));
		_mm_store_ss(&final_history, final_sample_half);
		_mm_store_ss(&final_sample, final_sample_half);
#elif defined(__ARM_NEON) && ENABLE_SIMD
		float32x2_t final_sample_half = vadd_f32(vget_low_f32(final_sample_vec),
		                                         vget_high_f32(final_sample_vec));
		final_sample_half = vpadd_f32(final_sample_half, final_sample_half);
		float final_sample = vget_lane_f32(final_sample_half, 0);
		final_sample = 0.5f * final_history + 0.5f * final_sample;
		final_history = final_sample;
#else
		// Trivial 1-pole IIR filter to serve as a slight low-pass to dampen the worst high-end.
		final_sample = 0.5f * final_history + 0.5f * final_sample;
		final_history = final_sample;
#endif

		fir_history[(index - 1) & (FilterTaps - 1)] = in_sample;
		out_samples[samp] = distort(2.0f * final_sample);
		index = (index - 1) & (FilterTaps - 1);
	}

#if defined(__AVX__) && ENABLE_SIMD
	_mm256_zeroupper();
#endif
}

void ToneFilter::filter(float *out_samples, const float *in_samples, unsigned count)
{
	impl->filter(out_samples, in_samples, count);
}
}
}
}
