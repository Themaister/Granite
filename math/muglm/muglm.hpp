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

#include <stdint.h>

namespace muglm
{
template <typename T> struct tvec2;
template <typename T> struct tvec3;
template <typename T> struct tvec4;
template <typename T> struct tmat2;
template <typename T> struct tmat3;
template <typename T> struct tmat4;

template <typename T>
struct tvec2
{
	tvec2() = default;
	tvec2(const tvec2 &) = default;

	explicit inline tvec2(T v)
	{
		x = v;
		y = v;
	}

	template <typename U>
	explicit inline tvec2(const tvec2<U> &u)
	{
		x = T(u.x);
		y = T(u.y);
	}

	inline tvec2(T x, T y)
	{
		this->x = x;
		this->y = y;
	}

	union
	{
		T data[2];
		struct
		{
			T x, y;
		};
	};

	inline T &operator[](int index)
	{
		return data[index];
	}

	inline const T &operator[](int index) const
	{
		return data[index];
	}

	inline tvec2 xx() const;
	inline tvec2 xy() const;
	inline tvec2 yx() const;
	inline tvec2 yy() const;

	inline tvec3<T> xxx() const;
	inline tvec3<T> xxy() const;
	inline tvec3<T> xyx() const;
	inline tvec3<T> xyy() const;
	inline tvec3<T> yxx() const;
	inline tvec3<T> yxy() const;
	inline tvec3<T> yyx() const;
	inline tvec3<T> yyy() const;

	inline tvec4<T> xxxx() const;
	inline tvec4<T> xxxy() const;
	inline tvec4<T> xxyx() const;
	inline tvec4<T> xxyy() const;
	inline tvec4<T> xyxx() const;
	inline tvec4<T> xyxy() const;
	inline tvec4<T> xyyx() const;
	inline tvec4<T> xyyy() const;
	inline tvec4<T> yxxx() const;
	inline tvec4<T> yxxy() const;
	inline tvec4<T> yxyx() const;
	inline tvec4<T> yxyy() const;
	inline tvec4<T> yyxx() const;
	inline tvec4<T> yyxy() const;
	inline tvec4<T> yyyx() const;
	inline tvec4<T> yyyy() const;
};

template <typename T>
struct tvec3
{
	tvec3() = default;
	tvec3(const tvec3 &) = default;

	template <typename U>
	explicit inline tvec3(const tvec3<U> &u)
	{
		x = T(u.x);
		y = T(u.y);
		z = T(u.z);
	}

	inline tvec3(const tvec2<T> &a, float b)
	{
		x = a.x;
		y = a.y;
		z = b;
	}

	inline tvec3(float a, const tvec2<T> &b)
	{
		x = a;
		y = b.x;
		z = b.y;
	}

	explicit inline tvec3(T v)
	{
		x = v;
		y = v;
		z = v;
	}

	inline tvec3(T x, T y, T z)
	{
		this->x = x;
		this->y = y;
		this->z = z;
	}

	union
	{
		T data[3];
		struct
		{
			T x, y, z;
		};
	};

	inline T &operator[](int index)
	{
		return data[index];
	}

	inline const T &operator[](int index) const
	{
		return data[index];
	}

	inline tvec2<T> xx() const;
	inline tvec2<T> xy() const;
	inline tvec2<T> xz() const;
	inline tvec2<T> yx() const;
	inline tvec2<T> yy() const;
	inline tvec2<T> yz() const;
	inline tvec2<T> zx() const;
	inline tvec2<T> zy() const;
	inline tvec2<T> zz() const;

