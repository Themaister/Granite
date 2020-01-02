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

#include "muglm_impl.hpp"
#include "matrix_helper.hpp"
#include <assert.h>
#include <stdlib.h>

using namespace muglm;

#define MATH_ASSERT(x) do { \
	if (!bool(x)) \
		abort(); \
} while(0)

template <typename T>
static void assert_equal_epsilon(const T &a, const T &b, float epsilon = 0.0001f)
{
	MATH_ASSERT(all(lessThanEqual(abs(a - b), T(epsilon))));
}

template <typename T>
static void assert_equal(const T &a, const T &b)
{
	MATH_ASSERT(all(equal(a, b)));
}

static void assert_equal_epsilon(const mat2 &a, const mat2 &b, float epsilon = 0.0001f)
{
	assert_equal_epsilon(a[0], b[0], epsilon);
	assert_equal_epsilon(a[1], b[1], epsilon);
}

static void assert_equal_epsilon(const mat3 &a, const mat3 &b, float epsilon = 0.0001f)
{
	assert_equal_epsilon(a[0], b[0], epsilon);
	assert_equal_epsilon(a[1], b[1], epsilon);
	assert_equal_epsilon(a[2], b[2], epsilon);
}

static void assert_equal_epsilon(const mat4 &a, const mat4 &b, float epsilon = 0.0001f)
{
	assert_equal_epsilon(a[0], b[0], epsilon);
	assert_equal_epsilon(a[1], b[1], epsilon);
	assert_equal_epsilon(a[2], b[2], epsilon);
	assert_equal_epsilon(a[3], b[3], epsilon);
}

static void assert_equal(const mat2 &a, const mat2 &b)
{
	assert_equal_epsilon(a, b, 0.0f);
}

static void assert_equal(const mat3 &a, const mat3 &b)
{
	assert_equal_epsilon(a, b, 0.0f);
}

static void assert_equal(const mat4 &a, const mat4 &b)
{
	assert_equal_epsilon(a, b, 0.0f);
}

static void test_mat2()
{
	mat2 m(vec2(2.0f, 0.5f), vec2(8.0f, 4.0f));
	mat2 inv_m = inverse(m);
	mat2 mul0 = m * inv_m;
	mat2 mul1 = inv_m * m;
	assert_equal_epsilon(mul0, mat2(1.0f));
	assert_equal_epsilon(mul1, mat2(1.0f));
	assert_equal(transpose(transpose(m)), m);
}

static void test_mat3()
{
	mat3 m(vec3(2.0f, 0.5f, -3.0f), vec3(8.0f, 4.0f, 0.25f), vec3(-20.0f, 5.0f, 1.0f));
	mat3 inv_m = inverse(m);
	mat3 mul0 = m * inv_m;
	mat3 mul1 = inv_m * m;
	assert_equal_epsilon(mul0, mat3(1.0f));
	assert_equal_epsilon(mul1, mat3(1.0f));
	assert_equal(transpose(transpose(m)), m);
}

static void test_mat4()
{
	mat4 m(vec4(2.0f, 0.5f, -3.0f, 1.0f), vec4(8.0f, 4.0f, 0.25f, 8.0f), vec4(8.0f, -20.0f, 5.0f, 1.0f), vec4(0.0f, 1.0f, 2.0f, 3.0f));
	mat4 inv_m = inverse(m);
	mat4 mul0 = m * inv_m;
	mat4 mul1 = inv_m * m;
	assert_equal_epsilon(mul0, mat4(1.0f));
	assert_equal_epsilon(mul1, mat4(1.0f));
	assert_equal(transpose(transpose(m)), m);
}

static void test_quat()
{
	// X
	{
		quat q = angleAxis(half_pi<float>(), vec3(0.0f, 0.0f, 1.0f));
		vec3 y = q * vec3(1.0f, 0.0f, 0.0f);
		assert_equal_epsilon(y, vec3(0.0f, 1.0f, 0.0f));

		quat half_q = angleAxis(0.5f * half_pi<float>(), vec3(0.0f, 0.0f, 1.0f));
		q = half_q * half_q;
		y = q * vec3(1.0f, 0.0f, 0.0f);
		assert_equal_epsilon(y, vec3(0.0f, 1.0f, 0.0f));
	}

	// Y
	{
		quat q = angleAxis(half_pi<float>(), vec3(0.0f, 1.0f, 0.0f));
		vec3 y = q * vec3(1.0f, 0.0f, 0.0f);
		assert_equal_epsilon(y, vec3(0.0f, 0.0f, -1.0f));

		quat half_q = angleAxis(0.5f * half_pi<float>(), vec3(0.0f, 1.0f, 0.0f));
		q = half_q * half_q;
		y = q * vec3(1.0f, 0.0f, 0.0f);
		assert_equal_epsilon(y, vec3(0.0f, 0.0f, -1.0f));
	}

	assert_equal_epsilon(mat3_cast(quat(1.0f, 0.0f, 0.0f, 0.0f)), mat3(1.0f));
	assert_equal_epsilon(mat4_cast(quat(1.0f, 0.0f, 0.0f, 0.0f)), mat4(1.0f));
}

static void test_decompose()
{
	vec3 scaling(4.0f, -3.0f, 2.0f);
	quat rotate = angleAxis(0.543f, vec3(0.1f, 0.2f, -0.9f));
	vec3 trans(5.0f, 4.0f, 2.0f);

	mat4 original = translate(trans) * mat4_cast(rotate) * scale(scaling);

	vec3 s, t;
	quat r;
	decompose(original, s, r, t);

	mat4 reconstructed = translate(t) * mat4_cast(r) * scale(s);
	assert_equal_epsilon(original, reconstructed);
}

int main()
{
	test_mat2();
	test_mat3();
	test_mat4();
	test_quat();
	test_decompose();
}