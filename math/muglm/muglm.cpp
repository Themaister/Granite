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

#include "matrix_helper.hpp"
#include "muglm_impl.hpp"

namespace muglm
{
mat3 mat3_cast(const quat &q_)
{
	auto &q = q_.as_vec4();

	mat3 res(1.0f);
	float qxx = q.x * q.x;
	float qyy = q.y * q.y;
	float qzz = q.z * q.z;
	float qxz = q.x * q.z;
	float qxy = q.x * q.y;
	float qyz = q.y * q.z;
	float qwx = q.w * q.x;
	float qwy = q.w * q.y;
	float qwz = q.w * q.z;

	res[0][0] = 1.0f - 2.0f * (qyy + qzz);
	res[0][1] = 2.0f * (qxy + qwz);
	res[0][2] = 2.0f * (qxz - qwy);

	res[1][0] = 2.0f * (qxy - qwz);
	res[1][1] = 1.0f - 2.0f * (qxx + qzz);
	res[1][2] = 2.0f * (qyz + qwx);

	res[2][0] = 2.0f * (qxz + qwy);
	res[2][1] = 2.0f * (qyz - qwx);
	res[2][2] = 1.0f - 2.0f * (qxx + qyy);

	return res;
}

mat4 mat4_cast(const quat &q)
{
	return mat4(mat3_cast(q));
}

mat4 translate(const vec3 &v)
{
	return mat4(
			vec4(1.0f, 0.0f, 0.0f, 0.0f),
			vec4(0.0f, 1.0f, 0.0f, 0.0f),
			vec4(0.0f, 0.0f, 1.0f, 0.0f),
			vec4(v, 1.0f));
}

mat4 scale(const vec3 &v)
{
	return mat4(
			vec4(v.x, 0.0f, 0.0f, 0.0f),
			vec4(0.0f, v.y, 0.0f, 0.0f),
			vec4(0.0f, 0.0f, v.z, 0.0f),
			vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

mat2 inverse(const mat2 &m)
{
	float OneOverDeterminant = 1.0f / (m[0][0] * m[1][1] - m[1][0] * m[0][1]);

	mat2 Inverse(
			vec2(m[1][1] * OneOverDeterminant,
			     -m[0][1] * OneOverDeterminant),
			vec2(-m[1][0] * OneOverDeterminant,
			     m[0][0] * OneOverDeterminant));

	return Inverse;
}

mat3 inverse(const mat3 &m)
{
	float OneOverDeterminant = 1.0f / (
			m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2])
			- m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2])
			+ m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]));

	mat3 Inverse;
	Inverse[0][0] = +(m[1][1] * m[2][2] - m[2][1] * m[1][2]) * OneOverDeterminant;
	Inverse[1][0] = -(m[1][0] * m[2][2] - m[2][0] * m[1][2]) * OneOverDeterminant;
	Inverse[2][0] = +(m[1][0] * m[2][1] - m[2][0] * m[1][1]) * OneOverDeterminant;
	Inverse[0][1] = -(m[0][1] * m[2][2] - m[2][1] * m[0][2]) * OneOverDeterminant;
	Inverse[1][1] = +(m[0][0] * m[2][2] - m[2][0] * m[0][2]) * OneOverDeterminant;
	Inverse[2][1] = -(m[0][0] * m[2][1] - m[2][0] * m[0][1]) * OneOverDeterminant;
	Inverse[0][2] = +(m[0][1] * m[1][2] - m[1][1] * m[0][2]) * OneOverDeterminant;
	Inverse[1][2] = -(m[0][0] * m[1][2] - m[1][0] * m[0][2]) * OneOverDeterminant;
	Inverse[2][2] = +(m[0][0] * m[1][1] - m[1][0] * m[0][1]) * OneOverDeterminant;

	return Inverse;
}

mat4 inverse(const mat4 &m)
{
	float Coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
	float Coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
	float Coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

	float Coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
	float Coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
	float Coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

	float Coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
	float Coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
	float Coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

	float Coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
	float Coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
	float Coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

	float Coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
	float Coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
	float Coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

	float Coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
	float Coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
	float Coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

	vec4 Fac0(Coef00, Coef00, Coef02, Coef03);
	vec4 Fac1(Coef04, Coef04, Coef06, Coef07);
	vec4 Fac2(Coef08, Coef08, Coef10, Coef11);
	vec4 Fac3(Coef12, Coef12, Coef14, Coef15);
	vec4 Fac4(Coef16, Coef16, Coef18, Coef19);
	vec4 Fac5(Coef20, Coef20, Coef22, Coef23);

	vec4 Vec0(m[1][0], m[0][0], m[0][0], m[0][0]);
	vec4 Vec1(m[1][1], m[0][1], m[0][1], m[0][1]);
	vec4 Vec2(m[1][2], m[0][2], m[0][2], m[0][2]);
	vec4 Vec3(m[1][3], m[0][3], m[0][3], m[0][3]);

	vec4 Inv0(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2);
	vec4 Inv1(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4);
	vec4 Inv2(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5);
	vec4 Inv3(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5);

	vec4 SignA(+1, -1, +1, -1);
	vec4 SignB(-1, +1, -1, +1);
	mat4 Inverse(Inv0 * SignA, Inv1 * SignB, Inv2 * SignA, Inv3 * SignB);

	vec4 Row0(Inverse[0][0], Inverse[1][0], Inverse[2][0], Inverse[3][0]);

	vec4 Dot0(m[0] * Row0);
	float Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w);

	float OneOverDeterminant = 1.0f / Dot1;

	return Inverse * OneOverDeterminant;
}

