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

#include <atomic>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include "aligned_alloc.hpp"
#include "bitops.hpp"
#include <type_traits>
#include <utility>
#include <algorithm>

namespace Util
{
template <typename T, int MinimumMSB = 8>
class AtomicAppendBuffer
{
public:
	static_assert(std::is_trivially_destructible<T>::value, "T is not trivially destructible.");

	AtomicAppendBuffer()
	{
		assert(count.is_lock_free());
		assert(lists[0].is_lock_free());

		count.store(0, std::memory_order_relaxed);
		for (auto &l : lists)
			l.store(nullptr, std::memory_order_relaxed);
	}

	~AtomicAppendBuffer()
	{
		for (auto &l : lists)
		{
			auto *ptr = l.load(std::memory_order_relaxed);
			Util::memalign_free(ptr);
		}
	}

	AtomicAppendBuffer(const AtomicAppendBuffer &) = delete;
	void operator=(const AtomicAppendBuffer &) = delete;

	void clear()
	{
		count.store(0, std::memory_order_relaxed);
	}

	// Only thing that is thread-safe.
	template <typename U>
	void push(U &&u)
	{
		uint32_t offset = count.fetch_add(1, std::memory_order_relaxed);
		auto w = reserve_write(offset);
		w.first[w.second] = std::forward<U>(u);
	}

	uint32_t size() const
	{
		return count.load(std::memory_order_relaxed);
	}

	template <typename Func>
	void for_each_ranged(Func &&func)
	{
		uint32_t offset = count.load(std::memory_order_relaxed);
		if (!offset)
			return;
		offset--;
		int msb = 31 - int(leading_zeroes(offset));
		msb = std::max<int>(msb, MinimumMSB);
		int list_index = msb - MinimumMSB;

		// Iterate over the complete lists.
		for (int index = 0; index < list_index; index++)
		{
			uint32_t num_elements = num_elements_per_list_index(index);
			auto *t = lists[index].load(std::memory_order_relaxed);
			for (uint32_t i = 0; i < num_elements; i += 1u << MinimumMSB)
				func(&t[i], 1u << MinimumMSB);
		}

		// Iterate over the final list.
		if (list_index != 0)
			offset -= 1u << msb;
		uint32_t num_elements = offset + 1;
		auto *t = lists[list_index].load(std::memory_order_relaxed);
		for (uint32_t i = 0; i < num_elements; i += 1u << MinimumMSB)
			func(&t[i], std::min<uint32_t>(num_elements - i, 1u << MinimumMSB));
	}

private:
	static_assert(MinimumMSB < 32, "MinimumMSB must be < 32.");
	std::atomic<T *> lists[32 - MinimumMSB];
	std::atomic_uint32_t count;

	static uint32_t num_elements_per_list_index(unsigned index)
	{
		return 1u << (index + MinimumMSB + unsigned(index == 0));
	}

	std::pair<T *, uint32_t> reserve_write(uint32_t required_offset)
	{
		int msb = 31 - int(leading_zeroes(uint32_t(required_offset)));
		if (msb < MinimumMSB)
			msb = MinimumMSB;
		int list_index = msb - MinimumMSB;

		uint32_t offset = required_offset;
		if (list_index != 0)
			offset -= 1u << msb;

		// If we can observe the pointer, we're good.
		if (auto *new_t = lists[list_index].load(std::memory_order_relaxed))
			return { new_t, offset };

		uint32_t required_count = num_elements_per_list_index(list_index);
		size_t required_size = size_t(required_count) * sizeof(T);
		auto *new_t = static_cast<T *>(Util::memalign_alloc(std::max<size_t>(64, alignof(T)), required_size));
		T *expected_t = nullptr;

		// Relaxed order is fine. We will not read anything from this buffer until we have synchronized, and
		// thus memory order is moot.
		if (!lists[list_index].compare_exchange_strong(expected_t, new_t,
		                                               std::memory_order_relaxed,
		                                               std::memory_order_relaxed))
		{
			// Another thread allocated early, free.
			Util::memalign_free(new_t);
			new_t = expected_t;
		}

		return { new_t, offset };
	}
};
}
