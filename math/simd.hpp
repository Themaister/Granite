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

#pragma once

#include "math.hpp"
#include "aabb.hpp"
#include "simd_headers.hpp"

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
#error "Implement me."
#else
#error "Implement me."
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
#error "Implement me."
#else
	*c = (*a) * (*b);
#endif
}

}
}