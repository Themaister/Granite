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
#include <cmath>

namespace muglm
{
#define MUGLM_IMPL_SWIZZLE(ret_type, self_type, swiz, ...) template <typename T> t##ret_type<T> t##self_type<T>::swiz() const { return t##ret_type<T>(__VA_ARGS__); }

// vec2
MUGLM_IMPL_SWIZZLE(vec2, vec2, xx, x, x)
MUGLM_IMPL_SWIZZLE(vec2, vec2, xy, x, y)
MUGLM_IMPL_SWIZZLE(vec2, vec2, yx, y, x)
MUGLM_IMPL_SWIZZLE(vec2, vec2, yy, y, y)

MUGLM_IMPL_SWIZZLE(vec3, vec2, xxx, x, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec2, xxy, x, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec2, xyx, x, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec2, xyy, x, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec2, yxx, y, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec2, yxy, y, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec2, yyx, y, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec2, yyy, y, y, y)

MUGLM_IMPL_SWIZZLE(vec4, vec2, xxxx, x, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xxxy, x, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xxyx, x, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xxyy, x, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xyxx, x, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xyxy, x, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xyyx, x, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, xyyy, x, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yxxx, y, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yxxy, y, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yxyx, y, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yxyy, y, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yyxx, y, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yyxy, y, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yyyx, y, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec2, yyyy, y, y, y, y)

// vec3
MUGLM_IMPL_SWIZZLE(vec2, vec3, xx, x, x)
MUGLM_IMPL_SWIZZLE(vec2, vec3, xy, x, y)
MUGLM_IMPL_SWIZZLE(vec2, vec3, xz, x, z)
MUGLM_IMPL_SWIZZLE(vec2, vec3, yx, y, x)
MUGLM_IMPL_SWIZZLE(vec2, vec3, yy, y, y)
MUGLM_IMPL_SWIZZLE(vec2, vec3, yz, y, z)
MUGLM_IMPL_SWIZZLE(vec2, vec3, zx, z, x)
MUGLM_IMPL_SWIZZLE(vec2, vec3, zy, z, y)
MUGLM_IMPL_SWIZZLE(vec2, vec3, zz, z, z)

MUGLM_IMPL_SWIZZLE(vec3, vec3, xxx, x, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xxy, x, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xxz, x, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xyx, x, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xyy, x, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xyz, x, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xzx, x, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xzy, x, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, xzz, x, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yxx, y, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yxy, y, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yxz, y, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yyx, y, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yyy, y, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yyz, y, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yzx, y, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yzy, y, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, yzz, y, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zxx, z, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zxy, z, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zxz, z, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zyx, z, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zyy, z, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zyz, z, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zzx, z, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zzy, z, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec3, zzz, z, z, z)

MUGLM_IMPL_SWIZZLE(vec4, vec3, xxxx, x, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxxy, x, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxxz, x, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxyx, x, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxyy, x, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxyz, x, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxzx, x, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxzy, x, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xxzz, x, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyxx, x, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyxy, x, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyxz, x, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyyx, x, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyyy, x, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyyz, x, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyzx, x, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyzy, x, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xyzz, x, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzxx, x, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzxy, x, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzxz, x, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzyx, x, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzyy, x, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzyz, x, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzzx, x, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzzy, x, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, xzzz, x, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxxx, y, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxxy, y, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxxz, y, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxyx, y, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxyy, y, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxyz, y, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxzx, y, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxzy, y, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yxzz, y, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyxx, y, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyxy, y, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyxz, y, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyyx, y, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyyy, y, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyyz, y, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyzx, y, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyzy, y, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yyzz, y, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzxx, y, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzxy, y, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzxz, y, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzyx, y, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzyy, y, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzyz, y, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzzx, y, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzzy, y, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, yzzz, y, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxxx, z, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxxy, z, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxxz, z, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxyx, z, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxyy, z, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxyz, z, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxzx, z, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxzy, z, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zxzz, z, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyxx, z, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyxy, z, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyxz, z, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyyx, z, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyyy, z, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyyz, z, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyzx, z, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyzy, z, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zyzz, z, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzxx, z, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzxy, z, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzxz, z, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzyx, z, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzyy, z, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzyz, z, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzzx, z, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzzy, z, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec3, zzzz, z, z, z, z)

// vec4
MUGLM_IMPL_SWIZZLE(vec2, vec4, xx, x, x)
MUGLM_IMPL_SWIZZLE(vec2, vec4, xy, x, y)
MUGLM_IMPL_SWIZZLE(vec2, vec4, xz, x, z)
MUGLM_IMPL_SWIZZLE(vec2, vec4, xw, x, w)
MUGLM_IMPL_SWIZZLE(vec2, vec4, yx, y, x)
MUGLM_IMPL_SWIZZLE(vec2, vec4, yy, y, y)
MUGLM_IMPL_SWIZZLE(vec2, vec4, yz, y, z)
MUGLM_IMPL_SWIZZLE(vec2, vec4, yw, y, w)
MUGLM_IMPL_SWIZZLE(vec2, vec4, zx, z, x)
MUGLM_IMPL_SWIZZLE(vec2, vec4, zy, z, y)
MUGLM_IMPL_SWIZZLE(vec2, vec4, zz, z, z)
MUGLM_IMPL_SWIZZLE(vec2, vec4, zw, z, w)
MUGLM_IMPL_SWIZZLE(vec2, vec4, wx, w, x)
MUGLM_IMPL_SWIZZLE(vec2, vec4, wy, w, y)
MUGLM_IMPL_SWIZZLE(vec2, vec4, wz, w, z)
MUGLM_IMPL_SWIZZLE(vec2, vec4, ww, w, w)

MUGLM_IMPL_SWIZZLE(vec3, vec4, xxx, x, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xxy, x, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xxz, x, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xxw, x, x, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xyx, x, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xyy, x, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xyz, x, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xyw, x, y, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xzx, x, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xzy, x, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xzz, x, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xzw, x, z, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xwx, x, w, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xwy, x, w, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xwz, x, w, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, xww, x, w, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yxx, y, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yxy, y, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yxz, y, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yxw, y, x, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yyx, y, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yyy, y, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yyz, y, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yyw, y, y, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yzx, y, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yzy, y, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yzz, y, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yzw, y, z, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, ywx, y, w, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, ywy, y, w, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, ywz, y, w, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, yww, y, w, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zxx, z, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zxy, z, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zxz, z, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zxw, z, x, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zyx, z, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zyy, z, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zyz, z, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zyw, z, y, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zzx, z, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zzy, z, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zzz, z, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zzw, z, z, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zwx, z, w, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zwy, z, w, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zwz, z, w, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, zww, z, w, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wxx, w, x, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wxy, w, x, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wxz, w, x, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wxw, w, x, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wyx, w, y, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wyy, w, y, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wyz, w, y, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wyw, w, y, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wzx, w, z, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wzy, w, z, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wzz, w, z, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wzw, w, z, w)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wwx, w, w, x)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wwy, w, w, y)
MUGLM_IMPL_SWIZZLE(vec3, vec4, wwz, w, w, z)
MUGLM_IMPL_SWIZZLE(vec3, vec4, www, w, w, w)

