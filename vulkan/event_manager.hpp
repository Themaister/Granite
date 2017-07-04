#pragma once

#include "vulkan.hpp"
#include <vector>

namespace Vulkan
{
class EventManager
{
public:
	void init(VkDevice device);
	~EventManager();

	VkEvent request_cleared_event();
	void recycle(VkEvent event);

private:
	VkDevice device = VK_NULL_HANDLE;
	std::vector<VkEvent> events;
};
}