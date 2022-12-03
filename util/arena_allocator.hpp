/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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
#include "intrusive_list.hpp"
#include "logging.hpp"

namespace Util
{
// Expands the buddy allocator to consider 32 "buddies".
// The allocator is logical and works in terms of units, not bytes.
class LegionAllocator
{
public:
	enum
	{
		NumSubBlocks = 32u,
		AllFree = ~0u
	};

	LegionAllocator(const LegionAllocator &) = delete;
	void operator=(const LegionAllocator &) = delete;

	LegionAllocator()
	{
		for (auto &v : free_blocks)
			v = AllFree;
		longest_run = 32;
	}

	~LegionAllocator()
	{
		if (free_blocks[0] != AllFree)
			LOGE("Memory leak in block detected.\n");
	}

	inline bool full() const
	{
		return free_blocks[0] == 0;
	}

	inline bool empty() const
	{
		return free_blocks[0] == AllFree;
	}

	inline uint32_t get_longest_run() const
	{
		return longest_run;
	}

	void allocate(uint32_t num_blocks, uint32_t &mask, uint32_t &offset);
	void free(uint32_t mask);

private:
	uint32_t free_blocks[NumSubBlocks];
	uint32_t longest_run = 0;
	void update_longest_run();
};

// Represents that a legion heap is backed by some kind of allocation.
template <typename BackingAllocation>
struct LegionHeap : Util::IntrusiveListEnabled<LegionHeap<BackingAllocation>>
{
	BackingAllocation allocation;
	Util::LegionAllocator heap;
};

template <typename BackingAllocation>
struct AllocationArena
{
	Util::IntrusiveList<LegionHeap<BackingAllocation>> heaps[Util::LegionAllocator::NumSubBlocks];
	Util::IntrusiveList<LegionHeap<BackingAllocation>> full_heaps;
	uint32_t heap_availability_mask = 0;
};
}