	inline tvec3<T> xxx() const;
	inline tvec3<T> xxy() const;
	inline tvec3<T> xxz() const;
	inline tvec3<T> xyx() const;
	inline tvec3<T> xyy() const;
	inline tvec3<T> xyz() const;
	inline tvec3<T> xzx() const;
	inline tvec3<T> xzy() const;
	inline tvec3<T> xzz() const;
	inline tvec3<T> yxx() const;
	inline tvec3<T> yxy() const;
	inline tvec3<T> yxz() const;
	inline tvec3<T> yyx() const;
	inline tvec3<T> yyy() const;
	inline tvec3<T> yyz() const;
	inline tvec3<T> yzx() const;
	inline tvec3<T> yzy() const;
	inline tvec3<T> yzz() const;
	inline tvec3<T> zxx() const;
	inline tvec3<T> zxy() const;
	inline tvec3<T> zxz() const;
	inline tvec3<T> zyx() const;
	inline tvec3<T> zyy() const;
	inline tvec3<T> zyz() const;
	inline tvec3<T> zzx() const;
	inline tvec3<T> zzy() const;
	inline tvec3<T> zzz() const;

	inline tvec4<T> xxxx() const;
	inline tvec4<T> xxxy() const;
	inline tvec4<T> xxxz() const;
	inline tvec4<T> xxyx() const;
	inline tvec4<T> xxyy() const;
	inline tvec4<T> xxyz() const;
	inline tvec4<T> xxzx() const;
	inline tvec4<T> xxzy() const;
	inline tvec4<T> xxzz() const;
	inline tvec4<T> xyxx() const;
	inline tvec4<T> xyxy() const;
	inline tvec4<T> xyxz() const;
	inline tvec4<T> xyyx() const;
	inline tvec4<T> xyyy() const;
	inline tvec4<T> xyyz() const;
	inline tvec4<T> xyzx() const;
	inline tvec4<T> xyzy() const;
	inline tvec4<T> xyzz() const;
	inline tvec4<T> xzxx() const;
	inline tvec4<T> xzxy() const;
	inline tvec4<T> xzxz() const;
	inline tvec4<T> xzyx() const;
	inline tvec4<T> xzyy() const;
	inline tvec4<T> xzyz() const;
	inline tvec4<T> xzzx() const;
	inline tvec4<T> xzzy() const;
	inline tvec4<T> xzzz() const;
	inline tvec4<T> yxxx() const;
	inline tvec4<T> yxxy() const;
	inline tvec4<T> yxxz() const;
	inline tvec4<T> yxyx() const;
	inline tvec4<T> yxyy() const;
	inline tvec4<T> yxyz() const;
	inline tvec4<T> yxzx() const;
	inline tvec4<T> yxzy() const;
	inline tvec4<T> yxzz() const;
	inline tvec4<T> yyxx() const;
	inline tvec4<T> yyxy() const;
	inline tvec4<T> yyxz() const;
	inline tvec4<T> yyyx() const;
	inline tvec4<T> yyyy() const;
	inline tvec4<T> yyyz() const;
	inline tvec4<T> yyzx() const;
	inline tvec4<T> yyzy() const;
	inline tvec4<T> yyzz() const;
	inline tvec4<T> yzxx() const;
	inline tvec4<T> yzxy() const;
	inline tvec4<T> yzxz() const;
	inline tvec4<T> yzyx() const;
	inline tvec4<T> yzyy() const;
	inline tvec4<T> yzyz() const;
	inline tvec4<T> yzzx() const;
	inline tvec4<T> yzzy() const;
	inline tvec4<T> yzzz() const;
	inline tvec4<T> zxxx() const;
	inline tvec4<T> zxxy() const;
	inline tvec4<T> zxxz() const;
	inline tvec4<T> zxyx() const;
	inline tvec4<T> zxyy() const;
	inline tvec4<T> zxyz() const;
	inline tvec4<T> zxzx() const;
	inline tvec4<T> zxzy() const;
	inline tvec4<T> zxzz() const;
	inline tvec4<T> zyxx() const;
	inline tvec4<T> zyxy() const;
	inline tvec4<T> zyxz() const;
	inline tvec4<T> zyyx() const;
	inline tvec4<T> zyyy() const;
	inline tvec4<T> zyyz() const;
	inline tvec4<T> zyzx() const;
	inline tvec4<T> zyzy() const;
	inline tvec4<T> zyzz() const;
	inline tvec4<T> zzxx() const;
	inline tvec4<T> zzxy() const;
	inline tvec4<T> zzxz() const;
	inline tvec4<T> zzyx() const;
	inline tvec4<T> zzyy() const;
	inline tvec4<T> zzyz() const;
	inline tvec4<T> zzzx() const;
	inline tvec4<T> zzzy() const;
	inline tvec4<T> zzzz() const;
};

template <typename T>
struct tvec4
{
	tvec4() = default;
	tvec4(const tvec4 &) = default;

