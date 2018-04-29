/* Copyright (c) 2017 Hans-Kristian Arntzen
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

#include "muglm.hpp"
#include <cmath>

namespace muglm
{
// arithmetic operations
#define MUGLM_DEFINE_ARITH_OP(op) \
template <typename T> inline tvec2<T> operator op(const tvec2<T> &a, const tvec2<T> &b) { return tvec2<T>(a.x op b.x, a.y op b.y); } \
template <typename T> inline tvec3<T> operator op(const tvec3<T> &a, const tvec3<T> &b) { return tvec3<T>(a.x op b.x, a.y op b.y, a.z op b.z); } \
template <typename T> inline tvec4<T> operator op(const tvec4<T> &a, const tvec4<T> &b) { return tvec4<T>(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w); } \
template <typename T> inline tvec2<T> operator op(const tvec2<T> &a, T b) { return tvec2<T>(a.x op b, a.y op b); } \
template <typename T> inline tvec3<T> operator op(const tvec3<T> &a, T b) { return tvec3<T>(a.x op b, a.y op b, a.z op b); } \
template <typename T> inline tvec4<T> operator op(const tvec4<T> &a, T b) { return tvec4<T>(a.x op b, a.y op b, a.z op b, a.w op b); } \
template <typename T> inline tvec2<T> operator op(T a, const tvec2<T> &b) { return tvec2<T>(a op b.x, a op b.y); } \
template <typename T> inline tvec3<T> operator op(T a, const tvec3<T> &b) { return tvec3<T>(a op b.x, a op b.y, a op b.z); } \
template <typename T> inline tvec4<T> operator op(T a, const tvec4<T> &b) { return tvec4<T>(a op b.x, a op b.y, a op b.z, a op b.w); }
MUGLM_DEFINE_ARITH_OP(+)
MUGLM_DEFINE_ARITH_OP(-)
MUGLM_DEFINE_ARITH_OP(*)
MUGLM_DEFINE_ARITH_OP(/)
MUGLM_DEFINE_ARITH_OP(^)
MUGLM_DEFINE_ARITH_OP(&)
MUGLM_DEFINE_ARITH_OP(|)

#define MUGLM_DEFINE_MATRIX_SCALAR_OP(op) \
template <typename T> inline tmat2<T> operator op(const tmat2<T> &m, T s) { return tmat2<T>(m[0] op s, m[1] op s); } \
template <typename T> inline tmat3<T> operator op(const tmat3<T> &m, T s) { return tmat3<T>(m[0] op s, m[1] op s, m[2] op s); } \
template <typename T> inline tmat4<T> operator op(const tmat4<T> &m, T s) { return tmat4<T>(m[0] op s, m[1] op s, m[2] op s, m[3] op s); }
MUGLM_DEFINE_MATRIX_SCALAR_OP(+)
MUGLM_DEFINE_MATRIX_SCALAR_OP(-)
MUGLM_DEFINE_MATRIX_SCALAR_OP(*)
MUGLM_DEFINE_MATRIX_SCALAR_OP(/)

#define MUGLM_DEFINE_BOOL_OP(bop, op) \
template <typename T> inline bvec2 bop(const tvec2<T> &a, const tvec2<T> &b) { return bvec2(a.x op b.x, a.y op b.y); } \
template <typename T> inline bvec3 bop(const tvec3<T> &a, const tvec3<T> &b) { return bvec3(a.x op b.x, a.y op b.y, a.z op b.z); } \
template <typename T> inline bvec4 bop(const tvec4<T> &a, const tvec4<T> &b) { return bvec4(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w); }
MUGLM_DEFINE_BOOL_OP(notEqual, !=)
MUGLM_DEFINE_BOOL_OP(equal, ==)
MUGLM_DEFINE_BOOL_OP(lessThan, <)
MUGLM_DEFINE_BOOL_OP(lessThanEqual, <=)
MUGLM_DEFINE_BOOL_OP(greaterThan, >)
MUGLM_DEFINE_BOOL_OP(greaterThanEqual, >=)

inline bool any(const bvec2 &v) { return v.x || v.y; }
inline bool any(const bvec3 &v) { return v.x || v.y || v.z; }
inline bool any(const bvec4 &v) { return v.x || v.y || v.z || v.w; }
inline bool all(const bvec2 &v) { return v.x && v.y; }
inline bool all(const bvec3 &v) { return v.x && v.y && v.z; }
inline bool all(const bvec4 &v) { return v.x && v.y && v.z && v.w; }

template <typename T> inline tvec2<T> operator-(const tvec2<T> &v) { return tvec2<T>(-v.x, -v.y); }
template <typename T> inline tvec3<T> operator-(const tvec3<T> &v) { return tvec3<T>(-v.x, -v.y, -v.z); }
template <typename T> inline tvec4<T> operator-(const tvec4<T> &v) { return tvec4<T>(-v.x, -v.y, -v.z, -v.w); }
template <typename T> inline tvec2<T> operator~(const tvec2<T> &v) { return tvec2<T>(~v.x, ~v.y); }
template <typename T> inline tvec3<T> operator~(const tvec3<T> &v) { return tvec3<T>(~v.x, ~v.y, ~v.z); }
template <typename T> inline tvec4<T> operator~(const tvec4<T> &v) { return tvec4<T>(~v.x, ~v.y, ~v.z, ~v.w); }

// arithmetic operations, inplace modify
#define MUGLM_DEFINE_ARITH_MOD_OP(op) \
template <typename T> inline tvec2<T> &operator op(tvec2<T> &a, const tvec2<T> &b) { a.x op b.x; a.y op b.y; return a; } \
template <typename T> inline tvec3<T> &operator op(tvec3<T> &a, const tvec3<T> &b) { a.x op b.x; a.y op b.y; a.z op b.z; return a; } \
template <typename T> inline tvec4<T> &operator op(tvec4<T> &a, const tvec4<T> &b) { a.x op b.x; a.y op b.y; a.z op b.z; a.w op b.w; return a; } \
template <typename T> inline tvec2<T> &operator op(tvec2<T> &a, T b) { a.x op b; a.y op b; return a; } \
template <typename T> inline tvec3<T> &operator op(tvec3<T> &a, T b) { a.x op b; a.y op b; a.z op b; return a; } \
template <typename T> inline tvec4<T> &operator op(tvec4<T> &a, T b) { a.x op b; a.y op b; a.z op b; a.w op b; return a; }
MUGLM_DEFINE_ARITH_MOD_OP(+=)
MUGLM_DEFINE_ARITH_MOD_OP(-=)
MUGLM_DEFINE_ARITH_MOD_OP(*=)
MUGLM_DEFINE_ARITH_MOD_OP(/=)
MUGLM_DEFINE_ARITH_MOD_OP(^=)
MUGLM_DEFINE_ARITH_MOD_OP(&=)
MUGLM_DEFINE_ARITH_MOD_OP(|=)

// matrix multiply
inline vec2 operator*(const mat2 &m, const vec2 &v)
{
	return m[0] * v.x + m[1] * v.y;
}

inline vec3 operator*(const mat3 &m, const vec3 &v)
{
	return m[0] * v.x + m[1] * v.y + m[2] * v.z;
}

inline vec4 operator*(const mat4 &m, const vec4 &v)
{
	return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w;
}

inline mat2 operator*(const mat2 &a, const mat2 &b)
{
	return mat2(a * b[0], a * b[1]);
}

inline mat3 operator*(const mat3 &a, const mat3 &b)
{
	return mat3(a * b[0], a * b[1], a * b[2]);
}

inline mat4 operator*(const mat4 &a, const mat4 &b)
{
	return mat4(a * b[0], a * b[1], a * b[2], a * b[3]);
}

inline mat2 transpose(const mat2 &m)
{
	return mat2(vec2(m[0].x, m[1].x),
	            vec2(m[0].y, m[1].y));
}

inline mat3 transpose(const mat3 &m)
{
	return mat3(vec3(m[0].x, m[1].x, m[2].x),
	            vec3(m[0].y, m[1].y, m[2].y),
	            vec3(m[0].z, m[1].z, m[2].z));
}

inline mat4 transpose(const mat4 &m)
{
	return mat4(vec4(m[0].x, m[1].x, m[2].x, m[3].x),
	            vec4(m[0].y, m[1].y, m[2].y, m[3].y),
	            vec4(m[0].z, m[1].z, m[2].z, m[3].z),
	            vec4(m[0].w, m[1].w, m[2].w, m[3].w));
}

// dot
inline float dot(const vec2 &a, const vec2 &b) { return a.x * b.x + a.y * b.y; }
inline float dot(const vec3 &a, const vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float dot(const vec4 &a, const vec4 &b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// min, max, clamp
template <typename T> T min(T a, T b) { return b < a ? b : a; }
template <typename T> T max(T a, T b) { return a < b ? b : a; }
template <typename T> T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> T sign(T v) { return v < T(0) ? T(-1) : (v > T(0) ? T(1) : T(0)); }
template <typename T> T sin(T v) { return std::sin(v); }
template <typename T> T cos(T v) { return std::cos(v); }
template <typename T> T tan(T v) { return std::tan(v); }
template <typename T> T asin(T v) { return std::asin(v); }
template <typename T> T acos(T v) { return std::acos(v); }
template <typename T> T atan(T v) { return std::atan(v); }
template <typename T> T log2(T v) { return std::log2(v); }
template <typename T> T log10(T v) { return std::log10(v); }
template <typename T> T log(T v) { return std::log(v); }
template <typename T> T exp2(T v) { return std::exp2(v); }
template <typename T> T exp(T v) { return std::exp(v); }

#define MUGLM_VECTORIZED_FUNC1(func) \
template <typename T> inline tvec2<T> func(const tvec2<T> &a) { return tvec2<T>(func(a.x), func(a.y)); } \
template <typename T> inline tvec3<T> func(const tvec3<T> &a) { return tvec3<T>(func(a.x), func(a.y), func(a.z)); } \
template <typename T> inline tvec4<T> func(const tvec4<T> &a) { return tvec4<T>(func(a.x), func(a.y), func(a.z), func(a.w)); }

#define MUGLM_VECTORIZED_FUNC2(func) \
template <typename T> inline tvec2<T> func(const tvec2<T> &a, const tvec2<T> &b) { return tvec2<T>(func(a.x, b.x), func(a.y, b.y)); } \
template <typename T> inline tvec3<T> func(const tvec3<T> &a, const tvec3<T> &b) { return tvec3<T>(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z)); } \
template <typename T> inline tvec4<T> func(const tvec4<T> &a, const tvec4<T> &b) { return tvec4<T>(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z), func(a.w, b.w)); }

#define MUGLM_VECTORIZED_FUNC3(func) \
template <typename T> inline tvec2<T> func(const tvec2<T> &a, const tvec2<T> &b, const tvec2<T> &c) { return tvec2<T>(func(a.x, b.x, c.x), func(a.y, b.y, c.y)); } \
template <typename T> inline tvec3<T> func(const tvec3<T> &a, const tvec3<T> &b, const tvec3<T> &c) { return tvec3<T>(func(a.x, b.x, c.x), func(a.y, b.y, c.y), func(a.z, b.z, c.z)); } \
template <typename T> inline tvec4<T> func(const tvec4<T> &a, const tvec4<T> &b, const tvec4<T> &c) { return tvec4<T>(func(a.x, b.x, c.x), func(a.y, b.y, c.y), func(a.z, b.z, c.z), func(a.w, b.w, c.w)); }

MUGLM_VECTORIZED_FUNC1(sign)
MUGLM_VECTORIZED_FUNC1(sin)
MUGLM_VECTORIZED_FUNC1(cos)
MUGLM_VECTORIZED_FUNC1(tan)
MUGLM_VECTORIZED_FUNC1(asin)
MUGLM_VECTORIZED_FUNC1(acos)
MUGLM_VECTORIZED_FUNC1(atan)
MUGLM_VECTORIZED_FUNC1(log2)
MUGLM_VECTORIZED_FUNC1(log10)
MUGLM_VECTORIZED_FUNC1(log)
MUGLM_VECTORIZED_FUNC1(exp2)
MUGLM_VECTORIZED_FUNC1(exp)
MUGLM_VECTORIZED_FUNC2(min)
MUGLM_VECTORIZED_FUNC2(max)
MUGLM_VECTORIZED_FUNC3(clamp)

// mix
template <typename T, typename Lerp> inline T mix(const T &a, const T &b, const Lerp &lerp) { return a + (b - a) * lerp; }

// cross
inline vec3 cross(const vec3 &a, const vec3 &b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// sqrt
inline float sqrt(float v) { return std::sqrt(v); }
MUGLM_VECTORIZED_FUNC1(sqrt)

// floor
inline float floor(float v) { return std::floor(v); }
MUGLM_VECTORIZED_FUNC1(floor)

// ceil
inline float ceil(float v) { return std::ceil(v); }
MUGLM_VECTORIZED_FUNC1(ceil)

// round
inline float round(float v) { return std::round(v); }
MUGLM_VECTORIZED_FUNC1(round)

// abs
template <typename T>
inline T abs(T v) { return std::abs(v); }
MUGLM_VECTORIZED_FUNC1(abs)

inline uint floatBitsToUint(float v)
{
	union { float f32; uint u32; } u;
	u.f32 = v;
	return u.u32;
}
MUGLM_VECTORIZED_FUNC1(floatBitsToUint)

inline float uintBitsToFloat(uint v)
{
	union { float f32; uint u32; } u;
	u.u32 = v;
	return u.f32;
}
MUGLM_VECTORIZED_FUNC1(uintBitsToFloat)

inline uint16_t packHalf1x16(float v)
{
	uint f = floatBitsToUint(v);
	return uint16_t(((f >> 16) & 0x8000) | // sign
	                ((((f & 0x7f800000) - 0x38000000) >> 13) & 0x7c00) | // exponential
	                ((f >> 13) & 0x03ff)); // Mantissa
}

inline uint packHalf2x16(const vec2 &v)
{
	uint lo = packHalf1x16(v.x);
	uint hi = packHalf1x16(v.y);
	return lo | (hi << 16u);
}

// inversesqrt
template <typename T> inline T inversesqrt(const T &v) { return T(1) / sqrt(v); }

// normalize
template <typename T> inline T normalize(const T &v) { return v * inversesqrt(dot(v, v)); }
inline quat normalize(const quat &q) { return quat(normalize(q.as_vec4())); }

// length
template <typename T> inline float length(const T &v) { return sqrt(dot(v, v)); }

// distance
template <typename T> inline float distance(const T &a, const T &b) { return length(a - b); }

// quaternions
inline vec3 operator*(const quat &q, const vec3 &v)
{
	vec3 quat_vector = q.as_vec4().xyz();
	vec3 uv = cross(quat_vector, v);
	vec3 uuv = cross(quat_vector, uv);
	return v + ((uv * q.w) + uuv) * 2.0f;
}

inline quat operator*(const quat &p, const quat &q)
{
	float w = p.w * q.w - p.x * q.x - p.y * q.y - p.z * q.z;
	float x = p.w * q.x + p.x * q.w + p.y * q.z - p.z * q.y;
	float y = p.w * q.y + p.y * q.w + p.z * q.x - p.x * q.z;
	float z = p.w * q.z + p.z * q.w + p.x * q.y - p.y * q.x;
	return quat(w, x, y, z);
}

inline quat slerp(const quat &x, const quat &y, float l)
{
	quat z = y;
	float cos_theta = dot(x.as_vec4(), y.as_vec4());
	if (cos_theta < 0.0f)
	{
		z = quat(-y.as_vec4());
		cos_theta = -cos_theta;
	}

	if (cos_theta > 0.999f)
		return quat(mix(x.as_vec4(), z.as_vec4(), l));

	float angle = acos(cos_theta);

	auto &vz = z.as_vec4();
	auto &vx = x.as_vec4();
	auto res = (sin((1.0f - l) * angle) * vx + sin(l * angle) * vz) / sin(angle);
	return quat(res);
}

inline quat angleAxis(float angle, const vec3 &axis)
{
	return quat(cos(0.5f * angle), sin(0.5f * angle) * normalize(axis));
}

inline quat conjugate(const quat &q)
{
	return quat(q.w, -q.x, -q.y, -q.z);
}

}
