#include "command_pool.hpp"

namespace Vulkan
{
CommandPool::CommandPool(VkDevice device, uint32_t queue_family_index)
    : device(device)
{
	VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	info.queueFamilyIndex = queue_family_index;
	vkCreateCommandPool(device, &info, nullptr, &pool);
}

CommandPool::~CommandPool()
{
	if (!buffers.empty())
		vkFreeCommandBuffers(device, pool, buffers.size(), buffers.data());
	vkDestroyCommandPool(device, pool, nullptr);
}

#ifdef VULKAN_DEBUG
#endif

void CommandPool::signal_submitted(VkCommandBuffer cmd)
{
#ifdef VULKAN_DEBUG
	VK_ASSERT(in_flight.find(cmd) != end(in_flight));
	in_flight.erase(cmd);
#endif
}

VkCommandBuffer CommandPool::request_command_buffer()
{
	if (index < buffers.size())
	{
		auto ret = buffers[index++];
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.find(ret) == end(in_flight));
		in_flight.insert(ret);
#endif
		return ret;
	}
	else
	{
		VkCommandBuffer cmd;
		VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		info.commandPool = pool;
		info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		info.commandBufferCount = 1;

		vkAllocateCommandBuffers(device, &info, &cmd);
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.find(cmd) == end(in_flight));
		in_flight.insert(cmd);
#endif
		buffers.push_back(cmd);
		index++;
		return cmd;
	}
}

void CommandPool::begin()
{
#ifdef VULKAN_DEBUG
	VK_ASSERT(in_flight.empty());
#endif
	vkResetCommandPool(device, pool, 0);
	index = 0;
}
}