	template <typename U>
	explicit inline tvec4(const tvec4<U> &u)
	{
		x = T(u.x);
		y = T(u.y);
		z = T(u.z);
		w = T(u.w);
	}

	inline tvec4(const tvec2<T> &a, const tvec2<T> &b)
	{
		x = a.x;
		y = a.y;
		z = b.x;
		w = b.y;
	}

	inline tvec4(const tvec3<T> &a, float b)
	{
		x = a.x;
		y = a.y;
		z = a.z;
		w = b;
	}

	inline tvec4(float a, const tvec3<T> &b)
	{
		x = a;
		y = b.x;
		z = b.y;
		w = b.z;
	}

	inline tvec4(const tvec2<T> &a, float b, float c)
	{
		x = a.x;
		y = a.y;
		z = b;
		w = c;
	}

	inline tvec4(float a, const tvec2<T> &b, float c)
	{
		x = a;
		y = b.x;
		z = b.y;
		w = c;
	}

	inline tvec4(float a, float b, const tvec2<T> &c)
	{
		x = a;
		y = b;
		z = c.x;
		w = c.y;
	}

	explicit inline tvec4(T v)
	{
		x = v;
		y = v;
		z = v;
		w = v;
	}

	inline tvec4(T x, T y, T z, T w)
	{
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	inline T &operator[](int index)
	{
		return data[index];
	}

	inline const T &operator[](int index) const
	{
		return data[index];
	}

	union
	{
		T data[4];
		struct
		{
			T x, y, z, w;
		};
	};

	inline tvec2<T> xx() const;
	inline tvec2<T> xy() const;
	inline tvec2<T> xz() const;
	inline tvec2<T> xw() const;
	inline tvec2<T> yx() const;
	inline tvec2<T> yy() const;
	inline tvec2<T> yz() const;
	inline tvec2<T> yw() const;
	inline tvec2<T> zx() const;
	inline tvec2<T> zy() const;
	inline tvec2<T> zz() const;
	inline tvec2<T> zw() const;
	inline tvec2<T> wx() const;
	inline tvec2<T> wy() const;
	inline tvec2<T> wz() const;
	inline tvec2<T> ww() const;

	inline tvec3<T> xxx() const;
	inline tvec3<T> xxy() const;
	inline tvec3<T> xxz() const;
	inline tvec3<T> xxw() const;
	inline tvec3<T> xyx() const;
	inline tvec3<T> xyy() const;
	inline tvec3<T> xyz() const;
	inline tvec3<T> xyw() const;
	inline tvec3<T> xzx() const;
	inline tvec3<T> xzy() const;
	inline tvec3<T> xzz() const;
	inline tvec3<T> xzw() const;
	inline tvec3<T> xwx() const;
	inline tvec3<T> xwy() const;
	inline tvec3<T> xwz() const;
	inline tvec3<T> xww() const;
	inline tvec3<T> yxx() const;
	inline tvec3<T> yxy() const;
	inline tvec3<T> yxz() const;
	inline tvec3<T> yxw() const;
	inline tvec3<T> yyx() const;
	inline tvec3<T> yyy() const;
	inline tvec3<T> yyz() const;
	inline tvec3<T> yyw() const;
	inline tvec3<T> yzx() const;
	inline tvec3<T> yzy() const;
	inline tvec3<T> yzz() const;
	inline tvec3<T> yzw() const;
	inline tvec3<T> ywx() const;
	inline tvec3<T> ywy() const;
	inline tvec3<T> ywz() const;
	inline tvec3<T> yww() const;
	inline tvec3<T> zxx() const;
	inline tvec3<T> zxy() const;
	inline tvec3<T> zxz() const;
	inline tvec3<T> zxw() const;
	inline tvec3<T> zyx() const;
	inline tvec3<T> zyy() const;
	inline tvec3<T> zyz() const;
	inline tvec3<T> zyw() const;
	inline tvec3<T> zzx() const;
	inline tvec3<T> zzy() const;
	inline tvec3<T> zzz() const;
	inline tvec3<T> zzw() const;
	inline tvec3<T> zwx() const;
	inline tvec3<T> zwy() const;
	inline tvec3<T> zwz() const;
	inline tvec3<T> zww() const;
	inline tvec3<T> wxx() const;
	inline tvec3<T> wxy() const;
	inline tvec3<T> wxz() const;
	inline tvec3<T> wxw() const;
	inline tvec3<T> wyx() const;
	inline tvec3<T> wyy() const;
	inline tvec3<T> wyz() const;
	inline tvec3<T> wyw() const;
	inline tvec3<T> wzx() const;
	inline tvec3<T> wzy() const;
	inline tvec3<T> wzz() const;
	inline tvec3<T> wzw() const;
	inline tvec3<T> wwx() const;
	inline tvec3<T> wwy() const;
	inline tvec3<T> wwz() const;
	inline tvec3<T> www() const;

	inline tvec4 xxxx() const;
	inline tvec4 xxxy() const;
	inline tvec4 xxxz() const;
	inline tvec4 xxxw() const;
	inline tvec4 xxyx() const;
	inline tvec4 xxyy() const;
	inline tvec4 xxyz() const;
	inline tvec4 xxyw() const;
	inline tvec4 xxzx() const;
	inline tvec4 xxzy() const;
	inline tvec4 xxzz() const;
	inline tvec4 xxzw() const;
	inline tvec4 xxwx() const;
	inline tvec4 xxwy() const;
	inline tvec4 xxwz() const;
	inline tvec4 xxww() const;
	inline tvec4 xyxx() const;
	inline tvec4 xyxy() const;
	inline tvec4 xyxz() const;
	inline tvec4 xyxw() const;
	inline tvec4 xyyx() const;
	inline tvec4 xyyy() const;
	inline tvec4 xyyz() const;
	inline tvec4 xyyw() const;
	inline tvec4 xyzx() const;
	inline tvec4 xyzy() const;
	inline tvec4 xyzz() const;
	inline tvec4 xyzw() const;
	inline tvec4 xywx() const;
	inline tvec4 xywy() const;
	inline tvec4 xywz() const;
	inline tvec4 xyww() const;
	inline tvec4 xzxx() const;
	inline tvec4 xzxy() const;
	inline tvec4 xzxz() const;
	inline tvec4 xzxw() const;
	inline tvec4 xzyx() const;
	inline tvec4 xzyy() const;
	inline tvec4 xzyz() const;
	inline tvec4 xzyw() const;
	inline tvec4 xzzx() const;
	inline tvec4 xzzy() const;
	inline tvec4 xzzz() const;
	inline tvec4 xzzw() const;
	inline tvec4 xzwx() const;
	inline tvec4 xzwy() const;
	inline tvec4 xzwz() const;
	inline tvec4 xzww() const;
	inline tvec4 xwxx() const;
	inline tvec4 xwxy() const;
	inline tvec4 xwxz() const;
	inline tvec4 xwxw() const;
	inline tvec4 xwyx() const;
	inline tvec4 xwyy() const;
	inline tvec4 xwyz() const;
	inline tvec4 xwyw() const;
	inline tvec4 xwzx() const;
	inline tvec4 xwzy() const;
	inline tvec4 xwzz() const;
	inline tvec4 xwzw() const;
	inline tvec4 xwwx() const;
	inline tvec4 xwwy() const;
	inline tvec4 xwwz() const;
	inline tvec4 xwww() const;
	inline tvec4 yxxx() const;
	inline tvec4 yxxy() const;
	inline tvec4 yxxz() const;
	inline tvec4 yxxw() const;
	inline tvec4 yxyx() const;
	inline tvec4 yxyy() const;
	inline tvec4 yxyz() const;
	inline tvec4 yxyw() const;
	inline tvec4 yxzx() const;
	inline tvec4 yxzy() const;
	inline tvec4 yxzz() const;
	inline tvec4 yxzw() const;
	inline tvec4 yxwx() const;
	inline tvec4 yxwy() const;
	inline tvec4 yxwz() const;
	inline tvec4 yxww() const;
	inline tvec4 yyxx() const;
	inline tvec4 yyxy() const;
	inline tvec4 yyxz() const;
	inline tvec4 yyxw() const;
	inline tvec4 yyyx() const;
	inline tvec4 yyyy() const;
	inline tvec4 yyyz() const;
	inline tvec4 yyyw() const;
	inline tvec4 yyzx() const;
	inline tvec4 yyzy() const;
	inline tvec4 yyzz() const;
	inline tvec4 yyzw() const;
	inline tvec4 yywx() const;
	inline tvec4 yywy() const;
	inline tvec4 yywz() const;
	inline tvec4 yyww() const;
	inline tvec4 yzxx() const;
	inline tvec4 yzxy() const;
	inline tvec4 yzxz() const;
	inline tvec4 yzxw() const;
	inline tvec4 yzyx() const;
	inline tvec4 yzyy() const;
	inline tvec4 yzyz() const;
	inline tvec4 yzyw() const;
	inline tvec4 yzzx() const;
	inline tvec4 yzzy() const;
	inline tvec4 yzzz() const;
	inline tvec4 yzzw() const;
	inline tvec4 yzwx() const;
	inline tvec4 yzwy() const;
	inline tvec4 yzwz() const;
	inline tvec4 yzww() const;
	inline tvec4 ywxx() const;
	inline tvec4 ywxy() const;
	inline tvec4 ywxz() const;
	inline tvec4 ywxw() const;
	inline tvec4 ywyx() const;
	inline tvec4 ywyy() const;
	inline tvec4 ywyz() const;
	inline tvec4 ywyw() const;
	inline tvec4 ywzx() const;
	inline tvec4 ywzy() const;
	inline tvec4 ywzz() const;
	inline tvec4 ywzw() const;
	inline tvec4 ywwx() const;
	inline tvec4 ywwy() const;
	inline tvec4 ywwz() const;
	inline tvec4 ywww() const;
	inline tvec4 zxxx() const;
	inline tvec4 zxxy() const;
	inline tvec4 zxxz() const;
	inline tvec4 zxxw() const;
	inline tvec4 zxyx() const;
	inline tvec4 zxyy() const;
	inline tvec4 zxyz() const;
	inline tvec4 zxyw() const;
	inline tvec4 zxzx() const;
	inline tvec4 zxzy() const;
	inline tvec4 zxzz() const;
	inline tvec4 zxzw() const;
	inline tvec4 zxwx() const;
	inline tvec4 zxwy() const;
	inline tvec4 zxwz() const;
	inline tvec4 zxww() const;
	inline tvec4 zyxx() const;
	inline tvec4 zyxy() const;
	inline tvec4 zyxz() const;
	inline tvec4 zyxw() const;
	inline tvec4 zyyx() const;
	inline tvec4 zyyy() const;
	inline tvec4 zyyz() const;
	inline tvec4 zyyw() const;
	inline tvec4 zyzx() const;
	inline tvec4 zyzy() const;
	inline tvec4 zyzz() const;
	inline tvec4 zyzw() const;
	inline tvec4 zywx() const;
	inline tvec4 zywy() const;
	inline tvec4 zywz() const;
	inline tvec4 zyww() const;
	inline tvec4 zzxx() const;
	inline tvec4 zzxy() const;
	inline tvec4 zzxz() const;
	inline tvec4 zzxw() const;
	inline tvec4 zzyx() const;
	inline tvec4 zzyy() const;
	inline tvec4 zzyz() const;
	inline tvec4 zzyw() const;
	inline tvec4 zzzx() const;
	inline tvec4 zzzy() const;
	inline tvec4 zzzz() const;
	inline tvec4 zzzw() const;
	inline tvec4 zzwx() const;
	inline tvec4 zzwy() const;
	inline tvec4 zzwz() const;
	inline tvec4 zzww() const;
	inline tvec4 zwxx() const;
	inline tvec4 zwxy() const;
	inline tvec4 zwxz() const;
	inline tvec4 zwxw() const;
	inline tvec4 zwyx() const;
	inline tvec4 zwyy() const;
	inline tvec4 zwyz() const;
	inline tvec4 zwyw() const;
	inline tvec4 zwzx() const;
	inline tvec4 zwzy() const;
	inline tvec4 zwzz() const;
	inline tvec4 zwzw() const;
	inline tvec4 zwwx() const;
	inline tvec4 zwwy() const;
	inline tvec4 zwwz() const;
	inline tvec4 zwww() const;
	inline tvec4 wxxx() const;
	inline tvec4 wxxy() const;
	inline tvec4 wxxz() const;
	inline tvec4 wxxw() const;
	inline tvec4 wxyx() const;
	inline tvec4 wxyy() const;
	inline tvec4 wxyz() const;
	inline tvec4 wxyw() const;
	inline tvec4 wxzx() const;
	inline tvec4 wxzy() const;
	inline tvec4 wxzz() const;
	inline tvec4 wxzw() const;
	inline tvec4 wxwx() const;
	inline tvec4 wxwy() const;
	inline tvec4 wxwz() const;
	inline tvec4 wxww() const;
	inline tvec4 wyxx() const;
	inline tvec4 wyxy() const;
	inline tvec4 wyxz() const;
	inline tvec4 wyxw() const;
	inline tvec4 wyyx() const;
	inline tvec4 wyyy() const;
	inline tvec4 wyyz() const;
	inline tvec4 wyyw() const;
	inline tvec4 wyzx() const;
	inline tvec4 wyzy() const;
	inline tvec4 wyzz() const;
	inline tvec4 wyzw() const;
	inline tvec4 wywx() const;
	inline tvec4 wywy() const;
	inline tvec4 wywz() const;
	inline tvec4 wyww() const;
	inline tvec4 wzxx() const;
	inline tvec4 wzxy() const;
	inline tvec4 wzxz() const;
	inline tvec4 wzxw() const;
	inline tvec4 wzyx() const;
	inline tvec4 wzyy() const;
	inline tvec4 wzyz() const;
	inline tvec4 wzyw() const;
	inline tvec4 wzzx() const;
	inline tvec4 wzzy() const;
	inline tvec4 wzzz() const;
	inline tvec4 wzzw() const;
	inline tvec4 wzwx() const;
	inline tvec4 wzwy() const;
	inline tvec4 wzwz() const;
	inline tvec4 wzww() const;
	inline tvec4 wwxx() const;
	inline tvec4 wwxy() const;
	inline tvec4 wwxz() const;
	inline tvec4 wwxw() const;
	inline tvec4 wwyx() const;
	inline tvec4 wwyy() const;
	inline tvec4 wwyz() const;
	inline tvec4 wwyw() const;
	inline tvec4 wwzx() const;
	inline tvec4 wwzy() const;
	inline tvec4 wwzz() const;
	inline tvec4 wwzw() const;
	inline tvec4 wwwx() const;
	inline tvec4 wwwy() const;
	inline tvec4 wwwz() const;
	inline tvec4 wwww() const;
};

template <typename T>
struct tmat2
{
	tmat2() = default;

