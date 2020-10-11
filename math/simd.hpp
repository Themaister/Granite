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

#include "math.hpp"
#include "aabb.hpp"
#include "simd_headers.hpp"
#include "muglm/matrix_helper.hpp"

namespace Granite
{
namespace SIMD
{
static inline bool frustum_cull(const AABB &aabb, const vec4 *planes)
{
#if defined(__SSE3__)
	__m128 lo = _mm_loadu_ps(aabb.get_minimum4().data);
	__m128 hi = _mm_loadu_ps(aabb.get_maximum4().data);

#define COMPUTE_PLANE(i) \
	__m128 p##i = _mm_loadu_ps(planes[i].data); \
	__m128 mask##i = _mm_cmpgt_ps(p##i, _mm_setzero_ps()); \
	__m128 major_axis##i = _mm_or_ps(_mm_and_ps(mask##i, hi), _mm_andnot_ps(mask##i, lo)); \
	__m128 dotted##i = _mm_mul_ps(p##i, major_axis##i)
	COMPUTE_PLANE(0);
	COMPUTE_PLANE(1);
	COMPUTE_PLANE(2);
	COMPUTE_PLANE(3);
	COMPUTE_PLANE(4);
	COMPUTE_PLANE(5);

	__m128 merged01 = _mm_hadd_ps(dotted0, dotted1);
	__m128 merged23 = _mm_hadd_ps(dotted2, dotted3);
	__m128 merged45 = _mm_hadd_ps(dotted4, dotted5);
	__m128 merged0123 = _mm_hadd_ps(merged01, merged23);
	merged45 = _mm_hadd_ps(merged45, merged45);
	__m128 merged = _mm_or_ps(merged0123, merged45);
	// Sets bit if the sign bit is set.
	int mask = _mm_movemask_ps(merged);
	return mask == 0;
#elif defined(__ARM_NEON)
	float32x4_t lo = vld1q_f32(aabb.get_minimum4().data);
	float32x4_t hi = vld1q_f32(aabb.get_maximum4().data);

#define COMPUTE_PLANE(i) \
	float32x4_t p##i = vld1q_f32(planes[i].data); \
	uint32x4_t mask##i = vcgtq_f32(p##i, vdupq_n_f32(0.0f)); \
	float32x4_t major_axis##i = vbslq_f32(mask##i, hi, lo); \
	float32x4_t dotted##i = vmulq_f32(p##i, major_axis##i)
	COMPUTE_PLANE(0);
	COMPUTE_PLANE(1);
	COMPUTE_PLANE(2);
	COMPUTE_PLANE(3);
	COMPUTE_PLANE(4);
	COMPUTE_PLANE(5);

#if defined(__aarch64__)
	float32x4_t merged01 = vpaddq_f32(dotted0, dotted1);
	float32x4_t merged23 = vpaddq_f32(dotted2, dotted3);
	float32x4_t merged45 = vpaddq_f32(dotted4, dotted5);
	float32x4_t merged0123 = vpaddq_f32(merged01, merged23);
	merged45 = vpaddq_f32(merged45, merged45);
	float32x4_t merged = vminq_f32(merged0123, merged45);
	float32x2_t merged_half = vmin_f32(vget_low_f32(merged), vget_high_f32(merged));
	merged_half = vpmin_f32(merged_half, merged_half);
	return vget_lane_f32(merged_half, 0) >= 0.0f;
#else
	float32x2_t merged0 = vpadd_f32(vget_low_f32(dotted0), vget_high_f32(dotted0));
	float32x2_t merged1 = vpadd_f32(vget_low_f32(dotted1), vget_high_f32(dotted1));
	float32x2_t merged2 = vpadd_f32(vget_low_f32(dotted2), vget_high_f32(dotted2));
	float32x2_t merged3 = vpadd_f32(vget_low_f32(dotted3), vget_high_f32(dotted3));
	float32x2_t merged4 = vpadd_f32(vget_low_f32(dotted4), vget_high_f32(dotted4));
	float32x2_t merged5 = vpadd_f32(vget_low_f32(dotted5), vget_high_f32(dotted5));
	float32x2_t merged01 = vpadd_f32(merged0, merged1);
	float32x2_t merged23 = vpadd_f32(merged2, merged3);
	float32x2_t merged45 = vpadd_f32(merged4, merged5);
	float32x2_t merged = vmin_f32(merged01, merged23);
	merged = vmin_f32(merged, merged45);
	float32x2_t merged_half = vpmin_f32(merged, merged);
	return vget_lane_f32(merged_half, 0) >= 0.0f;
#endif
#else
#error "Implement me."
#endif
}

static inline void mul(vec4 &c, const mat4 &a, const vec4 &b)
{
#if defined(__SSE__)
	__m128 a0 = _mm_loadu_ps(a[0].data);
	__m128 a1 = _mm_loadu_ps(a[1].data);
	__m128 a2 = _mm_loadu_ps(a[2].data);
	__m128 a3 = _mm_loadu_ps(a[3].data);
	__m128 b0 = _mm_loadu_ps(b.data);

	__m128 b00 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 b01 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 b02 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 b03 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(3, 3, 3, 3));

