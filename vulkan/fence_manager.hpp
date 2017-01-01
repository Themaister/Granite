#pragma once

#include "vulkan.hpp"
#include <vector>

namespace Vulkan
{
class FenceManager
{
public:
	FenceManager(VkDevice device);
	~FenceManager();

	void begin();
	VkFence request_cleared_fence();

private:
	VkDevice device;
	std::vector<VkFence> fences;
	unsigned index = 0;
};
}