MUGLM_IMPL_SWIZZLE(vec4, vec4, xxxx, x, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxxy, x, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxxz, x, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxxw, x, x, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxyx, x, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxyy, x, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxyz, x, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxyw, x, x, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxzx, x, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxzy, x, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxzz, x, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxzw, x, x, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxwx, x, x, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxwy, x, x, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxwz, x, x, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xxww, x, x, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyxx, x, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyxy, x, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyxz, x, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyxw, x, y, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyyx, x, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyyy, x, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyyz, x, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyyw, x, y, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyzx, x, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyzy, x, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyzz, x, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyzw, x, y, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xywx, x, y, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xywy, x, y, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xywz, x, y, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xyww, x, y, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzxx, x, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzxy, x, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzxz, x, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzxw, x, z, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzyx, x, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzyy, x, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzyz, x, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzyw, x, z, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzzx, x, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzzy, x, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzzz, x, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzzw, x, z, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzwx, x, z, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzwy, x, z, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzwz, x, z, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xzww, x, z, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwxx, x, w, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwxy, x, w, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwxz, x, w, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwxw, x, w, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwyx, x, w, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwyy, x, w, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwyz, x, w, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwyw, x, w, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwzx, x, w, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwzy, x, w, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwzz, x, w, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwzw, x, w, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwwx, x, w, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwwy, x, w, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwwz, x, w, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, xwww, x, w, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxxx, y, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxxy, y, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxxz, y, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxxw, y, x, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxyx, y, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxyy, y, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxyz, y, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxyw, y, x, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxzx, y, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxzy, y, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxzz, y, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxzw, y, x, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxwx, y, x, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxwy, y, x, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxwz, y, x, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yxww, y, x, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyxx, y, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyxy, y, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyxz, y, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyxw, y, y, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyyx, y, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyyy, y, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyyz, y, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyyw, y, y, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyzx, y, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyzy, y, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyzz, y, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyzw, y, y, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yywx, y, y, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yywy, y, y, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yywz, y, y, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yyww, y, y, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzxx, y, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzxy, y, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzxz, y, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzxw, y, z, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzyx, y, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzyy, y, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzyz, y, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzyw, y, z, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzzx, y, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzzy, y, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzzz, y, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzzw, y, z, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzwx, y, z, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzwy, y, z, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzwz, y, z, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, yzww, y, z, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywxx, y, w, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywxy, y, w, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywxz, y, w, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywxw, y, w, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywyx, y, w, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywyy, y, w, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywyz, y, w, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywyw, y, w, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywzx, y, w, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywzy, y, w, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywzz, y, w, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywzw, y, w, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywwx, y, w, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywwy, y, w, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywwz, y, w, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, ywww, y, w, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxxx, z, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxxy, z, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxxz, z, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxxw, z, x, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxyx, z, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxyy, z, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxyz, z, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxyw, z, x, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxzx, z, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxzy, z, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxzz, z, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxzw, z, x, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxwx, z, x, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxwy, z, x, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxwz, z, x, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zxww, z, x, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyxx, z, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyxy, z, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyxz, z, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyxw, z, y, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyyx, z, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyyy, z, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyyz, z, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyyw, z, y, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyzx, z, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyzy, z, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyzz, z, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyzw, z, y, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zywx, z, y, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zywy, z, y, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zywz, z, y, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zyww, z, y, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzxx, z, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzxy, z, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzxz, z, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzxw, z, z, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzyx, z, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzyy, z, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzyz, z, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzyw, z, z, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzzx, z, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzzy, z, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzzz, z, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzzw, z, z, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzwx, z, z, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzwy, z, z, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzwz, z, z, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zzww, z, z, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwxx, z, w, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwxy, z, w, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwxz, z, w, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwxw, z, w, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwyx, z, w, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwyy, z, w, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwyz, z, w, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwyw, z, w, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwzx, z, w, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwzy, z, w, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwzz, z, w, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwzw, z, w, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwwx, z, w, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwwy, z, w, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwwz, z, w, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, zwww, z, w, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxxx, w, x, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxxy, w, x, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxxz, w, x, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxxw, w, x, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxyx, w, x, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxyy, w, x, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxyz, w, x, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxyw, w, x, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxzx, w, x, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxzy, w, x, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxzz, w, x, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxzw, w, x, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxwx, w, x, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxwy, w, x, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxwz, w, x, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wxww, w, x, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyxx, w, y, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyxy, w, y, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyxz, w, y, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyxw, w, y, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyyx, w, y, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyyy, w, y, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyyz, w, y, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyyw, w, y, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyzx, w, y, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyzy, w, y, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyzz, w, y, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyzw, w, y, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wywx, w, y, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wywy, w, y, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wywz, w, y, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wyww, w, y, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzxx, w, z, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzxy, w, z, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzxz, w, z, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzxw, w, z, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzyx, w, z, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzyy, w, z, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzyz, w, z, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzyw, w, z, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzzx, w, z, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzzy, w, z, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzzz, w, z, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzzw, w, z, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzwx, w, z, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzwy, w, z, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzwz, w, z, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wzww, w, z, w, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwxx, w, w, x, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwxy, w, w, x, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwxz, w, w, x, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwxw, w, w, x, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwyx, w, w, y, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwyy, w, w, y, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwyz, w, w, y, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwyw, w, w, y, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwzx, w, w, z, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwzy, w, w, z, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwzz, w, w, z, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwzw, w, w, z, w)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwwx, w, w, w, x)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwwy, w, w, w, y)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwwz, w, w, w, z)
MUGLM_IMPL_SWIZZLE(vec4, vec4, wwww, w, w, w, w)

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
MUGLM_DEFINE_ARITH_OP(>>)
MUGLM_DEFINE_ARITH_OP(<<)

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
MUGLM_DEFINE_ARITH_MOD_OP(>>=)
MUGLM_DEFINE_ARITH_MOD_OP(<<=)

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
template <typename T> T pow(T a, T b) { return std::pow(a, b); }
template <typename T> T radians(T a) { return a * (T(pi<T>() / T(180))); }
template <typename T> T degrees(T a) { return a * (T(180) / pi<T>()); }

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
MUGLM_VECTORIZED_FUNC2(pow)
MUGLM_VECTORIZED_FUNC3(clamp)

