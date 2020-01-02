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

#include <vector>
#include <type_traits>

namespace Util
{
template<typename T>
class ArrayView
{
public:
	using ConstT = const typename std::remove_const<T>::type;

	ArrayView(T *t, size_t size)
		: ptr(t), array_size(size)
	{
	}

	template<typename U>
	ArrayView(U &u)
		: ptr(u.data()), array_size(u.size())
	{
	}

	ArrayView() = default;

	T *begin()
	{
		return ptr;
	}

	T *end()
	{
		return ptr + array_size;
	}

	ConstT *begin() const
	{
		return ptr;
	}

	ConstT *end() const
	{
		return ptr + array_size;
	}

	T &operator[](size_t n)
	{
		return ptr[n];
	}

	ConstT &operator[](size_t n) const
	{
		return ptr[n];
	}

	size_t size() const
	{
		return array_size;
	}

	T *data()
	{
		return ptr;
	}

	ConstT *data() const
	{
		return ptr;
	}

	bool empty() const
	{
		return array_size == 0;
	}

	void reset()
	{
		ptr = nullptr;
		array_size = 0;
	}

private:
	T *ptr = nullptr;
	size_t array_size = 0;
};
}