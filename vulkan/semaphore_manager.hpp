#pragma once

#include "vulkan.hpp"
#include <vector>

namespace Vulkan
{
class SemaphoreManager
{
public:
	void init(VkDevice device);
	~SemaphoreManager();

	VkSemaphore request_cleared_semaphore();
	void recycle(VkSemaphore semaphore);

private:
	VkDevice device = VK_NULL_HANDLE;
	std::vector<VkSemaphore> semaphores;
};
}