// mix
template <typename T, typename Lerp> inline T mix(const T &a, const T &b, const Lerp &lerp) { return a * (1.0f - lerp) + b * lerp; }

template <typename T> inline T select(T a, T b, bool lerp)
{
	return lerp ? b : a;
}

template <typename T> inline tvec2<T> select(const tvec2<T> &a, const tvec2<T> &b, const tvec2<bool> &lerp)
{
	return tvec2<T>(lerp.x ? b.x : a.x, lerp.y ? b.y : a.y);
}

template <typename T> inline tvec3<T> select(const tvec3<T> &a, const tvec3<T> &b, const tvec3<bool> &lerp)
{
	return tvec3<T>(lerp.x ? b.x : a.x, lerp.y ? b.y : a.y, lerp.z ? b.z : a.z);
}

template <typename T> inline tvec4<T> select(const tvec4<T> &a, const tvec4<T> &b, const tvec4<bool> &lerp)
{
	return tvec4<T>(lerp.x ? b.x : a.x, lerp.y ? b.y : a.y, lerp.z ? b.z : a.z, lerp.w ? b.w : a.w);
}

// smoothstep
template <typename T> inline T smoothstep(const T &lo, const T &hi, T val)
{
	val = clamp((val - lo) / (hi - lo), T(0.0f), T(1.0f));
	return val * val * (3.0f - 2.0f * val);
}

