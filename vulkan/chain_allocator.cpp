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

#include "chain_allocator.hpp"
#include "device.hpp"

namespace Vulkan
{
ChainAllocator::ChainAllocator(Device *device, VkDeviceSize block_size, VkDeviceSize alignment,
                               VkBufferUsageFlags usage)
    : device(device)
    , block_size(block_size)
    , alignment(alignment)
    , usage(usage)
{
	buffers.push_back(device->create_buffer({ BufferDomain::Host, block_size, usage }, nullptr));
}

ChainAllocator::~ChainAllocator()
{
	discard();
}

void ChainAllocator::reset()
{
	buffers.clear();
	large_buffers.clear();
	offset = 0;
	chain_index = 0;
}

ChainDataAllocation ChainAllocator::allocate(VkDeviceSize size)
{
	// Fallback to dedicated allocation.
	if (size > block_size)
	{
		auto buffer = device->create_buffer({ BufferDomain::Host, size, usage }, nullptr);
		ChainDataAllocation alloc = {};
		alloc.data = device->map_host_buffer(*buffer, MEMORY_ACCESS_WRITE);
		alloc.offset = 0;
		alloc.buffer = buffer.get();
		large_buffers.push_back(buffer);
		return alloc;
	}

	offset = (offset + alignment - 1) & ~(alignment - 1);
	if (offset + size > block_size)
	{
		chain_index++;
		offset = 0;
	}

	if (chain_index >= buffers.size())
	{
		buffers.push_back(device->create_buffer({ BufferDomain::Host, block_size, usage }, nullptr));
		host = static_cast<uint8_t *>(device->map_host_buffer(*buffers.back(), MEMORY_ACCESS_WRITE));
	}
	else if (offset == 0)
		host = static_cast<uint8_t *>(device->map_host_buffer(*buffers[chain_index], MEMORY_ACCESS_WRITE));

	ChainDataAllocation alloc = {};
	alloc.data = host + offset;
	alloc.offset = offset;
	alloc.buffer = buffers[chain_index].get();
	offset += size;
	return alloc;
}

void ChainAllocator::discard()
{
	chain_index = 0;
	offset = 0;
	start_flush_index = 0;
	host = nullptr;
	large_buffers.clear();
}
}
