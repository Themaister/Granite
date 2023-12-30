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

#include "arena_allocator.hpp"
#include "bitops.hpp"
#include <assert.h>

namespace Util
{
void LegionAllocator::allocate(uint32_t num_blocks, uint32_t &out_mask, uint32_t &out_offset)
{
	assert(NumSubBlocks >= num_blocks);
	assert(num_blocks != 0);

	uint32_t block_mask;
	if (num_blocks == NumSubBlocks)
		block_mask = ~0u;
	else
		block_mask = ((1u << num_blocks) - 1u);

	uint32_t mask = free_blocks[num_blocks - 1];
	uint32_t b = trailing_zeroes(mask);

	assert(((free_blocks[0] >> b) & block_mask) == block_mask);

	uint32_t sb = block_mask << b;
	free_blocks[0] &= ~sb;
	update_longest_run();

	out_mask = sb;
	out_offset = b;
}

void LegionAllocator::free(uint32_t mask)
{
	assert((free_blocks[0] & mask) == 0);
	free_blocks[0] |= mask;
	update_longest_run();
}

void LegionAllocator::update_longest_run()
{
	uint32_t f = free_blocks[0];
	longest_run = 0;

	while (f)
	{
		free_blocks[longest_run++] = f;
		f &= f >> 1;
	}
}

bool SliceSubAllocator::allocate_backing_heap(AllocatedSlice *allocation)
{
	uint32_t count = sub_block_size * Util::LegionAllocator::NumSubBlocks;

	if (parent)
	{
		return parent->allocate(count, allocation);
	}
	else if (global_allocator)
	{
		uint32_t index = global_allocator->allocate(count);
		if (index == UINT32_MAX)
			return false;

		*allocation = {};
		allocation->count = count;
		allocation->buffer_index = index;
		return true;
	}
	else
	{
		return false;
	}
}

void SliceSubAllocator::free_backing_heap(AllocatedSlice *allocation) const
{
	if (parent)
		parent->free(allocation->heap, allocation->mask);
	else if (global_allocator)
		global_allocator->free(allocation->buffer_index);
}

void SliceSubAllocator::prepare_allocation(AllocatedSlice *allocation, Util::IntrusiveList<MiniHeap>::Iterator heap,
                                           const Util::SuballocationResult &suballoc)
{
	allocation->buffer_index = heap->allocation.buffer_index;
	allocation->offset = heap->allocation.offset + suballoc.offset;
	allocation->count = suballoc.size;
	allocation->mask = suballoc.mask;
	allocation->heap = heap;
	allocation->alloc = this;
}

void SliceAllocator::init(uint32_t sub_block_size, uint32_t num_sub_blocks_in_arena_log2,
                          Util::SliceBackingAllocator *alloc)
{
	global_allocator = alloc;
	assert(num_sub_blocks_in_arena_log2 < SliceAllocatorCount * 5 && num_sub_blocks_in_arena_log2 >= 5);
	unsigned num_hierarchies = (num_sub_blocks_in_arena_log2 + 4) / 5;
	assert(num_hierarchies <= SliceAllocatorCount);

	for (unsigned i = 0; i < num_hierarchies - 1; i++)
		allocators[i].parent = &allocators[i + 1];
	allocators[num_hierarchies - 1].global_allocator = alloc;

	unsigned shamt[SliceAllocatorCount] = {};
	shamt[num_hierarchies - 1] = num_sub_blocks_in_arena_log2 - Util::floor_log2(Util::LegionAllocator::NumSubBlocks);

	// Spread out the multiplier if possible.
	for (unsigned i = num_hierarchies - 1; i > 1; i--)
	{
		shamt[i - 1] = shamt[i] - shamt[i] / (i);
		assert(shamt[i] - shamt[i - 1] <= Util::floor_log2(Util::LegionAllocator::NumSubBlocks));
	}

	for (unsigned i = 0; i < num_hierarchies; i++)
	{
		allocators[i].set_sub_block_size(sub_block_size << shamt[i]);
		allocators[i].set_object_pool(&object_pool);
	}
}

void SliceAllocator::free(const Util::AllocatedSlice &slice)
{
	if (slice.alloc)
		slice.alloc->free(slice.heap, slice.mask);
	else if (slice.buffer_index != UINT32_MAX)
		global_allocator->free(slice.buffer_index);
}

void SliceAllocator::prime(const void *opaque_meta)
{
	for (auto &alloc : allocators)
	{
		if (alloc.global_allocator)
		{
			alloc.global_allocator->prime(alloc.get_sub_block_size() * Util::LegionAllocator::NumSubBlocks, opaque_meta);
			break;
		}
	}
}

bool SliceAllocator::allocate(uint32_t count, Util::AllocatedSlice *slice)
{
	for (auto &alloc : allocators)
	{
		uint32_t max_alloc_size = alloc.get_max_allocation_size();
		if (count <= max_alloc_size)
			return alloc.allocate(count, slice);
	}

	LOGE("Allocation of %u elements is too large for SliceAllocator.\n", count);
	return false;
}

void SliceBackingAllocatorVA::free(uint32_t)
{
	allocated = false;
}

uint32_t SliceBackingAllocatorVA::allocate(uint32_t)
{
	if (allocated)
		return UINT32_MAX;
	else
	{
		allocated = true;
		return 0;
	}
}

void SliceBackingAllocatorVA::prime(uint32_t, const void *)
{
}
}