// cross
inline vec3 cross(const vec3 &a, const vec3 &b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

// sqrt
inline float sqrt(float v) { return std::sqrt(v); }
MUGLM_VECTORIZED_FUNC1(sqrt)

// floor
inline float floor(float v) { return std::floor(v); }
MUGLM_VECTORIZED_FUNC1(floor)

// fract
template <typename T>
inline T fract(const T &v) { return v - floor(v); }

// ceil
inline float ceil(float v) { return std::ceil(v); }
MUGLM_VECTORIZED_FUNC1(ceil)

// round
inline float round(float v) { return std::round(v); }
MUGLM_VECTORIZED_FUNC1(round)

// mod
inline float mod(float x, float y) { return x - y * floor(x / y); }
MUGLM_VECTORIZED_FUNC2(mod)

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

inline float halfToFloat(uint16_t u16_value)
{
	// Based on the GLM implementation.
	int s = (u16_value >> 15) & 0x1;
	int e = (u16_value >> 10) & 0x1f;
	int m = (u16_value >> 0) & 0x3ff;

	union {
		float f32;
		uint32_t u32;
	} u;

	if (e == 0)
	{
		if (m == 0)
		{
			u.u32 = uint32_t(s) << 31;
			return u.f32;
		}
		else
		{
			while ((m & 0x400) == 0)
			{
				m <<= 1;
				e--;
			}

			e++;
			m &= ~0x400;
		}
	}
	else if (e == 31)
	{
		if (m == 0)
		{
			u.u32 = (uint32_t(s) << 31) | 0x7f800000u;
			return u.f32;
		}
		else
		{
			u.u32 = (uint32_t(s) << 31) | 0x7f800000u | (m << 13);
			return u.f32;
		}
	}

	e += 127 - 15;
	m <<= 13;
	u.u32 = (uint32_t(s) << 31) | (e << 23) | m;
	return u.f32;
}

inline vec2 halfToFloat(const u16vec2 &v)
{
	return vec2(halfToFloat(v.x), halfToFloat(v.y));
}

inline vec3 floatToHalf(const u16vec3 &v)
{
	return vec3(halfToFloat(v.x), halfToFloat(v.y), halfToFloat(v.z));
}

inline vec4 floatToHalf(const u16vec4 &v)
{
	return vec4(halfToFloat(v.x), halfToFloat(v.y), halfToFloat(v.z), halfToFloat(v.w));
}

inline uint16_t floatToHalf(float v)
{
	int i = floatBitsToUint(v);
	int s =  (i >> 16) & 0x00008000;
	int e = ((i >> 23) & 0x000000ff) - (127 - 15);
	int m =   i        & 0x007fffff;

	if (e <= 0)
	{
		if (e < -10)
			return uint16_t(s);

		m = (m | 0x00800000) >> (1 - e);

		if (m & 0x00001000)
			m += 0x00002000;

		return uint16_t(s | (m >> 13));
	}
	else if (e == 0xff - (127 - 15))
	{
		if (m == 0)
			return uint16_t(s | 0x7c00);
		else
		{
			m >>= 13;
			return uint16_t(s | 0x7c00 | m | (m == 0));
		}
	}
	else
	{
		if (m & 0x00001000)
		{
			m += 0x00002000;

			if (m & 0x00800000)
			{
				m =  0;
				e += 1;
			}
		}

		if (e > 30)
			return uint16_t(s | 0x7c00);

		return uint16_t(s | (e << 10) | (m >> 13));
	}
}

inline u16vec2 floatToHalf(const vec2 &v)
{
	return u16vec2(floatToHalf(v.x), floatToHalf(v.y));
}

inline u16vec3 floatToHalf(const vec3 &v)
{
	return u16vec3(floatToHalf(v.x), floatToHalf(v.y), floatToHalf(v.z));
}

inline u16vec4 floatToHalf(const vec4 &v)
{
	return u16vec4(floatToHalf(v.x), floatToHalf(v.y), floatToHalf(v.z), floatToHalf(v.w));
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

inline vec3 rotateX(const vec3 &v, float angle)
{
	return angleAxis(angle, vec3(1.0f, 0.0f, 0.0f)) * v;
}

inline vec3 rotateY(const vec3 &v, float angle)
{
	return angleAxis(angle, vec3(0.0f, 1.0f, 0.0f)) * v;
}

inline vec3 rotateZ(const vec3 &v, float angle)
{
	return angleAxis(angle, vec3(0.0f, 0.0f, 1.0f)) * v;
}

}