void decompose(const mat4 &m, vec3 &scale, quat &rotation, vec3 &trans)
{
	vec4 rot;

	// Make a lot of assumptions.
	// We don't need skew, nor perspective.

	// Isolate translation.
	trans = m[3].xyz();

	vec3 cols[3];
	cols[0] = m[0].xyz();
	cols[1] = m[1].xyz();
	cols[2] = m[2].xyz();

	scale.x = length(cols[0]);
	scale.y = length(cols[1]);
	scale.z = length(cols[2]);

	// Isolate scale.
	cols[0] /= scale.x;
	cols[1] /= scale.y;
	cols[2] /= scale.z;

	vec3 pdum3 = cross(cols[1], cols[2]);
	if (dot(cols[0], pdum3) < 0.0f)
	{
		scale = -scale;
		cols[0] = -cols[0];
		cols[1] = -cols[1];
		cols[2] = -cols[2];
	}

	int i, j, k = 0;
	float root, trace = cols[0].x + cols[1].y + cols[2].z;
	if (trace > 0.0f)
	{
		root = sqrt(trace + 1.0f);
		rot.w = 0.5f * root;
		root = 0.5f / root;
		rot.x = root * (cols[1].z - cols[2].y);
		rot.y = root * (cols[2].x - cols[0].z);
		rot.z = root * (cols[0].y - cols[1].x);
	}
	else
	{
		static const int Next[3] = {1, 2, 0};

		i = 0;
		if (cols[1].y > cols[0].x) i = 1;
		if (cols[2].z > cols[i][i]) i = 2;

		j = Next[i];
		k = Next[j];

		root = sqrt(cols[i][i] - cols[j][j] - cols[k][k] + 1.0f);

		rot[i] = 0.5f * root;
		root = 0.5f / root;
		rot[j] = root * (cols[i][j] + cols[j][i]);
		rot[k] = root * (cols[i][k] + cols[k][i]);
		rot.w = root * (cols[j][k] - cols[k][j]);
	}

	rotation = quat(rot);
}

mat4 ortho(float left, float right, float bottom, float top, float near, float far)
{
	mat4 result(1.0f);
	result[0][0] = 2.0f / (right - left);
	result[1][1] = 2.0f / (top - bottom);
	result[2][2] = -1.0f / (far - near);
	result[3][0] = -(right + left) / (right - left);
	result[3][1] = -(top + bottom) / (top - bottom);
	result[3][2] = -near / (far - near);

	result[0].y *= -1.0f;
	result[1].y *= -1.0f;
	result[2].y *= -1.0f;
	result[3].y *= -1.0f;

	return result;
}

mat4 frustum(float left, float right, float bottom, float top, float near, float far)
{
	mat4 result(0.0f);
	result[0][0] = (2.0f * near) / (right - left);
	result[1][1] = (2.0f * near) / (top - bottom);
	result[2][0] = (right + left) / (right - left);
	result[2][1] = (top + bottom) / (top - bottom);
	result[2][2] = far / (near - far);
	result[2][3] = -1.0f;
	result[3][2] = -(far * near) / (far - near);

	result[0].y *= -1.0f;
	result[1].y *= -1.0f;
	result[2].y *= -1.0f;
	result[3].y *= -1.0f;

	return result;
}

mat4 perspective(float fovy, float aspect, float near, float far)
{
	float tanHalfFovy = tan(fovy / 2.0f);

	mat4 result(0.0f);
	result[0][0] = 1.0f / (aspect * tanHalfFovy);
	result[1][1] = 1.0f / (tanHalfFovy);
	result[2][2] = far / (near - far);
	result[2][3] = -1.0f;
	result[3][2] = -(far * near) / (far - near);

	result[0].y *= -1.0f;
	result[1].y *= -1.0f;
	result[2].y *= -1.0f;
	result[3].y *= -1.0f;

	return result;
}
}