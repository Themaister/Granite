#pragma once

#include "vulkan.hpp"
#include <unordered_set>
#include <vector>

namespace Vulkan
{
class CommandPool
{
public:
	CommandPool(VkDevice device, uint32_t queue_family_index);
	~CommandPool();

	void begin();
	VkCommandBuffer request_command_buffer();
	void signal_submitted(VkCommandBuffer cmd);

private:
	VkDevice device;
	VkCommandPool pool;
	std::vector<VkCommandBuffer> buffers;
#ifdef VULKAN_DEBUG
	std::unordered_set<VkCommandBuffer> in_flight;
#endif
	unsigned index = 0;
};
}
