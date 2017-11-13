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
	start_flush_buffer = 0;
	start_flush_offset = 0;
	host = nullptr;
}

ChainDataAllocation ChainAllocator::allocate(VkDeviceSize size)
{
	BufferDomain ideal_domain = (usage & ~VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 ? BufferDomain::Device : BufferDomain::Host;
	VkBufferUsageFlags extra_usage = ideal_domain == BufferDomain::Device ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0;

	// Fallback to dedicated allocation.
	if (size > block_size)
	{
		auto gpu_buffer = device->create_buffer({ ideal_domain, size, usage | extra_usage }, nullptr);
		BufferHandle cpu_buffer;

		ChainDataAllocation alloc = {};

		// Try to map it, will fail unless the memory is host visible.
		alloc.data = device->map_host_buffer(*gpu_buffer, MEMORY_ACCESS_WRITE);
		if (!alloc.data)
		{
			// Fall back to host memory, and remember to sync to gpu on submission time using DMA queue. :)
			cpu_buffer = device->create_buffer({ BufferDomain::Host, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT }, nullptr);
			alloc.data = device->map_host_buffer(*cpu_buffer, MEMORY_ACCESS_WRITE);
		}
		else
			cpu_buffer = gpu_buffer;

		alloc.offset = 0;
		alloc.buffer = gpu_buffer.get();
		large_buffers.push_back({ cpu_buffer, gpu_buffer });
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
		auto gpu_buffer = device->create_buffer({ ideal_domain, block_size, usage | extra_usage }, nullptr);
		BufferHandle cpu_buffer;

		host = static_cast<uint8_t *>(device->map_host_buffer(*gpu_buffer, MEMORY_ACCESS_WRITE));
		if (!host)
		{
			cpu_buffer = device->create_buffer({ BufferDomain::Host, block_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT }, nullptr);
			host = static_cast<uint8_t *>(device->map_host_buffer(*cpu_buffer, MEMORY_ACCESS_WRITE));
		}
		else
			cpu_buffer = gpu_buffer;

		buffers.push_back({ cpu_buffer, gpu_buffer });
	}
	else if (offset == 0)
		host = static_cast<uint8_t *>(device->map_host_buffer(*buffers[chain_index].cpu, MEMORY_ACCESS_WRITE));

	ChainDataAllocation alloc = {};
	alloc.data = host + offset;
	alloc.offset = offset;
	alloc.buffer = buffers[chain_index].gpu.get();
	offset += size;
	return alloc;
}

VkBufferUsageFlags ChainAllocator::sync_to_gpu(CommandBufferHandle &cmd)
{
	bool need_sync = false;

	for (auto &buffer : large_buffers)
	{
		if (buffer.gpu != buffer.cpu)
		{
			need_sync = true;
			if (!cmd)
				cmd = device->request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->copy_buffer(*buffer.gpu, 0, *buffer.cpu, 0, buffer.gpu->get_create_info().size);
		}
	}
	large_buffers.clear();

	if (!buffers.empty())
	{
		auto &buffer = buffers[start_flush_buffer];

		bool flush_all = start_flush_buffer < chain_index;
		auto to_flush_bytes = (flush_all ? buffer.gpu->get_create_info().size : offset) - start_flush_offset;

		if (buffer.gpu != buffer.cpu && to_flush_bytes)
		{
			need_sync = true;
			if (!cmd)
				cmd = device->request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->copy_buffer(*buffer.gpu, start_flush_offset, *buffer.cpu, start_flush_offset, to_flush_bytes);
		}

		if (flush_all)
		{
			start_flush_offset = 0;
			start_flush_buffer++;
		}
		else
			start_flush_offset = offset;
	}

	// Completely flush nodes.
	while (start_flush_buffer < chain_index)
	{
		auto &buffer = buffers[start_flush_buffer];
		if (buffer.gpu != buffer.cpu)
		{
			need_sync = true;
			if (!cmd)
				cmd = device->request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->copy_buffer(*buffer.gpu, 0, *buffer.cpu, 0, buffer.gpu->get_create_info().size);
		}

		start_flush_buffer++;
		start_flush_offset = 0;
	}

	// The last node might not be fully complete, partially sync.
	// We might have done this sync already in the first branch ...
	if (start_flush_offset < offset && start_flush_buffer == chain_index)
	{
		auto &buffer = buffers[start_flush_buffer];
		if (buffer.gpu != buffer.cpu)
		{
			need_sync = true;
			if (!cmd)
				cmd = device->request_command_buffer(CommandBuffer::Type::Transfer);
			cmd->copy_buffer(*buffer.gpu, start_flush_offset, *buffer.cpu, start_flush_offset,
			                 offset - start_flush_offset);
		}

		start_flush_offset = offset;
	}

	return need_sync ? usage : 0;
}

void ChainAllocator::discard()
{
	chain_index = 0;
	offset = 0;
	start_flush_buffer = 0;
	start_flush_offset = 0;
	host = nullptr;
}
}
