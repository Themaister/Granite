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

#pragma once

#include "buffer.hpp"
#include "vulkan.hpp"
#include <vector>

namespace Vulkan
{
class Device;
struct ChainDataAllocation
{
	const Buffer *buffer;
	VkDeviceSize offset;
	void *data;
};

class ChainAllocator
{
public:
	ChainAllocator(Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage);
	~ChainAllocator();

	ChainDataAllocation allocate(VkDeviceSize size);
	void discard();
	void reset();

	void sync_to_gpu();

private:
	Device *device;
	VkDeviceSize block_size;
	VkDeviceSize alignment;
	VkBufferUsageFlags usage;

	struct SyncedBuffer
	{
		BufferHandle cpu;
		BufferHandle gpu;
	};

	std::vector<SyncedBuffer> buffers;
	std::vector<SyncedBuffer> large_buffers;
	unsigned chain_index = 0;
	unsigned start_flush_buffer = 0;
	VkDeviceSize start_flush_offset = 0;
	VkDeviceSize offset = 0;
	VkDeviceSize size = 0;
	uint8_t *host = nullptr;
};
}
