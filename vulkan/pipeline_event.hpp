#pragma once

#include "vulkan.hpp"
#include "intrusive.hpp"

namespace Vulkan
{
class Device;

class EventHolder : public Util::IntrusivePtrEnabled<EventHolder>
{
public:
	EventHolder(Device *device, VkEvent event)
	    : device(device)
	    , event(event)
	{
	}

	~EventHolder();

	const VkEvent &get_event() const
	{
		return event;
	}

	VkPipelineStageFlags get_stages() const
	{
		return stages;
	}

	void set_stages(VkPipelineStageFlags stages)
	{
		this->stages = stages;
	}

private:
	Device *device;
	VkEvent event;
	VkPipelineStageFlags stages = 0;
};

using PipelineEvent = Util::IntrusivePtr<EventHolder>;

}