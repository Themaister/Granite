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

#include "muglm.hpp"

namespace muglm
{
mat4 mat4_cast(const quat &q);
mat3 mat3_cast(const quat &q);
mat4 translate(const vec3 &v);
mat4 scale(const vec3 &v);
mat4 frustum(float left, float right, float bottom, float top, float near, float far);
mat2 inverse(const mat2 &m);
mat3 inverse(const mat3 &m);
mat4 inverse(const mat4 &m);
mat4 perspective(float fovy, float aspect, float near, float far);
mat4 ortho(float left, float right, float bottom, float top, float near, float far);

void decompose(const mat4 &m, vec3 &scale, quat &rot, vec3 &trans);
}