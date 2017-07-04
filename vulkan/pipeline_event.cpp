#include "pipeline_event.hpp"
#include "device.hpp"

namespace Vulkan
{
EventHolder::~EventHolder()
{
	if (event)
		device->destroy_event(event);
}
}