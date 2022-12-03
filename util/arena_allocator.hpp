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
#include <assert.h>
#include "intrusive_list.hpp"
#include "logging.hpp"
#include "object_pool.hpp"
#include "bitops.hpp"

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

template <typename DerivedAllocator, typename BackingAllocation>
class ArenaAllocator
{
public:
	using MiniHeap = LegionHeap<BackingAllocation>;

	~ArenaAllocator()
	{
		bool error = false;

		if (heap_arena.full_heaps.begin())
			error = true;

		for (auto &h : heap_arena.heaps)
			if (h.begin())
				error = true;

		if (error)
			LOGE("Memory leaked in class allocator!\n");
	}

	inline void set_sub_block_size(uint32_t size)
	{
		assert(Util::is_pow2(size));
		sub_block_size_log2 = Util::floor_log2(size);
		sub_block_size = size;
	}

	inline uint32_t get_max_allocation_size() const
	{
		return sub_block_size * Util::LegionAllocator::NumSubBlocks;
	}

	inline uint32_t get_block_alignment() const
	{
		return sub_block_size;
	}

	inline bool allocate(uint32_t size, BackingAllocation *alloc)
	{
		unsigned num_blocks = (size + sub_block_size - 1) >> sub_block_size_log2;
		uint32_t size_mask = (1u << (num_blocks - 1)) - 1;
		uint32_t index = trailing_zeroes(heap_arena.heap_availability_mask & ~size_mask);

		if (index < LegionAllocator::NumSubBlocks)
		{
			auto itr = heap_arena.heaps[index].begin();
			assert(itr);
			assert(index >= (num_blocks - 1));

			auto &heap = *itr;
			static_cast<DerivedAllocator *>(this)->prepare_allocation(alloc, heap, suballocate(num_blocks, heap));

			unsigned new_index = heap.heap.get_longest_run() - 1;

			if (heap.heap.full())
			{
				heap_arena.full_heaps.move_to_front(heap_arena.heaps[index], itr);
				if (!heap_arena.heaps[index].begin())
					heap_arena.heap_availability_mask &= ~(1u << index);
			}
			else if (new_index != index)
			{
				auto &new_heap = heap_arena.heaps[new_index];
				new_heap.move_to_front(heap_arena.heaps[index], itr);
				heap_arena.heap_availability_mask |= 1u << new_index;
				if (!heap_arena.heaps[index].begin())
					heap_arena.heap_availability_mask &= ~(1u << index);
			}

			alloc->heap = itr;
			return true;
		}

		// We didn't find a vacant heap, make a new one.
		auto *node = object_pool->allocate();
		if (!node)
			return false;

		auto &heap = *node;

		if (!static_cast<DerivedAllocator *>(this)->allocate_backing_heap(&heap.allocation))
		{
			object_pool->free(node);
			return false;
		}

		// This cannot fail.
		static_cast<DerivedAllocator *>(this)->prepare_allocation(alloc, heap, suballocate(num_blocks, heap));

		alloc->heap = node;
		if (heap.heap.full())
		{
			heap_arena.full_heaps.insert_front(node);
		}
		else
		{
			unsigned new_index = heap.heap.get_longest_run() - 1;
			heap_arena.heaps[new_index].insert_front(node);
			heap_arena.heap_availability_mask |= 1u << new_index;
		}

		return true;
	}

	inline void free(typename IntrusiveList<MiniHeap>::Iterator itr, uint32_t mask)
	{
		auto *heap = itr.get();
		auto &block = heap->heap;
		bool was_full = block.full();

		unsigned index = block.get_longest_run() - 1;
		block.free(mask);
		unsigned new_index = block.get_longest_run() - 1;

		if (block.empty())
		{
			static_cast<DerivedAllocator *>(this)->free_backing_heap(&heap->allocation);

			if (was_full)
				heap_arena.full_heaps.erase(heap);
			else
			{
				heap_arena.heaps[index].erase(heap);
				if (!heap_arena.heaps[index].begin())
					heap_arena.heap_availability_mask &= ~(1u << index);
			}

			object_pool->free(heap);
		}
		else if (was_full)
		{
			heap_arena.heaps[new_index].move_to_front(heap_arena.full_heaps, heap);
			heap_arena.heap_availability_mask |= 1u << new_index;
		}
		else if (index != new_index)
		{
			heap_arena.heaps[new_index].move_to_front(heap_arena.heaps[index], heap);
			heap_arena.heap_availability_mask |= 1u << new_index;
			if (!heap_arena.heaps[index].begin())
				heap_arena.heap_availability_mask &= ~(1u << index);
		}
	}

	inline void set_object_pool(ObjectPool<MiniHeap> *object_pool_)
	{
		object_pool = object_pool_;
	}

protected:
	AllocationArena<BackingAllocation> heap_arena;
	ObjectPool<LegionHeap<BackingAllocation>> *object_pool = nullptr;

	uint32_t sub_block_size = 1;
	uint32_t sub_block_size_log2 = 0;

	struct SuballocationResult
	{
		uint32_t offset;
		uint32_t size;
		uint32_t mask;
	};

private:
	inline SuballocationResult suballocate(uint32_t num_blocks, MiniHeap &heap)
	{
		SuballocationResult res = {};
		res.size = num_blocks << sub_block_size_log2;
		heap.heap.allocate(num_blocks, res.mask, res.offset);
		res.offset <<= sub_block_size_log2;
		return res;
	}
};
}