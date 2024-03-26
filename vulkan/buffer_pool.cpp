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

#define NOMINMAX
#include "buffer_pool.hpp"
#include "device.hpp"
#include <utility>

namespace Vulkan
{
void BufferPool::init(Device *device_, VkDeviceSize block_size_,
                      VkDeviceSize alignment_, VkBufferUsageFlags usage_)
{
	device = device_;
	block_size = block_size_;
	alignment = alignment_;
	usage = usage_;
}

void BufferPool::set_spill_region_size(VkDeviceSize spill_size_)
{
	spill_size = spill_size_;
}

void BufferPool::set_max_retained_blocks(size_t max_blocks)
{
	max_retained_blocks = max_blocks;
}

BufferBlock::~BufferBlock()
{
}

void BufferPool::reset()
{
	blocks.clear();
}

BufferBlock BufferPool::allocate_block(VkDeviceSize size)
{
	BufferDomain ideal_domain = ((usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0) ?
			BufferDomain::Host : BufferDomain::LinkedDeviceHost;

	BufferBlock block;

	BufferCreateInfo info;
	info.domain = ideal_domain;
	info.size = size;
	info.usage = usage;

	block.buffer = device->create_buffer(info, nullptr);
	device->set_name(*block.buffer, "chain-allocated-block");
	block.buffer->set_internal_sync_object();

	// Try to map it, will fail unless the memory is host visible.
	block.mapped = static_cast<uint8_t *>(device->map_host_buffer(*block.buffer, MEMORY_ACCESS_WRITE_BIT));

	block.offset = 0;
	block.alignment = alignment;
	block.size = size;
	block.spill_size = spill_size;
	return block;
}

BufferBlock BufferPool::request_block(VkDeviceSize minimum_size)
{
	if ((minimum_size > block_size) || blocks.empty())
	{
		return allocate_block(std::max(block_size, minimum_size));
	}
	else
	{
		auto back = std::move(blocks.back());
		blocks.pop_back();

		back.mapped = static_cast<uint8_t *>(device->map_host_buffer(*back.buffer, MEMORY_ACCESS_WRITE_BIT));
		back.offset = 0;
		return back;
	}
}

void BufferPool::recycle_block(BufferBlock &block)
{
	VK_ASSERT(block.size == block_size);

	if (blocks.size() < max_retained_blocks)
		blocks.push_back(std::move(block));
	else
		block = {};
}

BufferPool::~BufferPool()
{
	VK_ASSERT(blocks.empty());
}

BufferBlockAllocation BufferBlock::allocate(VkDeviceSize allocate_size)
{
	auto aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
	if (aligned_offset + allocate_size <= size)
	{
		auto *ret = mapped + aligned_offset;
		offset = aligned_offset + allocate_size;

		VkDeviceSize padded_size = std::max<VkDeviceSize>(allocate_size, spill_size);
		padded_size = std::min<VkDeviceSize>(padded_size, size - aligned_offset);

		return { ret, buffer, aligned_offset, padded_size };
	}
	else
		return { nullptr, {}, 0, 0 };
}

void BufferBlock::unmap(Device &device)
{
	device.unmap_host_buffer(*buffer, MEMORY_ACCESS_WRITE_BIT);
	mapped = nullptr;
}
}
