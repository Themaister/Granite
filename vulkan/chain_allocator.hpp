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

private:
	Device *device;
	VkDeviceSize block_size;
	VkDeviceSize alignment;
	VkBufferUsageFlags usage;

	std::vector<BufferHandle> buffers;
	std::vector<BufferHandle> large_buffers;
	unsigned chain_index = 0;
	unsigned start_flush_index = 0;
	VkDeviceSize offset = 0;
	VkDeviceSize size = 0;
	uint8_t *host = nullptr;
};
}