	explicit inline tmat2(T v)
	{
		vec[0] = tvec2<T>(v, T(0));
		vec[1] = tvec2<T>(T(0), v);
	}

	inline tmat2(const tvec2<T> &a, const tvec2<T> &b)
	{
		vec[0] = a;
		vec[1] = b;
	}

	inline tvec2<T> &operator[](int index)
	{
		return vec[index];
	}

	inline const tvec2<T> &operator[](int index) const
	{
		return vec[index];
	}

private:
	tvec2<T> vec[2];
};

template <typename T>
struct tmat3
{
	tmat3() = default;

	explicit inline tmat3(T v)
	{
		vec[0] = tvec3<T>(v, T(0), T(0));
		vec[1] = tvec3<T>(T(0), v, T(0));
		vec[2] = tvec3<T>(T(0), T(0), v);
	}

	inline tmat3(const tvec3<T> &a, const tvec3<T> &b, const tvec3<T> &c)
	{
		vec[0] = a;
		vec[1] = b;
		vec[2] = c;
	}

	explicit inline tmat3(const tmat4<T> &m)
	{
		for (int col = 0; col < 3; col++)
			for (int row = 0; row < 3; row++)
				vec[col][row] = m[col][row];
	}

	inline tvec3<T> &operator[](int index)
	{
		return vec[index];
	}

