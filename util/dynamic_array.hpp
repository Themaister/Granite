/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#include "aligned_alloc.hpp"
#include <memory>
#include <algorithm>
#include <type_traits>

namespace Util
{
template <typename T>
class DynamicArray
{
public:
	// Only POD-like types work here since we don't invoke placement new or delete.
	static_assert(std::is_trivially_default_constructible<T>::value, "T must be trivially constructible.");
	static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible.");

	inline void reserve(size_t n)
	{
		if (n > N)
		{
			buffer.reset(static_cast<T *>(memalign_alloc(std::max<size_t>(64, alignof(T)), n * sizeof(T))));
			N = n;
		}
	}

	inline T &operator[](size_t index) { return buffer.get()[index]; }
	inline const T &operator[](size_t index) const { return buffer.get()[index]; }
	inline T *data() { return buffer.get(); }
	inline const T *data() const { return buffer.get(); }
	inline size_t get_capacity() const { return N; }

private:
	std::unique_ptr<T, AlignedDeleter> buffer;
	size_t N = 0;
};
}
