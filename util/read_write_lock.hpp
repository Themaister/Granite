/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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
	enum { Reader = 2, Writer = 1 };
	RWSpinLock()
	{
		counter.store(0);
	}

	inline void lock_read()
	{
		unsigned v = counter.fetch_add(Reader, std::memory_order_acquire);
		while ((v & Writer) != 0)
		{
#ifdef __SSE2__
			_mm_pause();
#endif
			v = counter.load(std::memory_order_acquire);
		}
	}

	inline bool try_lock_read()
	{
		unsigned v = counter.fetch_add(Reader, std::memory_order_acquire);
		if ((v & Writer) != 0)
		{
			unlock_read();
			return false;
		}

		return true;
	}

	inline void unlock_read()
	{
		counter.fetch_sub(Reader, std::memory_order_release);
	}

	inline void lock_write()
	{
		uint32_t expected = 0;
		while (!counter.compare_exchange_weak(expected, Writer,
		                                      std::memory_order_acquire,
		                                      std::memory_order_relaxed))
		{
#ifdef __SSE2__
			_mm_pause();
#endif
			expected = 0;
		}
	}

	inline bool try_lock_write()
	{
		uint32_t expected = 0;
		return counter.compare_exchange_strong(expected, Writer,
		                                       std::memory_order_acquire,
		                                       std::memory_order_relaxed);
	}

	inline void unlock_write()
	{
		counter.fetch_and(~Writer, std::memory_order_release);
	}

	inline void promote_reader_to_writer()
	{
		uint32_t expected = Reader;
		if (!counter.compare_exchange_strong(expected, Writer,
		                                     std::memory_order_acquire,
		                                     std::memory_order_relaxed))
		{
			unlock_read();
			lock_write();
		}
	}

private:
	std::atomic_uint32_t counter;
};

class RWSpinLockReadHolder
{
public:
	explicit RWSpinLockReadHolder(RWSpinLock &lock_)
		: lock(lock_)
	{
		lock.lock_read();
	}

	~RWSpinLockReadHolder()
	{
		lock.unlock_read();
	}

private:
	RWSpinLock &lock;
};

class RWSpinLockWriteHolder
{
public:
	explicit RWSpinLockWriteHolder(RWSpinLock &lock_)
			: lock(lock_)
	{
		lock.lock_write();
	}

	~RWSpinLockWriteHolder()
	{
		lock.unlock_write();
	}

private:
	RWSpinLock &lock;
};
}
