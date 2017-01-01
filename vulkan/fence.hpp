#pragma once

#include <memory>

namespace Vulkan
{
class Device;

class FenceHolder
{
public:
	FenceHolder(Device *device, VkFence fence)
	    : device(device)
	    , fence(fence)
	{
	}

	const VkFence &get_fence() const
	{
		return fence;
	}

private:
	Device *device;
	VkFence fence;
};

using Fence = std::weak_ptr<FenceHolder>;
}
