/* Copyright (c) 2021-2024 Hans-Kristian Arntzen
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
#include <utility>
#include <stddef.h>
#include <assert.h>

namespace Util
{
struct IntrusiveUnorderedArrayEnabled
{
	size_t unordered_array_offset;
};

// A special kind of vector where we only care about contiguous storage, not relative order between elements.
// This allows for O(1) removal. The stored type needs to store its own array offset.
template <typename T>
class IntrusiveUnorderedArray
{
public:
	static_assert(std::is_base_of<IntrusiveUnorderedArrayEnabled, T>::value,
	              "T is not derived from IntrusiveUnorderedArrayEnabled.");

	void add(T *t)
	{
		t->unordered_array_offset = ts.size();
		ts.push_back(t);
	}

	void erase(T *t)
	{
		erase_offset(t->unordered_array_offset);
	}

	typename std::vector<T *>::const_iterator begin() const
	{
		return ts.begin();
	}

	typename std::vector<T *>::const_iterator end() const
	{
		return ts.end();
	}

	size_t size() const
	{
		return ts.size();
	}

	void clear()
	{
		ts.clear();
	}

	// If functor returns true, the pointer can be freed in the callback.
	// Implementation must not dereference the pointer if true is returned.
	template <typename Func>
	void garbage_collect_if(const Func &func)
	{
		auto begin_itr = begin();
		auto end_itr = end();
		while (begin_itr != end_itr)
		{
			size_t offset = (*begin_itr)->unordered_array_offset;
			if (func(*begin_itr))
			{
				--end_itr;
				erase_offset(offset);
			}
			else
			{
				++begin_itr;
			}
		}
	}

private:
	std::vector<T *> ts;

	void erase_offset(size_t offset)
	{
		assert(offset < ts.size());
		auto &current = ts[offset];
		if (&current != &ts.back())
		{
			std::swap(current, ts.back());
			current->unordered_array_offset = offset;
		}
		ts.pop_back();
	}
};
}
