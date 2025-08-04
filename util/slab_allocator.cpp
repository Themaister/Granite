/* Copyright (c) 2017-2025 Hans-Kristian Arntzen
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

#include "slab_allocator.hpp"

namespace Util
{
SlabAllocator::SlabAllocator(size_t object_size_)
	: object_size(object_size_)
{
}

uint8_t *SlabAllocator::allocate()
{
	if (vacants.empty())
	{
		size_t count = size_t(64) << memory.size();
		memory.emplace_back(static_cast<uint8_t *>(memalign_alloc(64, count * object_size)));
		auto *ptr = memory.back().get();
		vacants.reserve(vacants.size() + count);
		for (size_t i = 0; i < count; i++, ptr += object_size)
			vacants.push_back(ptr);
	}

	auto *v = vacants.back();
	vacants.pop_back();
	return v;
}

void SlabAllocator::free(uint8_t *ptr)
{
	vacants.push_back(ptr);
}

void ThreadSafeSlabAllocator::init(size_t object_size)
{
	slab = SlabAllocator(object_size);
}
}
