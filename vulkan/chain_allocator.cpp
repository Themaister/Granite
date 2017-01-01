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
	host = static_cast<uint8_t *>(device->map_host_buffer(*buffers.back(), MEMORY_ACCESS_WRITE));
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
	large_buffers.clear();
}
}
