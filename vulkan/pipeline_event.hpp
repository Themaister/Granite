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

private:
	Device *device;
	VkEvent event;
};

using PipelineEvent = Util::IntrusivePtr<EventHolder>;

}