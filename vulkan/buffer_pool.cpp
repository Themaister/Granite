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

#include "buffer_pool.hpp"
#include "device.hpp"
#include <utility>

using namespace std;

namespace Vulkan
{
void BufferPool::init(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage)
{
	this->device = device;
	this->block_size = block_size;
	this->alignment = alignment;
	this->usage = usage;
}

BufferBlock BufferPool::allocate_block(VkDeviceSize size)
{
	BufferDomain ideal_domain = (usage & ~VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0 ? BufferDomain::Device : BufferDomain::Host;
	VkBufferUsageFlags extra_usage = ideal_domain == BufferDomain::Device ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0;

	BufferBlock block;

	block.gpu = device->create_buffer({ ideal_domain, size, usage | extra_usage }, nullptr);

	// Try to map it, will fail unless the memory is host visible.
	block.mapped = static_cast<uint8_t *>(device->map_host_buffer(*block.gpu, MEMORY_ACCESS_WRITE));
	if (!block.mapped)
	{
		// Fall back to host memory, and remember to sync to gpu on submission time using DMA queue. :)
		block.cpu = device->create_buffer({ BufferDomain::Host, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT }, nullptr);
		block.mapped = static_cast<uint8_t *>(device->map_host_buffer(*block.cpu, MEMORY_ACCESS_WRITE));
	}
	else
		block.cpu = block.gpu;

	block.offset = 0;
	block.alignment = alignment;
	block.size = size;
	return block;
}

BufferBlock BufferPool::request_block(VkDeviceSize minimum_size)
{
	if ((minimum_size > block_size) || blocks.empty())
	{
		return allocate_block(max(block_size, minimum_size));
	}
	else
	{
		auto back = move(blocks.back());
		blocks.pop_back();

		back.mapped = static_cast<uint8_t *>(device->map_host_buffer(*back.cpu, MEMORY_ACCESS_WRITE));
		back.offset = 0;
		return back;
	}
}

void BufferPool::recycle_block(BufferBlock &&block)
{
	VK_ASSERT(block.size == block_size);
	blocks.push_back(move(block));
}

}