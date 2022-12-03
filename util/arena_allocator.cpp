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
}