	inline const tvec3<T> &operator[](int index) const
	{
		return vec[index];
	}

private:
	tvec3<T> vec[3];
};

template <typename T>
struct tmat4
{
	tmat4() = default;

	explicit inline tmat4(T v)
	{
		vec[0] = tvec4<T>(v, T(0), T(0), T(0));
		vec[1] = tvec4<T>(T(0), v, T(0), T(0));
		vec[2] = tvec4<T>(T(0), T(0), v, T(0));
		vec[3] = tvec4<T>(T(0), T(0), T(0), v);
	}

	explicit inline tmat4(const tmat3<T> &m)
	{
		vec[0] = tvec4<T>(m[0], T(0));
		vec[1] = tvec4<T>(m[1], T(0));
		vec[2] = tvec4<T>(m[2], T(0));
		vec[3] = tvec4<T>(T(0), T(0), T(0), T(1));
	}

	inline tmat4(const tvec4<T> &a, const tvec4<T> &b, const tvec4<T> &c, const tvec4<T> &d)
	{
		vec[0] = a;
		vec[1] = b;
		vec[2] = c;
		vec[3] = d;
	}

	inline tvec4<T> &operator[](int index)
	{
		return vec[index];
	}

	inline const tvec4<T> &operator[](int index) const
	{
		return vec[index];
	}

private:
	tvec4<T> vec[4];
};

using uint = uint32_t;
using vec2 = tvec2<float>;
using vec3 = tvec3<float>;
using vec4 = tvec4<float>;
using mat2 = tmat2<float>;
using mat3 = tmat3<float>;
using mat4 = tmat4<float>;

using ivec2 = tvec2<int32_t>;
using ivec3 = tvec3<int32_t>;
using ivec4 = tvec4<int32_t>;
using uvec2 = tvec2<uint32_t>;
using uvec3 = tvec3<uint32_t>;
using uvec4 = tvec4<uint32_t>;

using bvec2 = tvec2<bool>;
using bvec3 = tvec3<bool>;
using bvec4 = tvec4<bool>;

struct quat : private vec4
{
	quat() = default;
	quat(const quat &) = default;
	quat(float w, float x, float y, float z)
		: vec4(x, y, z, w)
	{}

	explicit inline quat(const vec4 &v)
		: vec4(v)
	{}

	inline quat(float w, const vec3 &v)
		: vec4(v, w)
	{}

	inline const vec4 &as_vec4() const
	{
		return *static_cast<const vec4 *>(this);
	}

	using vec4::x;
	using vec4::y;
	using vec4::z;
	using vec4::w;
};

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

template <typename T> inline T pi() { return T(3.1415926535897932384626433832795028841971); }
template <typename T> inline T half_pi() { return T(0.5) * pi<T>(); }

}