	__m128 col0 = _mm_mul_ps(a0, b00);
	col0 = _mm_add_ps(col0, _mm_mul_ps(a1, b01));
	col0 = _mm_add_ps(col0, _mm_mul_ps(a2, b02));
	col0 = _mm_add_ps(col0, _mm_mul_ps(a3, b03));

	_mm_storeu_ps(c.data, col0);
#elif defined(__ARM_NEON)
	float32x4_t a0 = vld1q_f32(a[0].data);
	float32x4_t a1 = vld1q_f32(a[1].data);
	float32x4_t a2 = vld1q_f32(a[2].data);
	float32x4_t a3 = vld1q_f32(a[3].data);
	float32x4_t b0 = vld1q_f32(b.data);

	float32x4_t col0 = vmulq_n_f32(a0, vgetq_lane_f32(b0, 0));
	col0 = vmlaq_n_f32(col0, a1, vgetq_lane_f32(b0, 1));
	col0 = vmlaq_n_f32(col0, a2, vgetq_lane_f32(b0, 2));
	col0 = vmlaq_n_f32(col0, a3, vgetq_lane_f32(b0, 3));

	vst1q_f32(c.data, col0);
#else
	c = a * b;
#endif
}

static inline void mul(mat4 &c, const mat4 &a, const mat4 &b)
{
#if defined(__SSE__)
	__m128 a0 = _mm_loadu_ps(a[0].data);
	__m128 a1 = _mm_loadu_ps(a[1].data);
	__m128 a2 = _mm_loadu_ps(a[2].data);
	__m128 a3 = _mm_loadu_ps(a[3].data);
	__m128 b0 = _mm_loadu_ps(b[0].data);
	__m128 b1 = _mm_loadu_ps(b[1].data);
	__m128 b2 = _mm_loadu_ps(b[2].data);
	__m128 b3 = _mm_loadu_ps(b[3].data);

	__m128 b00 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 b01 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 b02 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 b03 = _mm_shuffle_ps(b0, b0, _MM_SHUFFLE(3, 3, 3, 3));

	__m128 col0 = _mm_mul_ps(a0, b00);
	col0 = _mm_add_ps(col0, _mm_mul_ps(a1, b01));
	col0 = _mm_add_ps(col0, _mm_mul_ps(a2, b02));
	col0 = _mm_add_ps(col0, _mm_mul_ps(a3, b03));

	__m128 b10 = _mm_shuffle_ps(b1, b1, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 b11 = _mm_shuffle_ps(b1, b1, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 b12 = _mm_shuffle_ps(b1, b1, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 b13 = _mm_shuffle_ps(b1, b1, _MM_SHUFFLE(3, 3, 3, 3));

	__m128 col1 = _mm_mul_ps(a0, b10);
	col1 = _mm_add_ps(col1, _mm_mul_ps(a1, b11));
	col1 = _mm_add_ps(col1, _mm_mul_ps(a2, b12));
	col1 = _mm_add_ps(col1, _mm_mul_ps(a3, b13));

	__m128 b20 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 b21 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 b22 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 b23 = _mm_shuffle_ps(b2, b2, _MM_SHUFFLE(3, 3, 3, 3));

	__m128 col2 = _mm_mul_ps(a0, b20);
	col2 = _mm_add_ps(col2, _mm_mul_ps(a1, b21));
	col2 = _mm_add_ps(col2, _mm_mul_ps(a2, b22));
	col2 = _mm_add_ps(col2, _mm_mul_ps(a3, b23));

	__m128 b30 = _mm_shuffle_ps(b3, b3, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 b31 = _mm_shuffle_ps(b3, b3, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 b32 = _mm_shuffle_ps(b3, b3, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 b33 = _mm_shuffle_ps(b3, b3, _MM_SHUFFLE(3, 3, 3, 3));

	__m128 col3 = _mm_mul_ps(a0, b30);
	col3 = _mm_add_ps(col3, _mm_mul_ps(a1, b31));
	col3 = _mm_add_ps(col3, _mm_mul_ps(a2, b32));
	col3 = _mm_add_ps(col3, _mm_mul_ps(a3, b33));

	_mm_storeu_ps(c[0].data, col0);
	_mm_storeu_ps(c[1].data, col1);
	_mm_storeu_ps(c[2].data, col2);
	_mm_storeu_ps(c[3].data, col3);
#elif defined(__ARM_NEON)
	float32x4_t a0 = vld1q_f32(a[0].data);
	float32x4_t a1 = vld1q_f32(a[1].data);
	float32x4_t a2 = vld1q_f32(a[2].data);
	float32x4_t a3 = vld1q_f32(a[3].data);
	float32x4_t b0 = vld1q_f32(b[0].data);
	float32x4_t b1 = vld1q_f32(b[1].data);
	float32x4_t b2 = vld1q_f32(b[2].data);
	float32x4_t b3 = vld1q_f32(b[3].data);

	float32x4_t col0 = vmulq_n_f32(a0, vgetq_lane_f32(b0, 0));
	float32x4_t col1 = vmulq_n_f32(a0, vgetq_lane_f32(b1, 0));
	float32x4_t col2 = vmulq_n_f32(a0, vgetq_lane_f32(b2, 0));
	float32x4_t col3 = vmulq_n_f32(a0, vgetq_lane_f32(b3, 0));

	col0 = vmlaq_n_f32(col0, a1, vgetq_lane_f32(b0, 1));
	col1 = vmlaq_n_f32(col1, a1, vgetq_lane_f32(b1, 1));
	col2 = vmlaq_n_f32(col2, a1, vgetq_lane_f32(b2, 1));
	col3 = vmlaq_n_f32(col3, a1, vgetq_lane_f32(b3, 1));

	col0 = vmlaq_n_f32(col0, a2, vgetq_lane_f32(b0, 2));
	col1 = vmlaq_n_f32(col1, a2, vgetq_lane_f32(b1, 2));
	col2 = vmlaq_n_f32(col2, a2, vgetq_lane_f32(b2, 2));
	col3 = vmlaq_n_f32(col3, a2, vgetq_lane_f32(b3, 2));

	col0 = vmlaq_n_f32(col0, a3, vgetq_lane_f32(b0, 3));
	col1 = vmlaq_n_f32(col1, a3, vgetq_lane_f32(b1, 3));
	col2 = vmlaq_n_f32(col2, a3, vgetq_lane_f32(b2, 3));
	col3 = vmlaq_n_f32(col3, a3, vgetq_lane_f32(b3, 3));

	vst1q_f32(c[0].data, col0);
	vst1q_f32(c[1].data, col1);
	vst1q_f32(c[2].data, col2);
	vst1q_f32(c[3].data, col3);
#else
	c = a * b;
#endif
}

static inline void transform_aabb(AABB &output, const AABB &aabb, const mat4 &m)
{
#if defined(__SSE__)
	__m128 lo = _mm_loadu_ps(aabb.get_minimum4().data);
	__m128 hi = _mm_loadu_ps(aabb.get_maximum4().data);

	__m128 m0 = _mm_loadu_ps(m[0].data);
	__m128 m1 = _mm_loadu_ps(m[1].data);
	__m128 m2 = _mm_loadu_ps(m[2].data);
	__m128 m3 = _mm_loadu_ps(m[3].data);

	__m128 m0_pos = _mm_cmpgt_ps(m0, _mm_setzero_ps());
	__m128 m1_pos = _mm_cmpgt_ps(m1, _mm_setzero_ps());
	__m128 m2_pos = _mm_cmpgt_ps(m2, _mm_setzero_ps());

	__m128 hi0 = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 hi1 = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 hi2 = _mm_shuffle_ps(hi, hi, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 lo0 = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 lo1 = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 lo2 = _mm_shuffle_ps(lo, lo, _MM_SHUFFLE(2, 2, 2, 2));

	__m128 hi_result = m3;
	hi_result = _mm_add_ps(hi_result, _mm_mul_ps(m0, _mm_or_ps(_mm_and_ps(m0_pos, hi0), _mm_andnot_ps(m0_pos, lo0))));
	hi_result = _mm_add_ps(hi_result, _mm_mul_ps(m1, _mm_or_ps(_mm_and_ps(m1_pos, hi1), _mm_andnot_ps(m1_pos, lo1))));
	hi_result = _mm_add_ps(hi_result, _mm_mul_ps(m2, _mm_or_ps(_mm_and_ps(m2_pos, hi2), _mm_andnot_ps(m2_pos, lo2))));

	__m128 lo_result = m3;
	lo_result = _mm_add_ps(lo_result, _mm_mul_ps(m0, _mm_or_ps(_mm_andnot_ps(m0_pos, hi0), _mm_and_ps(m0_pos, lo0))));
	lo_result = _mm_add_ps(lo_result, _mm_mul_ps(m1, _mm_or_ps(_mm_andnot_ps(m1_pos, hi1), _mm_and_ps(m1_pos, lo1))));
	lo_result = _mm_add_ps(lo_result, _mm_mul_ps(m2, _mm_or_ps(_mm_andnot_ps(m2_pos, hi2), _mm_and_ps(m2_pos, lo2))));

	_mm_storeu_ps(output.get_minimum4().data, lo_result);
	_mm_storeu_ps(output.get_maximum4().data, hi_result);
#elif defined(__ARM_NEON)
	float32x4_t lo = vld1q_f32(aabb.get_minimum4().data);
	float32x4_t hi = vld1q_f32(aabb.get_maximum4().data);

	float32x4_t m0 = vld1q_f32(m[0].data);
	float32x4_t m1 = vld1q_f32(m[1].data);
	float32x4_t m2 = vld1q_f32(m[2].data);
	float32x4_t m3 = vld1q_f32(m[3].data);

	uint32x4_t m0_pos = vcgtq_f32(m0, vdupq_n_f32(0.0f));
	uint32x4_t m1_pos = vcgtq_f32(m1, vdupq_n_f32(0.0f));
	uint32x4_t m2_pos = vcgtq_f32(m2, vdupq_n_f32(0.0f));

	float32x4_t lo0 = vdupq_lane_f32(vget_low_f32(lo), 0);
	float32x4_t lo1 = vdupq_lane_f32(vget_low_f32(lo), 1);
	float32x4_t lo2 = vdupq_lane_f32(vget_high_f32(lo), 0);
	float32x4_t hi0 = vdupq_lane_f32(vget_low_f32(hi), 0);
	float32x4_t hi1 = vdupq_lane_f32(vget_low_f32(hi), 1);
	float32x4_t hi2 = vdupq_lane_f32(vget_high_f32(hi), 0);

	float32x4_t hi_result = m3;
	hi_result = vmlaq_f32(hi_result, m0, vbslq_f32(m0_pos, hi0, lo0));
	hi_result = vmlaq_f32(hi_result, m1, vbslq_f32(m1_pos, hi1, lo1));
	hi_result = vmlaq_f32(hi_result, m2, vbslq_f32(m2_pos, hi2, lo2));

	float32x4_t lo_result = m3;
	lo_result = vmlaq_f32(lo_result, m0, vbslq_f32(m0_pos, lo0, hi0));
	lo_result = vmlaq_f32(lo_result, m1, vbslq_f32(m1_pos, lo1, hi1));
	lo_result = vmlaq_f32(lo_result, m2, vbslq_f32(m2_pos, lo2, hi2));

	vst1q_f32(output.get_minimum4().data, lo_result);
	vst1q_f32(output.get_maximum4().data, hi_result);
#else
	output = aabb.transform(m);
#endif
}

static inline void transform_and_expand_aabb(AABB &expandee, const AABB &aabb, const mat4 &m)
{
	alignas(16) AABB tmp;
	transform_aabb(tmp, aabb, m);
#if defined(__SSE__)
	__m128 lo = _mm_min_ps(_mm_load_ps(tmp.get_minimum4().data), _mm_loadu_ps(expandee.get_minimum4().data));
	__m128 hi = _mm_max_ps(_mm_load_ps(tmp.get_maximum4().data), _mm_loadu_ps(expandee.get_maximum4().data));
	_mm_storeu_ps(expandee.get_minimum4().data, lo);
	_mm_storeu_ps(expandee.get_maximum4().data, hi);
#elif defined(__ARM_NEON)
	float32x4_t lo = vminq_f32(vld1q_f32(tmp.get_minimum4().data), vld1q_f32(expandee.get_minimum4().data));
	float32x4_t hi = vmaxq_f32(vld1q_f32(tmp.get_maximum4().data), vld1q_f32(expandee.get_maximum4().data));
	vst1q_f32(expandee.get_minimum4().data, lo);
	vst1q_f32(expandee.get_maximum4().data, hi);
#else
	auto &output_min = expandee.get_minimum4();
	auto &output_max = expandee.get_maximum4();
	output_min = min(output_min, tmp.get_minimum4());
	output_max = max(output_max, tmp.get_maximum4());
#endif
}

static inline void convert_quaternion_with_scale(vec4 *cols, const quat &q, const vec3 &scale)
{
#if defined(__SSE3__)
	__m128 quat = _mm_loadu_ps(q.as_vec4().data);

#define SHUF(x, y, z) _mm_shuffle_ps(quat, quat, _MM_SHUFFLE(z, y, x, 3))
	__m128 q_yy_xz_xy = _mm_mul_ps(SHUF(1, 0, 0), SHUF(1, 2, 1));
	__m128 q_zz_wy_wz = _mm_mul_ps(SHUF(2, 3, 3), SHUF(2, 1, 2));
	__m128 col0 = _mm_mul_ps(_mm_set_ps(+2.0f, +2.0f, -2.0f, 0.0f), _mm_addsub_ps(q_yy_xz_xy, q_zz_wy_wz));
	col0 = _mm_shuffle_ps(col0, col0, _MM_SHUFFLE(0, 2, 3, 1));
	col0 = _mm_add_ps(col0, _mm_set_ss(1.0f));
	col0 = _mm_mul_ps(col0, _mm_set1_ps(scale.x));
	_mm_storeu_ps(cols[0].data, col0);

	__m128 q_xx_xy_yz = _mm_mul_ps(SHUF(0, 0, 1), SHUF(0, 1, 2));
	__m128 q_zz_wz_wx = _mm_mul_ps(SHUF(2, 3, 3), SHUF(2, 2, 0));
	__m128 col1 = _mm_mul_ps(_mm_set_ps(2.0f, 2.0f, -2.0f, 0.0f), _mm_addsub_ps(q_xx_xy_yz, q_zz_wz_wx));
	col1 = _mm_shuffle_ps(col1, col1, _MM_SHUFFLE(0, 3, 1, 2));
	col1 = _mm_add_ps(col1, _mm_set_ps(0.0f, 0.0f, 1.0f, 0.0f));
	col1 = _mm_mul_ps(col1, _mm_set1_ps(scale.y));
	_mm_storeu_ps(cols[1].data, col1);

	__m128 q_xz_yz_xx = _mm_mul_ps(SHUF(0, 1, 0), SHUF(2, 2, 0));
	__m128 q_wy_wx_yy = _mm_mul_ps(SHUF(3, 3, 1), SHUF(1, 0, 1));
	__m128 col2 = _mm_mul_ps(_mm_set_ps(-2.0f, 2.0f, 2.0f, 0.0f), _mm_addsub_ps(q_xz_yz_xx, q_wy_wx_yy));
	col2 = _mm_shuffle_ps(col2, col2, _MM_SHUFFLE(0, 3, 2, 1));
	col2 = _mm_add_ps(col2, _mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f));
	col2 = _mm_mul_ps(col2, _mm_set1_ps(scale.z));
	_mm_storeu_ps(cols[2].data, col2);
#undef SHUF
#else
	mat3 m = muglm::mat3_cast(q);
	cols[0] = vec4(m[0] * scale.x, 0.0f);
	cols[1] = vec4(m[1] * scale.y, 0.0f);
	cols[2] = vec4(m[2] * scale.z, 0.0f);
#endif
}
}
}