/* Copyright (c) 2017-2026 Hans-Kristian Arntzen
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

#include "vulkan_headers.hpp"
#include "intrusive.hpp"
#include <vector>
#include <algorithm>

namespace Vulkan
{
class Device;
class Buffer;

struct BufferBlockAllocation
{
	uint8_t *host;
	Util::IntrusivePtr<Buffer> buffer;
	VkDeviceSize offset;
	VkDeviceSize padded_size;
};

class BufferBlock
{
public:
	~BufferBlock();

	BufferBlockAllocation allocate(VkDeviceSize allocate_size);
	bool is_mapped() const { return mapped != nullptr; }
	const Buffer &get_buffer() const { return *buffer; }
	void unmap(Device &device);

	VkDeviceSize get_offset() const { return offset; }
	VkDeviceSize get_size() const { return size; }

private:
	friend class BufferPool;
	Util::IntrusivePtr<Buffer> buffer;
	VkDeviceSize offset = 0;
	VkDeviceSize alignment = 0;
	VkDeviceSize size = 0;
	uint8_t *mapped = nullptr;
};

class BufferPool
{
public:
	~BufferPool();
	void init(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage);
	void reset();

	void set_max_retained_blocks(size_t max_blocks);

	VkDeviceSize get_block_size() const
	{
		return block_size;
	}

	BufferBlock request_block(VkDeviceSize minimum_size);
	void recycle_block(BufferBlock &block);

private:
	Device *device = nullptr;
	VkDeviceSize block_size = 0;
	VkDeviceSize alignment = 0;
	VkBufferUsageFlags usage = 0;
	size_t max_retained_blocks = 0;
	std::vector<BufferBlock> blocks;
	BufferBlock allocate_block(VkDeviceSize size);
};
}