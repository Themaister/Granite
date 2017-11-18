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

#include <atomic>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

namespace Util
{
class RWSpinLock
{
public:
	RWSpinLock()
	{
		counter.store(0);
	}

	inline void lock_read()
	{
		unsigned v = counter.fetch_add(4, std::memory_order_acquire);
		while ((v & 3) != 0)
			v = counter.load(std::memory_order_acquire);
	}

	inline void unlock_read()
	{
		counter.fetch_sub(4, std::memory_order_relaxed);
	}

	inline void lock_write()
	{
		// Lock out potential readers.
		counter.fetch_or(2, std::memory_order_relaxed);

		uint32_t expected = 2;
		while (!counter.compare_exchange_weak(expected, 1,
		                                      std::memory_order_acquire,
		                                      std::memory_order_relaxed))
		{
#ifdef __SSE2__
			_mm_pause();
#endif
			// Lock out potential readers.
			counter.fetch_or(2, std::memory_order_relaxed);
			expected = 2;
		}
	}

	inline void unlock_write()
	{
		counter.fetch_and(~0x1u, std::memory_order_release);
	}

private:
	std::atomic<uint32_t> counter;
};
}