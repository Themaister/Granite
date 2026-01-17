/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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
#include <stdlib.h>
#include <memory>
#include <vector>
#include <mutex>
#include "aligned_alloc.hpp"

namespace Util
{
class SlabAllocator
{
public:
	SlabAllocator() = default;
	explicit SlabAllocator(size_t object_size);
	uint8_t *allocate();
	void free(uint8_t *ptr);

private:
	struct MallocDeleter
	{
		void operator()(uint8_t *ptr)
		{
			memalign_free(ptr);
		}
	};

	std::vector<uint8_t *> vacants;
	std::vector<std::unique_ptr<uint8_t, MallocDeleter>> memory;
	size_t object_size = 0;
};

class ThreadSafeSlabAllocator
{
public:
	ThreadSafeSlabAllocator() = default;
	void init(size_t object_size);

	inline uint8_t *allocate() { std::lock_guard<std::mutex> holder{lock}; return slab.allocate(); }
	inline void free(uint8_t *ptr) { std::lock_guard<std::mutex> holder{lock}; slab.free(ptr); }

private:
	SlabAllocator slab;
	std::mutex lock;
};
}
